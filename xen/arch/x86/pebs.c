#include <asm/apic.h>
#include <asm/pebs.h>
#include <asm/msr.h>
#include <xen/config.h>
#include <xen/lib.h>
#include <xen/mm.h>
#include <xen/percpu.h>
#include <xen/types.h>

#define PEBS_SIZE_CORE   144        /* PEBS size for Intel Core & Intel Atom */
#define PEBS_SIZE_NHM    176  /* PEBS size for Nehalem, Sandy and Ivy Bridge */
#define PEBS_SIZE_HSW    192                        /* PEBS size for Haswell */


/*
 * The debug store layout with an undefined amount of counter reset fields.
 * This is so because the amount is implementation dependent.
 * This layout is only accurate for 64-bits architectures.
 */
struct debug_store
{
    u64 bts_buffer_base;
    u64 bts_index;
    u64 bts_absolute_maximum;
    u64 bts_interrupt_threshold;
    u64 pebs_buffer_base;
    u64 pebs_index;
    u64 pebs_absolute_maximum;
    u64 pebs_interrupt_threshold;
    u64 pebs_counter_reset[0];
} __attribute__((packed));


static void __rdmsr_cpu(void *args)
{
    u64 *ret = (u64 *) args;
    u64 *msr = ret + 1;
    u64 *val = msr + 1;

    *ret = rdmsr_safe(*msr, *val);
}

/*
 * Read the specified MSR on the specified CPU.
 * Return 0 in case of success, otherwise the val output parameter is not
 * modified.
 */
int rdmsr_cpu(u64 msr, u64 *val, int cpu)
{
    cpumask_t cpumask;
    u64 args[] = { 0, msr, 0 };

    cpumask_clear(&cpumask);
    cpumask_set_cpu(cpu, &cpumask);

    if ( cpu == smp_processor_id() )
        __rdmsr_cpu(args);
    else
        on_selected_cpus(&cpumask, __rdmsr_cpu, args, 1);

    if ( !args[0] )
        *val = args[2];
    return (int) args[0];
}


static void __wrmsr_cpu(void *args)
{
    u64 *ret = (u64 *) args;
    u64 *msr = ret + 1;
    u64 *val = msr + 1;

    *ret = wrmsr_safe(*msr, *val);
}

/*
 * Write on the specified register on the specified CPU.
 * Return 0 in case of success.
 */
int wrmsr_cpu(u64 msr, u64 val, int cpu)
{
    cpumask_t cpumask;
    u64 args[] = { 0, msr, val };

    cpumask_clear(&cpumask);
    cpumask_set_cpu(cpu, &cpumask);

    if ( cpu == smp_processor_id() )
        __wrmsr_cpu(args);
    else
        on_selected_cpus(&cpumask, __wrmsr_cpu, args, 1);

    return (int) args[0];
}


static void __apic_read_cpu(void *args)
{
    u64 *ret = (u64 *) args;
    u64 *reg = ret + 1;

    *ret = (u64) apic_read((unsigned long) *reg);
}

/*
 * Read the APIC register on the specified CPU.
 * Return the read value.
 */
u32 apic_read_cpu(unsigned long reg, int cpu)
{
    cpumask_t cpumask;
    u64 args[] = { 0, reg };

    cpumask_clear(&cpumask);
    cpumask_set_cpu(cpu, &cpumask);

    if ( cpu == smp_processor_id() )
        __apic_read_cpu(args);
    else
        on_selected_cpus(&cpumask, __apic_read_cpu, args, 1);

    return (u32) args[0];
}


static void __apic_write_cpu(void *args)
{
    u64 *reg = (u64 *) args;
    u64 *val = reg + 1;

    apic_write((unsigned long) *reg, (u32) *val);
}

/*
 * Write on the APIC registers for the specified CPU.
 */
void apic_write_cpu(unsigned long reg, u32 val, int cpu)
{
    cpumask_t cpumask;
    u64 args[] = { reg, val };

    cpumask_clear(&cpumask);
    cpumask_set_cpu(cpu, &cpumask);

    if ( cpu == smp_processor_id() )
        __apic_write_cpu(args);
    else
        on_selected_cpus(&cpumask, __apic_write_cpu, args, 1);
}





/*
 * The reference counters for the debug store of each CPU.
 */
