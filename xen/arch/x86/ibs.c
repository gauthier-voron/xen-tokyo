#include <asm/atomic.h>
#include <asm/ibs.h>
#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/xenoprof.h>
#include <xen/spinlock.h>
#include <xen/time.h>

/*
 * IBS should be initialized by the ibs_init() function of the Xenoprofile
 * module at boot time.
 * In Xen, the performance counting resource initialization and allocation is
 * a mess, so let's deal with it...
 */

#include "oprofile/op_counter.h"


static int ibs_acquired = 0;

static int ibs_enabled = 0;

static void (*ibs_handler)(const struct ibs_record *record,
			   const struct cpu_user_regs *regs) = NULL;


static DEFINE_PER_CPU(atomic_t, ibs_disabling);
static DEFINE_PER_CPU(atomic_t, ibs_disabled);


static DEFINE_PER_CPU(unsigned long, ibs_current_rate);


/* count of nanoseconds between two modifications of the IBS rate */
static unsigned long ibs_tick_period;

/* count of IBS NMI each core wants to reach at the next tick */
static unsigned long ibs_nmi_goal;

/* time of the last tick */
static DEFINE_PER_CPU(unsigned long, ibs_last_tick);

/* actual count of IBS NMI since the last tick */
static DEFINE_PER_CPU(unsigned long, ibs_nmi_count);

/* divisor of each increment (or decrement) to the current rate */
#define IBS_RATE_INERTIA   10

#define IBS_RATE_MIN       65536
#define IBS_RATE_MAX       (cpu_khz)
#define IBS_IPS_SPLIT_MIN  500


static inline void ibs_adapt_rate(void)
{
    unsigned long cur, vec;
    unsigned long now = NOW();

    if ( ! ibs_nmi_goal )
        return;

    if ( now > this_cpu(ibs_last_tick) + ibs_tick_period ) {
        cur = this_cpu(ibs_current_rate);
        vec = cur * this_cpu(ibs_nmi_count) / ibs_nmi_goal;

        if ( vec < cur )
            this_cpu(ibs_current_rate) -= (cur - vec) / IBS_RATE_INERTIA;
        else
            this_cpu(ibs_current_rate) += (vec - cur) / IBS_RATE_INERTIA;

        if ( this_cpu(ibs_current_rate) < IBS_RATE_MIN )
            this_cpu(ibs_current_rate) = IBS_RATE_MIN;
        if ( this_cpu(ibs_current_rate) > IBS_RATE_MAX )
            this_cpu(ibs_current_rate) = IBS_RATE_MAX;

        this_cpu(ibs_last_tick) = now;
        this_cpu(ibs_nmi_count) = 0;
    }

    this_cpu(ibs_nmi_count)++;
}

static inline void handle_ibs_fetch(u64 ctrl, const struct cpu_user_regs *regs)
{
    struct ibs_record record;

    record.record_mode = IBS_RECORD_MODE_FETCH | IBS_RECORD_MODE_ILA;

    record.fetch_infos = ctrl;

    rdmsr_safe(MSR_AMD64_IBSFETCHLINAD, record.inst_linear_address);

    if ( ctrl & IBS_RECORD_IPA )
    {
        record.record_mode |= IBS_RECORD_MODE_IPA;
        rdmsr_safe(MSR_AMD64_IBSFETCHPHYSAD, record.inst_physical_address);
    }

    ibs_handler(&record, regs);
}

static inline void handle_ibs_op(const struct cpu_user_regs *regs)
{
    struct ibs_record record;
    
    record.record_mode = IBS_RECORD_MODE_OP;

    rdmsr_safe(MSR_AMD64_IBSOPDATA, record.branch_infos);
    rdmsr_safe(MSR_AMD64_IBSOPDATA2, record.northbridge_infos);
    rdmsr_safe(MSR_AMD64_IBSOPDATA3, record.cache_infos);

    if ( record.branch_infos & IBS_RECORD_ILA )
    {
        record.record_mode |= IBS_RECORD_MODE_ILA;
        rdmsr_safe(MSR_AMD64_IBSOPRIP, record.inst_linear_address);
    }

    if ( record.cache_infos & IBS_RECORD_DLA )
    {
        record.record_mode |= IBS_RECORD_MODE_DLA;
        rdmsr_safe(MSR_AMD64_IBSDCLINAD, record.data_linear_address);
    }

    if ( record.cache_infos & IBS_RECORD_DPA )
    {
        record.record_mode |= IBS_RECORD_MODE_DPA;
        rdmsr_safe(MSR_AMD64_IBSDCPHYSAD, record.data_physical_address);
    }

    ibs_handler(&record, regs);
}

int nmi_ibs(const struct cpu_user_regs *regs)
{
    u64 val;
    int ret = 0;

    ibs_adapt_rate();

    rdmsr_safe(MSR_AMD64_IBSFETCHCTL, val);
    if ( val & IBS_FETCH_VAL )
    {
        /* TODO: what is the goal of this test ? */
        if ( val & IBS_FETCH_ENABLE && ibs_handler )
            handle_ibs_fetch(val, regs);

        val &= ~(IBS_FETCH_VAL | IBS_FETCH_CNT | IBS_FETCH_MAX_CNT);
        val |= IBS_FETCH_SET_MAX_CNT(this_cpu(ibs_current_rate));
        wrmsr_safe(MSR_AMD64_IBSFETCHCTL, val);
        ret = 1;
    }

    rdmsr_safe(MSR_AMD64_IBSOPCTL, val);
    if ( val & IBS_OP_VAL )
    {
        /* TODO: what is the goal of this test ? */
        if ( val & IBS_OP_ENABLE && ibs_handler )
            handle_ibs_op(regs);

        val &= ~(IBS_OP_VAL | IBS_OP_CNT | IBS_OP_MAX_CNT);
        val |= IBS_OP_SET_MAX_CNT(this_cpu(ibs_current_rate));
        wrmsr_safe(MSR_AMD64_IBSOPCTL, val);
        ret = 1;
    }

    /*
     * Delayed disabling.
     * See ibs_disable() for an explanation about that.
     */

    if (atomic_read(&this_cpu(ibs_disabling))) {

        wrmsr_safe(MSR_AMD64_IBSFETCHCTL, 0);
        wrmsr_safe(MSR_AMD64_IBSOPCTL, 0);

        atomic_set(&this_cpu(ibs_disabled), 1);

    }

    return ret;
}


