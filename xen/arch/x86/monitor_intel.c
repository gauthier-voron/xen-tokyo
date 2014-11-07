#include <asm/apic.h>
#include <asm/monitor.h>
#include <asm/msr.h>
#include <asm/p2m.h>
#include <xen/config.h>
#include <xen/lib.h>
#include <xen/mm.h>
#include <xen/percpu.h>
#include <xen/time.h>
#include <xen/types.h>

#define PEBS_SIZE_CORE   144        /* PEBS size for Intel Core & Intel Atom */
#define PEBS_SIZE_NHM    176  /* PEBS size for Nehalem, Sandy and Ivy Bridge */
#define PEBS_SIZE_HSW    192                        /* PEBS size for Haswell */

#define PEBS_RECORD_MAX  8
#define PEBS_RECORD_THR  1


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


static size_t              pebs_size = 0;
static struct debug_store *debug_store = NULL;
static struct pebs_record *pebs_records = NULL;

void debug_store_infos(void)
{
    printk("pebs_index = 0x%lx\n", debug_store->pebs_index);
    printk("relative size = %lu bytes\n", debug_store->pebs_index
           - debug_store->pebs_buffer_base);
    printk("relative size = %lu records\n",
           (debug_store->pebs_index - debug_store->pebs_buffer_base)
           / pebs_size);
}

void debug_store_pouet(void)
{
    debug_store->pebs_index = debug_store->pebs_buffer_base;
}

void pebs_record_infos(void)
{
    printk("rflags = 0x%lx\n", pebs_records[0].flags);
    printk("rip    = 0x%lx\n", pebs_records[0].ip);
    printk("rax    = 0x%lx\n", pebs_records[0].ax);
    printk("rbx    = 0x%lx\n", pebs_records[0].bx);
    printk("rcx    = 0x%lx\n", pebs_records[0].cx);
    printk("rdx    = 0x%lx\n", pebs_records[0].dx);
    printk("rsi    = 0x%lx\n", pebs_records[0].si);
    printk("rdi    = 0x%lx\n", pebs_records[0].di);
    printk("r8     = 0x%lx\n", pebs_records[0].r8);
    printk("r9     = 0x%lx\n", pebs_records[0].r9);
    printk("r10    = 0x%lx\n", pebs_records[0].r10);
    printk("r11    = 0x%lx\n", pebs_records[0].r11);
    printk("r12    = 0x%lx\n", pebs_records[0].r12);
    printk("r13    = 0x%lx\n", pebs_records[0].r13);
    printk("r14    = 0x%lx\n", pebs_records[0].r14);
    printk("r15    = 0x%lx\n", pebs_records[0].r15);
    printk("dla    = 0x%lx\n", pebs_records[0].data_linear_address);
    printk("dse    = 0x%lx\n", pebs_records[0].data_source_encoding);
}



int BIGOS_DEBUG_0 = 0;

static int init_debug_store(void)
{
    pebs_size = PEBS_SIZE_HSW; /* arbitrary for testing */
    debug_store = alloc_xenheap_pages(PAGE_ORDER_4K, 0);
    pebs_records = alloc_xenheap_pages(PAGE_ORDER_4K, 0);

    memset(debug_store, 0, PAGE_SIZE);
    memset(pebs_records, 0, PAGE_SIZE);
    
    debug_store->pebs_buffer_base = (u64) pebs_records;
    debug_store->pebs_index = (u64) pebs_records;
    debug_store->pebs_absolute_maximum = (u64) pebs_records
        + PEBS_RECORD_MAX * pebs_size;
    debug_store->pebs_interrupt_threshold = (u64) pebs_records
        + PEBS_RECORD_THR * pebs_size;

    return wrmsr_safe(MSR_IA32_DS_AREA, (u64) debug_store);
}

static int init_pebs(void)
{
    int ret;

    ret = wrmsr_safe(MSR_P6_PERFCTR0, -0x80000);
    if (ret)
        return ret;
    debug_store->pebs_counter_reset[0] = -0x80000;

    /* ret = wrmsr_safe(MSR_P6_EVNTSEL0, 0x4300c0); */
    ret = wrmsr_safe(MSR_P6_EVNTSEL0, 0x4381d0);
    return ret;
}

static int start_pebs(void)
{
    int ret;

    ret = wrmsr_safe(MSR_IA32_PEBS_ENABLE, 0x1);

    return ret;
}