DEFINE_PER_CPU(unsigned long, debug_store_refcnt);

/*
 * The spinlock used to protect every get/put for debug store.
 */
DEFINE_SPINLOCK(debug_store_spinlock);

/*
 * Get a reference on the debug store of the specified CPU.
 * Allocate and install a debug store if there is none.
 * The function will fail if there is no more memory.
 * Return a pointer to the debug store in case of success, or NULL otherwise.
 */
static struct debug_store *get_debug_store(int cpu)
{
    struct debug_store *ds = NULL;

    spin_lock(&debug_store_spinlock);

    if ( per_cpu(debug_store_refcnt, cpu) > 0 )
    {
        rdmsr_cpu(MSR_IA32_DS_AREA, (u64 *) &ds, cpu);
        goto out;
    }

    ds = alloc_xenheap_page();
    if ( ds == NULL )
        goto err;

    memset(ds, 0, PAGE_SIZE);
    wrmsr_cpu(MSR_IA32_DS_AREA, (u64) ds, cpu);

 out:
    per_cpu(debug_store_refcnt, cpu)++;
 err:
    spin_unlock(&debug_store_spinlock);
    return ds;
}

/*
 * Release a reference on the debug store of the specified CPU.
 * Free the debug store if there is no more reference.
 */
static void put_debug_store(int cpu)
{
    struct debug_store *ds;

    spin_lock(&debug_store_spinlock);

    if ( per_cpu(debug_store_refcnt, cpu) == 0 )
        goto out;

    per_cpu(debug_store_refcnt, cpu)--;
    if ( per_cpu(debug_store_refcnt, cpu) > 0 )
        goto out;

    rdmsr_cpu(MSR_IA32_DS_AREA, (u64 *) &ds, cpu);
    wrmsr_cpu(MSR_IA32_DS_AREA, 0, cpu);

    free_xenheap_page(ds);

 out:
    spin_unlock(&debug_store_spinlock);
}


/* Only compatible with Haswell architecture for now */
static unsigned long pebs_record_size;

/*
 * The reference counters for the PEBS record array of each CPU.
 */
DEFINE_PER_CPU(unsigned long, pebs_records_refcnt);

/*
 * The spinlock used to protect every get/put for pebs record arrays.
 */
DEFINE_SPINLOCK(pebs_records_spinlock);

/*
 * Get a reference on the PEBS record array of the current CPU.
 * Allocate a PEBS record array if there is none, and a debug store for the
 * specified CPU if necessary.
 * This function takes a reference on the debug store of the specified CPU
 * while the PEBS record array is allocated.
 * This function will fail if there is no more memory available.
 * Return the address of the PEBS record array in case of success, or NULL
 * otherwise.
 */
static struct pebs_record *get_pebs_records(int cpu)
{
    struct pebs_record *pebs = NULL;
    struct debug_store *ds;
    u64 base;

    spin_lock(&pebs_records_spinlock);

    ds = get_debug_store(cpu);
    if ( ds == NULL )
        goto err;

    if ( per_cpu(pebs_records_refcnt, cpu) > 0 )
    {
        pebs = (struct pebs_record *) ds->pebs_buffer_base;
        goto out;
    }

    pebs = alloc_xenheap_page();
    if ( pebs == NULL )
        goto put;

    base = (u64) pebs;
    ds->pebs_buffer_base = base;
    ds->pebs_index = base;
    ds->pebs_absolute_maximum = base + PAGE_SIZE;
    ds->pebs_interrupt_threshold = base + PAGE_SIZE;
    ds->pebs_interrupt_threshold -= PAGE_SIZE % pebs_record_size;

    get_debug_store(cpu);

 out:
    per_cpu(pebs_records_refcnt, cpu)++;
 put:
    put_debug_store(cpu);
 err:
    spin_unlock(&pebs_records_spinlock);
    return pebs;
}

/*
 * Release a reference on the PEBS record array of the specified CPU.
 * Free the PEBS record array if there is no more reference.
 * In this last case, release a reference on the corresponding debug store.
 */
