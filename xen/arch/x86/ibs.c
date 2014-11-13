#include <asm/ibs.h>
#include <asm/msr.h>
#include <asm/processor.h>
#include <asm/xenoprof.h>


/*
 * IBS should be initialized by the ibs_init() function of the Xenoprofile
 * module at boot time.
 * In Xen, the performance counting resource initialization and allocation is
 * a mess, so let's deal with it...
 */

#include "oprofile/op_counter.h"


static int ibs_acquired = 0;

static int ibs_enabled = 0;

static void (*ibs_handler)(struct ibs_record *record, int cpu) = NULL;


int nmi_ibs(int cpu)
{
    u64 val;
    int ret = 0;
    struct ibs_record record;

    rdmsr_safe(MSR_AMD64_IBSFETCHCTL, val);
    if ( val & IBS_FETCH_ENABLE && val & IBS_FETCH_VAL )
    {
        record.record_mode = IBS_RECORD_MODE_FETCH
            | IBS_RECORD_MODE_ILA;

        record.fetch_infos = val;
        rdmsr_safe(MSR_AMD64_IBSFETCHLINAD, record.inst_linear_address);

        if ( val & IBS_RECORD_IPA )
        {
            record.record_mode |= IBS_RECORD_MODE_IPA;
            rdmsr_safe(MSR_AMD64_IBSFETCHPHYSAD,record.inst_physical_address);
        }

        if ( ibs_handler )
            ibs_handler(&record, cpu);
        val &= ~(IBS_FETCH_VAL | IBS_FETCH_CNT);
        wrmsr_safe(MSR_AMD64_IBSFETCHCTL, val);
        ret = 1;
    }

    rdmsr_safe(MSR_AMD64_IBSOPCTL, val);
    if ( val & IBS_OP_ENABLE && val & IBS_OP_VAL )
    {
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

        if ( ibs_handler )
            ibs_handler(&record, cpu);
        val &= ~(IBS_OP_VAL | IBS_OP_CNT);
        wrmsr_safe(MSR_AMD64_IBSOPCTL, val);
        ret = 1;
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

int ibs_acquire(void)
{
    int ret;

    if ( !ibs_capable() )
        return -1;
    if ( ibs_acquired )
        return -1;
    ret = xenoprof_arch_reserve_counters();
    if ( ret )
        return ret;
    ibs_acquired = 1;
    return 0;
}

void ibs_release(void)
{
    if ( !ibs_acquired )
        return;
    if ( ibs_enabled )
        ibs_disable();

    ibs_config.op_enabled = 0;
    ibs_config.fetch_enabled = 0;
    xenoprof_arch_release_counters();
    ibs_acquired = 0;
}

int ibs_setevent(unsigned long event)
{
    if ( !ibs_acquired )
        return -1;
    ibs_config.op_enabled = event & IBS_EVENT_OP;
    ibs_config.fetch_enabled = event & IBS_EVENT_FETCH;
    return 0;
}

int ibs_setrate(unsigned long rate)
{
    if ( !ibs_acquired )
        return -1;
    ibs_config.max_cnt_fetch = rate;
    ibs_config.max_cnt_op = rate;
    return 0;
}

int ibs_sethandler(void (*handler)(struct ibs_record *record, int cpu))
{
    if ( !ibs_acquired )
        return -1;
    ibs_handler = handler;
    return 0;
}

int ibs_enable(void)
{
    int ret;

    if ( !ibs_acquired )
        return -1;
    ret = xenoprof_arch_start();
    if ( ret )
        return ret;
    ibs_enabled = 1;
    return 0;
}

void ibs_disable(void)
{
    if ( !ibs_enabled )
        return;
    xenoprof_arch_stop();
    ibs_enabled = 0;
}