void monitor_intel(void)
{
    int ret;
    u64 val;
    
    printk("init debug store\n");
    ret = init_debug_store();
    printk("ret value = %d\n", ret);
    rdmsr_safe(MSR_IA32_DS_AREA, val);
    printk("DS_AREA = 0x%lx\n", val);

    BIGOS_DEBUG_0 = 1;

    printk("\n");
    printk("apic\n");
    printk("apic read = %x\n", apic_read(APIC_LVTPC));
    printk("apic write : %x\n", APIC_DM_NMI);
    apic_write(APIC_LVTPC, SET_APIC_DELIVERY_MODE(0, APIC_MODE_NMI));
    printk("apic read = %x\n", apic_read(APIC_LVTPC));
    
    printk("\n");
    printk("ds index = %lu\n", debug_store->pebs_index - debug_store->pebs_buffer_base);

    printk("init pebs\n");
    ret = init_pebs();
    printk("ret value = %d\n", ret);
    rdmsr_safe(MSR_P6_EVNTSEL0, val);
    printk("EVNTSEL0 = 0x%lx\n", val);
    rdmsr_safe(MSR_P6_PERFCTR0, val);
    printk("PERFCTR0 = 0x%lx\n", val);

    printk("\n");
    printk("ds index = %lu\n", debug_store->pebs_index - debug_store->pebs_buffer_base);

    printk("start pebs\n");
    ret = start_pebs();
    printk("ret value = %d\n", ret);
    rdmsr_safe(MSR_IA32_PEBS_ENABLE, val);
    printk("PEBS_ENABLE = 0x%lx\n", val);

    printk("\n");
    printk("ds index = %lu\n", debug_store->pebs_index - debug_store->pebs_buffer_base);

    rdmsr_safe(MSR_P6_PERFCTR0, val);
    printk("PERFCTR0 = 0x%lx\n", val);
    rdmsr_safe(MSR_CORE_PERF_GLOBAL_STATUS, val);
    printk("PERF_GLOBAL_STATUS = 0x%lx\n", val);
    rdmsr_safe(MSR_CORE_PERF_GLOBAL_CTRL, val);
    printk("PERF_GLOBAL_CTRL = 0x%lx\n", val);

    /* for (i=0; i<256; i++) { */
    /*     printk("%d ", i); */
    /* } */
    /* printk("\n"); */

    /* rdmsr_safe(MSR_P6_PERFCTR0, val); */
    /* printk("PERFCTR0 = 0x%lx\n", val); */
    /* rdmsr_safe(MSR_CORE_PERF_GLOBAL_STATUS, val); */
    /* printk("PERF_GLOBAL_STATUS = 0x%lx\n", val); */
    /* rdmsr_safe(MSR_CORE_PERF_GLOBAL_CTRL, val); */
    /* printk("PERF_GLOBAL_CTRL = 0x%lx\n", val); */

    asm volatile ("xor %%rax, %%rax\n"
                  "mov %0, %%rbx\n"
                  "start_loop:\n"
                  "cmp $0x80000, %%rax\n"
                  "jge end_loop\n"
                  "mov 0x0(%%rbx), %%rcx\n"
                  "inc %%rax\n"
                  "jmp start_loop\n"
                  "end_loop:\n"
                  : : "m" (pebs_records));
    
    rdmsr_safe(MSR_IA32_PEBS_ENABLE, val);
    printk("PEBS_ENABLE = 0x%lx\n", val);
    val &= ~(1UL);
    wrmsr_safe(MSR_IA32_PEBS_ENABLE, val);
    rdmsr_safe(MSR_IA32_PEBS_ENABLE, val);
    printk("PEBS_ENABLE = 0x%lx\n", val);

    rdmsr_safe(MSR_P6_PERFCTR0, val);
    printk("PERFCTR0 = 0x%lx\n", val);
    rdmsr_safe(MSR_CORE_PERF_GLOBAL_STATUS, val);
    printk("PERF_GLOBAL_STATUS = 0x%lx\n", val);
    rdmsr_safe(MSR_CORE_PERF_GLOBAL_CTRL, val);
    printk("PERF_GLOBAL_CTRL = 0x%lx\n", val);
    printk("\n");
}

int BIGOS_DEBUG_1 = 0;
void demonitor(void)
{
    struct domain *d;
    struct page_info *page;
    unsigned long mfn;
    p2m_type_t p2mt;

    d = current->domain;
    
    page = virt_to_page(pebs_records);
    mfn = page_to_mfn(page);
    printk("pebs_records virt = %p\n", pebs_records);
    printk("pebs_records mfn  = %lx\n", mfn);
    BIGOS_DEBUG_1 = 1;
    mfn = mfn_x(get_gfn_query(d, (unsigned long) pebs_records, &p2mt));
    BIGOS_DEBUG_1 = 1;
    printk("pebs_records mfn  = %lx\n", mfn);
    printk("memory type = %x\n", p2mt);
}










static void __rdmsr_cpu(void *args)
{
    u64 *ret = (u64 *) args;
    u64 *msr = ret + 1;
    u64 *val = msr + 1;

    *ret = rdmsr_safe(*msr, *val);
}

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