static void put_pebs_records(int cpu)
{
    struct debug_store *ds;
    struct pebs_record *pebs;

    spin_lock(&pebs_records_spinlock);

    if ( per_cpu(pebs_records_refcnt, cpu) == 0 )
        goto out;

    per_cpu(pebs_records_refcnt, cpu)--;

    if ( per_cpu(pebs_records_refcnt, cpu) > 0 )
        goto out;

    ds = get_debug_store(cpu);
    if ( ds == NULL )
        goto out;

    pebs = (struct pebs_record *) ds->pebs_buffer_base;
    put_debug_store(cpu);

    if ( pebs == NULL )
        goto put;

    ds->pebs_absolute_maximum = 0;
    ds->pebs_buffer_base = 0;
    ds->pebs_index = 0;
    ds->pebs_interrupt_threshold = 0;

    free_xenheap_page(pebs);

 put:
    put_debug_store(cpu);
 out:
    spin_unlock(&pebs_records_spinlock);
}


/*
 * These are the counter and event selector registers used for PEBS.
 * We do not use the PERFCTR0 because it is already used by Xen for its
 * watchdog for Intel CPUs (see arch/x86/nmi.c)
 */
#define DS_CTRRST_PEBS        (0)
#define MSR_PERFCTR_PEBS      MSR_IA32_PERFCTR0
#define MSR_PERFEVTSEL_PEBS   MSR_IA32_PERFEVTSEL0
#define MSR_PEBS_ENABLE_MASK  (0x1)

static cpumask_t msr_pebs_usemap;         /* allocated PEBS counters per cpu */

/*
 * Allocate the PEBS capable MSR for the specified CPU.
 * Return 1 if the MSR has been successfully allocated, 0 therwise.
 */
static int alloc_msr_pebs(int cpu)
{
    if ( cpumask_test_cpu(cpu, &msr_pebs_usemap) )
        return 0;
    cpumask_set_cpu(cpu, &msr_pebs_usemap);
    return 1;
}

/*
 * Free the PEBS capable MSR for the specified CPU.
 */
static void free_msr_pebs(int cpu)
{
    cpumask_clear_cpu(cpu, &msr_pebs_usemap);
}


#define MSR_PERFEVT_INV          (1UL << 23)
#define MSR_PERFEVT_EN           (1UL << 22)
#define MSR_PERFEVT_INT          (1UL << 20)
#define MSR_PERFEVT_PC           (1UL << 19)
#define MSR_PERFEVT_E            (1UL << 18)
#define MSR_PERFEVT_OS           (1UL << 17)
#define MSR_PERFEVT_USR          (1UL << 16)
#define MSR_PERFEVT_UMASK        (0xff00)
#define MSR_PERFEVT_EVENT        (0xff)



/*
 * The PEBS user defined handler for each CPU.
 */
DEFINE_PER_CPU(pebs_handler_t, pebs_handler);

int nmi_pebs(int cpu)
{
    u64 pebs_enable, global_status, ds_area, addr;
    struct debug_store *local_store;
    pebs_handler_t handler;

    /* Start by disabling PEBS while handling the interrupt */
    rdmsr_safe(MSR_IA32_PEBS_ENABLE, pebs_enable);
    wrmsr_safe(MSR_IA32_PEBS_ENABLE, pebs_enable & ~MSR_PEBS_ENABLE_MASK);

    /* Now check the PERF_GLOBAL_STATUS to see if PEBS set an overflow */
    rdmsr_safe(MSR_CORE_PERF_GLOBAL_STATUS, global_status);
    if ( !(global_status & MSR_CORE_PERF_GLOBAL_OVFBUFFR) )
        return 0;

    /* This is a PEBS NMI, so obtain debug store and record array addresses */
    rdmsr_safe(MSR_IA32_DS_AREA, ds_area);
    local_store = (struct debug_store *) ds_area;

    /* If there is a handler, then apply it to every records */
    handler = per_cpu(pebs_handler, cpu);
    if ( handler != NULL )
        for (addr = local_store->pebs_buffer_base;
             addr < local_store->pebs_index;
             addr += pebs_record_size)
            handler((struct pebs_record *) addr, cpu);

    /* Once the data has been processed, reset the index to the start */
    local_store->pebs_index = local_store->pebs_buffer_base;

    /* To allow PEBS to produce further interrupt, clear the overflow bit */
    wrmsr_safe(MSR_CORE_PERF_GLOBAL_OVF_CTRL, MSR_CORE_PERF_GLOBAL_OVFBUFFR);

    /* Also notify the APIC the interrupt has been handled */
    apic_write(APIC_LVTPC, APIC_DM_NMI);

    /* Finally re-enable PEBS */
    wrmsr_safe(MSR_IA32_PEBS_ENABLE, pebs_enable | MSR_PEBS_ENABLE_MASK);
    return 1;
}