int ibs_capable(void)
{
    if ( boot_cpu_data.x86_vendor != X86_VENDOR_AMD )
        return 0;
    if ( !test_bit(X86_FEATURE_IBS, boot_cpu_data.x86_capability) )
        return 0;
    return 1;
}


static DEFINE_SPINLOCK(ibs_lock);


int ibs_setevent(unsigned long event)
{
    if ( !ibs_acquired )
        return -1;
    ibs_config.op_enabled = event & IBS_EVENT_OP;
    ibs_config.fetch_enabled = event & IBS_EVENT_FETCH;
    return 0;
}


static void ibs_adjust_ips_params(unsigned long rate)
{
    unsigned long period = 1000000000;  /* 1 second (in nanoseconds) */

    while ( rate >= IBS_IPS_SPLIT_MIN && period >= 10 )
    {
        rate /= 10;
        period /= 10;
    }

    ibs_nmi_goal = rate;
    ibs_tick_period = period;
}

int ibs_setrate(unsigned long rate, int flags)
{
    unsigned long raw;
    int cpu;

    if ( !ibs_acquired )
        return -1;

    switch ( flags & IBS_RATE_TYPE )
    {
    case IBS_RATE_RAW:
        raw = rate;
        ibs_nmi_goal = 0;
        break;
    case IBS_RATE_IPS:
        raw = (1000 * cpu_khz) / rate;
        ibs_adjust_ips_params(rate);
        break;
    default:
        return -1;
    }

    for_each_online_cpu ( cpu )
        per_cpu(ibs_current_rate, cpu) = raw;

    ibs_config.max_cnt_fetch = raw;
    ibs_config.max_cnt_op = raw;
    return 0;
}

int ibs_sethandler(void (*handler)(const struct ibs_record *record,
				   const struct cpu_user_regs *regs))
{
    if ( !ibs_acquired )
        return -1;
    ibs_handler = handler;
    return 0;
}

int ibs_enable(void)
{
    int err = -1;

    spin_lock(&ibs_lock);

    if ( !ibs_acquired )
	    goto out;
    err = xenoprof_arch_start();
    if ( err )
	    goto out;
    ibs_enabled = 1;

    err = 0;
 out:
    spin_unlock(&ibs_lock);
    return err;
}


static void __ibs_force_nmi(void *unused)
{
    wrmsr_safe(MSR_AMD64_IBSFETCHCTL, IBS_FETCH_ENABLE | 0x1000);
    wrmsr_safe(MSR_AMD64_IBSOPCTL, IBS_OP_ENABLE | IBS_OP_SET_MAX_CNT(0x1000));

    atomic_set(&this_cpu(ibs_disabling), 1);
}

static void __ibs_disable(void)
{
    int cpu;

    if ( !ibs_enabled )
        return;
    ibs_enabled = 0;

    /*
     * The xenoprof stop code does not handle race conditions very well with
     * already running IBS interrupt.
     * So the strategy to deal with it is the following:
     * - for each cpu, increase the sampling rate (a lot) and set a flag
     *   indicating the next NMI is the last one
     * - in the NMI, when seeing the flag, disable its own cpu interrupt and
     *   set another flag to indicate it
     * - in this function, wait for all flags being updated
     * - then call the xenoprof whatever broken code
     */

	on_each_cpu(__ibs_force_nmi, NULL, 1);

    for_each_online_cpu ( cpu )
        while (!atomic_read(&per_cpu(ibs_disabled, cpu)))
            cpu_relax();

    for_each_online_cpu ( cpu ) {
        atomic_set(&per_cpu(ibs_disabling, cpu), 0);
        atomic_set(&per_cpu(ibs_disabled, cpu), 0);
    }

    xenoprof_arch_stop();

}

void ibs_disable(void)
{
    spin_lock(&ibs_lock);
    __ibs_disable();
    spin_unlock(&ibs_lock);
}

int ibs_acquire(void)
{
    int err = -1;

    spin_lock(&ibs_lock);

    if ( !ibs_capable() )
        goto out;
    if ( ibs_acquired )
        goto out;
    err = xenoprof_arch_reserve_counters();
    if ( err )
        goto out;
    ibs_acquired = 1;

    err = 0;
 out:
    spin_unlock(&ibs_lock);
    return 0;
}

void ibs_release(void)
{
    spin_lock(&ibs_lock);

    if ( !ibs_acquired )
	    goto out;
    if ( ibs_enabled )
        __ibs_disable();

    ibs_config.op_enabled = 0;
    ibs_config.fetch_enabled = 0;
    xenoprof_arch_release_counters();
    ibs_acquired = 0;

 out:
    spin_unlock(&ibs_lock);
}


/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