#define PEBS_INST                (0x00c1)
#define  PEBS_INST_PDIST         (0x0100)
#define PEBS_UOPS                (0x00c2)
#define  PEBS_UOPS_ALL           (0x0100)
#define  PEBS_UOPS_SLOT          (0x0200)
#define PEBS_BRINST              (0x00c4)
#define  PEBS_BRINST_COND        (0x0100)
#define  PEBS_BRINST_NEARCL      (0x0200)
#define  PEBS_BRINST_ALL         (0x0400)
#define  PEBS_BRINST_NEARR       (0x0800)
#define  PEBS_BRINST_NEART       (0x2000)
#define PEBS_BRMISP              (0x00c5)
#define  PEBS_BRMISP_COND        (0x0100)
#define  PEBS_BRMISP_NEARCL      (0x0200)
#define  PEBS_BRMISP_ALL         (0x0400)
#define  PEBS_BRMISP_NOTTK       (0x1000)
#define  PEBS_BRMISP_TAKEN       (0x2000)
#define PEBS_MUOPS               (0x00d0)
#define  PEBS_MUOPS_TLBMSLD      (0x1100)
#define  PEBS_MUOPS_TLBMSST      (0x1200)
#define  PEBS_MUOPS_LCKLD        (0x2100)
#define  PEBS_MUOPS_SPLLD        (0x4100)
#define  PEBS_MUOPS_SPLST        (0x4200)
#define  PEBS_MUOPS_ALLLD        (0x8100)
#define  PEBS_MUOPS_ALLST        (0x8200)
#define PEBS_MLUOPS              (0x00d1)
#define  PEBS_MLUOPS_LIHIT       (0x0100)
#define  PEBS_MLUOPS_L2HIT       (0x0200)
#define  PEBS_MLUOPS_L3HIT       (0x0300)
#define  PEBS_MLUOPS_HITLFB      (0x4000)



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



/*
 * Initialize a PEBS control unit for the given cpus.
 * The total amount of PEBS control unit is hardware limited so this function
 * can fail if there is no more free resources on the specified cpus.
 * Return 0 in case of success.
 */
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
    this->handler = NULL;
    this->cpu = cpu;

    return 0;
 err_pebs:
    put_debug_store(cpu);
 err_ds:
    free_msr_pebs(cpu);
 err_init:
    return -1;
}

/*
 * Finalize a PEBS control unit.
 * Free the hardware resources of the given control unit on the according cpus.
 * Return 0 in case of success.
 */
int pebs_control_deinit(struct pebs_control *this)
{
    if ( this->enabled )
        pebs_control_disable(this);
    
    free_msr_pebs(this->cpu);
    put_debug_store(this->cpu);
    put_pebs_records(this->cpu);
    
    return 0;
}


/*
 * Set the event the PEBS control unit samples.
 * The list of events can be found in the Intel IA32 Developer's Manual.
 * Be sure to specify an event defined for the current model of cpu.
 * Return 0 in case of success.
 */
int pebs_control_setevent(struct pebs_control *this, unsigned long event)
{
    event &= MSR_PERFEVT_EVENT | MSR_PERFEVT_UMASK;

    event |= MSR_PERFEVT_USR;
    event |= MSR_PERFEVT_OS;
    
    wrmsr_cpu(MSR_PERFEVTSEL_PEBS, event, this->cpu);

    return 0;
}

/*
 * Set the sample rate for the PEBS control unit.
 * The rate is the count of event triggering the sampled event to ignore before
 * to tag an event to actually trigger the handler.
 * More the rate is small, more often an interrupt will be triggered.
 * Return 0 in case of success.
 */
int pebs_control_setrate(struct pebs_control *this, unsigned long rate)
{
    this->debug_store->pebs_counter_reset[DS_CTRRST_PEBS] = (u64) -rate;
    wrmsr_cpu(MSR_PERFCTR_PEBS, -rate, this->cpu);
    return 0;
}

/*
 * Set the handler to call when the interrupt is triggered.
 * If the old parameter is not NULL, it is filled with the previous handler.
 * You can specify the new parameter as NULL so no handler will be called.
 * Return 0 in case of success.
 */
int pebs_control_sethandler(struct pebs_control *this, pebs_handler_t new)
{
    per_cpu(pebs_handler, this->cpu) = new;
    return 0;
}


/*
 * Enable the PEBS control unit so it start the sampling.
 * Return 0 in case of success.
 */
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

/*
 * Disable the PEBS control unit so it does not sample anymore.
 * Return 0 in case of success.
 */
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





struct pebs_control pebsctr[NR_CPUS];

static void handler(struct pebs_record *record, int cpu)
{
    printk("CPU[%d] <= %lx at %lu\n", cpu, record->data_linear_address,
           NOW());
}

void test_setup(void)
{
    int cpu;
    
    for_each_cpu(cpu, &cpu_online_map)
    {
        pebs_control_init(&pebsctr[cpu], cpu);
        pebs_control_setevent(&pebsctr[cpu], PEBS_MUOPS | PEBS_MUOPS_ALLLD);
        pebs_control_setrate(&pebsctr[cpu], 0x10000);
        pebs_control_sethandler(&pebsctr[cpu], handler);
        pebs_control_enable(&pebsctr[cpu]);
    }
}

void test_teardown(void)
{
    int cpu;
    
    for_each_cpu(cpu, &cpu_online_map)
    {
        pebs_control_deinit(&pebsctr[cpu]);
    }
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