/*
 * A boolean indicating if the PEBS facility initialization has already been
 * called.
 */
DEFINE_PER_CPU(int, pebs_facility_initialized);

/*
 * Initialize the PEBS components on the specified CPU.
 * This function must be called before any other PEBS related functions.
 * This function assume a total control of the Performance Counter register of
 * the APIC Local Vector Table.
 * Return 0 if the facility has been initialized.
 */
static int init_pebs_facility(int cpu)
{
    if ( per_cpu(pebs_facility_initialized, cpu) )
        return 0;

    if ( pebs_record_size == 0 )
        pebs_record_size = PEBS_SIZE_HSW;

    apic_write_cpu(APIC_LVTPC, APIC_DM_NMI, cpu);

    per_cpu(pebs_facility_initialized, cpu) = 1;
    return 0;
}



int pebs_control_init(struct pebs_control *this, int cpu)
{
    if ( init_pebs_facility(cpu) )
        goto err_init;

    if ( !alloc_msr_pebs(cpu) )
        goto err_init;

    this->debug_store = get_debug_store(cpu);
    if ( this->debug_store == NULL )
        goto err_ds;

    this->pebs_records = get_pebs_records(cpu);
    if ( this->pebs_records == NULL )
        goto err_pebs;

    this->enabled = 0;
    this->cpu = cpu;

    return 0;
 err_pebs:
    put_debug_store(cpu);
 err_ds:
    free_msr_pebs(cpu);
 err_init:
    return -1;
}

int pebs_control_deinit(struct pebs_control *this)
{
    if ( this->enabled )
        pebs_control_disable(this);

    free_msr_pebs(this->cpu);
    put_debug_store(this->cpu);
    put_pebs_records(this->cpu);

    return 0;
}

int pebs_control_setevent(struct pebs_control *this, unsigned long event)
{
    event &= MSR_PERFEVT_EVENT | MSR_PERFEVT_UMASK;

    event |= MSR_PERFEVT_USR;
    event |= MSR_PERFEVT_OS;

    wrmsr_cpu(MSR_PERFEVTSEL_PEBS, event, this->cpu);

    return 0;
}

int pebs_control_setrate(struct pebs_control *this, unsigned long rate)
{
    this->debug_store->pebs_counter_reset[DS_CTRRST_PEBS] = (u64) -rate;
    wrmsr_cpu(MSR_PERFCTR_PEBS, -rate, this->cpu);
    return 0;
}

int pebs_control_sethandler(struct pebs_control *this, pebs_handler_t new)
{
    per_cpu(pebs_handler, this->cpu) = new;
    return 0;
}

int pebs_control_enable(struct pebs_control *this)
{
    u64 val;

    if ( this->enabled )
        return -1;

    rdmsr_cpu(MSR_PERFEVTSEL_PEBS, &val, this->cpu);
    wrmsr_cpu(MSR_PERFEVTSEL_PEBS, val | MSR_PERFEVT_EN, this->cpu);

    rdmsr_cpu(MSR_IA32_PEBS_ENABLE, &val, this->cpu);
    wrmsr_cpu(MSR_IA32_PEBS_ENABLE, val | MSR_PEBS_ENABLE_MASK, this->cpu);

    this->enabled = 1;
    return 0;
}

int pebs_control_disable(struct pebs_control *this)
{
    u64 val;

    if ( !this->enabled )
        return -1;

    rdmsr_cpu(MSR_IA32_PEBS_ENABLE, &val, this->cpu);
    wrmsr_cpu(MSR_IA32_PEBS_ENABLE, val & ~MSR_PEBS_ENABLE_MASK, this->cpu);

    rdmsr_cpu(MSR_PERFEVTSEL_PEBS, &val, this->cpu);
    wrmsr_cpu(MSR_PERFEVTSEL_PEBS, val & ~MSR_PERFEVT_EN, this->cpu);

    wrmsr_cpu(MSR_PERFCTR_PEBS, 0, this->cpu);

    this->enabled = 0;
    return 0;
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
