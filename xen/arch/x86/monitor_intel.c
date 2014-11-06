#include <asm/apic.h>
#include <asm/monitor.h>
#include <asm/msr.h>
#include <asm/p2m.h>
#include <xen/config.h>
#include <xen/lib.h>
#include <xen/mm.h>
#include <xen/percpu.h>
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
    u64 pebs_counter_reset;             /* always use PMC0 for PEBS sampling */
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
    debug_store->pebs_counter_reset = -0x80000;

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











DEFINE_PER_CPU(struct debug_store *, debug_store);



/* static void __wrmsr_cpu(void *args) */
/* { */
/*     u64 *ret = (u64 *) args; */
/*     u64 *expect = ret + 1; */
/*     u64 *new = expect + 1; */
/*     int err; */

/*     err = rdmsr_safe(MSR_AI32_DS_AREA, (u64 */
/* } */

static void __rpmsr_cpu(void *arg)
{
    u64 *ret = (u64 *) arg;
    u64 *msr = ret + 1;
    u64 *exp = msr + 1;
    u64 *new = exp + 1;
    int err;

    *ret = *new + 1;

    err = rdmsr_safe(*msr, *ret);
    if ( err && *ret != *exp )
        return;

    err = wrmsr_safe(*msr, *new);
    if ( err )
        return;

    *ret = *new;
}

u64 rpmsr_cpu(u64 msr, u64 exp, u64 new, int cpu)
{
    cpumask_t cpumask;
    u64 args[] = { 0, msr, exp, new };

    cpumask_clear(&cpumask);
    cpumask_set_cpu(cpu, &cpumask);

    on_selected_cpus(&cpumask, __rpmsr_cpu, args, 1);
    return args[0];
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

    on_selected_cpus(&cpumask, __rdmsr_cpu, args, 1);
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

    on_selected_cpus(&cpumask, __wrmsr_cpu, args, 1);
    return (int) args[0];
}



/*
 * Stub for installing and uninstalling debug store.
 * The argument is the address of the debug store pointer.
 * In case of success, the debug store pointer is left unchanged, otherwise,
 * it is modified.
 */
/* static void __set_ds_area(void *val) */
/* { */
/*     struct debug_store **debug_store = (struct debug_store **) val; */
/*     int ret; */

/*     ret = wrmsr_safe(MSR_IA32_DS_AREA, (u64) *debug_store); */
/*     if ( ret ) */
/*         *debug_store = *debug_store + 1; */
/* } */

/*
 * Install the given debug store for the specified CPU.
 * Return 0 in case of success, -1 otherwise.
 */
/* static int install_debug_store(int cpu) */
/* { */
/* /\*     cpumask_t cpumask; *\/ */
/* /\*     struct debug_store *err = per_cpu(debug_store, cpu); *\/ */

/* /\*     if ( per_cpu(debug_store, cpu) == NULL ) *\/ */
/* /\*         return -1; *\/ */
    
/* /\*     cpumask_clear(&cpumask); *\/ */
/* /\*     cpumask_set_cpu(cpu, &cpumask); *\/ */

/* /\*     on_selected_cpus(&cpumask, __set_ds_area, &err, 1); *\/ */
/* /\*     if ( err != per_cpu(debug_store, cpu) ) *\/ */
/* /\*         goto err; *\/ */
        
/* /\*     return 0; *\/ */
/* /\* err: *\/ */
/* /\*     return -1; *\/ */
/*     u64 ret; */

/*     ret = rpmsr_cpu(MSR_IA32_DS_AREA, 0, (u64) per_cpu(debug_store, cpu), cpu); */
/*     if ( ret == (u64) per_cpu(debug_store, cpu) ) */
/*         return 0; */
/*     return -1; */
/* } */



/* /\* */
/*  * Uninstall the given debug store for the specified CPU. */
/*  * Return 0 in case of success, -1 otherwise. */
/*  *\/ */
/* static int uninstall_debug_store(int cpu) */
/* { */
/*     /\* cpumask_t cpumask; *\/ */
/*     /\* struct debug_store *err = NULL; *\/ */
    
/*     /\* cpumask_clear(&cpumask); *\/ */
/*     /\* cpumask_set_cpu(cpu, &cpumask); *\/ */

/*     /\* on_selected_cpus(&cpumask, __set_ds_area, &err, 1); *\/ */
/*     /\* return (err == NULL) ? 0 : -1; *\/ */
/*     return wrmsr_cpu(MSR_IA32_DS_AREA, 0, cpu); */
/* } */




/*
 * Allocate a debug store on the specified CPU.
 * The function will fail if no more memory or if there already is a debug
 * store on the spcified CPU.
 * Return 0 in case of success.
 */
static int alloc_debug_store(int cpu)
{
    struct debug_store *ds;
    int err = -1;
    u64 ret;

    ds = alloc_xenheap_page();
    if ( ds == NULL )
        goto out;
    memset(ds, 0, PAGE_SIZE);
    
    ret = rpmsr_cpu(MSR_IA32_DS_AREA, 0, (u64) ds, cpu);
    if ( ret != (u64) ds )
        goto free;
    
    err = 0;
 out:
    return err;
 free:
    free_xenheap_page(ds);
    goto out;    
}

/*
 * Free the debug store of the specified CPU.
 * This function can fail in case of race condition.
 * Return 0 in case of success.
 */
static int free_debug_store(int cpu)
{
    struct debug_store *ds;
    int err = 0;
    u64 ret;

    err = rdmsr_cpu(MSR_IA32_DS_AREA, (u64 *) &ds, cpu);
    if ( err || ds == NULL )
        goto out;

    ret = rpmsr_cpu(MSR_IA32_DS_AREA, (u64) ds, 0, cpu);
    if ( ret != 0 )
        goto err;

    if ( ds->bts_buffer_base != 0 )
        printk("%s:%d: possible BTS memory leak\n", __FILE__, __LINE__);
    if ( ds->pebs_buffer_base != 0 )
        printk("%s:%d: possible PEBS memory leak\n", __FILE__, __LINE__);

    free_xenheap_page(ds);

 out:
    return err;
 err:
    err = -1;
    goto out;
}

/*
 * Return the debug store for the current CPU, or NULL if no debug store has
 * been allocated.
 */
static struct debug_store *get_debug_store(int cpu)
{
    struct debug_store *ds;
    int err;

    err = rdmsr_cpu(MSR_IA32_DS_AREA, (u64 *) &ds, cpu);
    if ( err )
        return NULL;
    return ds;
}





/* Only compatible with Haswell architecture for now */
static unsigned long pebs_record_size = PEBS_SIZE_HSW;

/*
 * Allocate a PEBS record array for the specified CPU.
 * This function will fail if there is no allocated debug store or if there
 * already is a record array or if there is no more memory available.
 * Return 0 in case of success.
 */
static int alloc_pebs_records(int cpu)
{
    struct debug_store *ds = get_debug_store(cpu);
    struct pebs_records *pebs;
    int err = 0;
    u64 base;

    if ( ds == NULL )
        goto err;

    if ( ds->pebs_buffer_base != 0 )
        goto err;
    
    pebs = alloc_xenheap_page();
    if ( pebs == NULL )
        goto err;

    base = (u64) pebs;
    ds->pebs_buffer_base = base;
    ds->pebs_index = base;
    ds->pebs_absolute_maximum = base + PAGE_SIZE;
    ds->pebs_interrupt_threshold = base + pebs_record_size;
    ds->pebs_counter_reset = 0;
    
 out:
    return err;
 err:
    err = -1;
    goto out;
}

/*
 * Free the PEBS record array for the specified CPU.
 * This function fails, if no debug store is allocated for the specified CPU.
 * Return 0 in case of success.
 */
static int free_pebs_records(int cpu)
{
    struct debug_store *ds = get_debug_store(cpu);
    struct pebs_record *pebs;
    int err = 0;

    if ( ds == NULL )
        goto err;

    pebs = (struct pebs_record *) ds->pebs_buffer_base;
    if ( pebs == NULL )
        goto out;

    ds->pebs_absolute_maximum = 0;
    ds->pebs_buffer_base = 0;
    ds->pebs_index = 0;
    ds->pebs_interrupt_threshold = 0;
    ds->pebs_counter_reset = 0;

    free_xenheap_page(pebs);

 out:
    return err;
 err:
    err = -1;
    goto out;
}

/*
 * Return the PEBS record array for the given CPU, or NULL if no debug store
 * or no PEBS record array is allocated for the given CPU.
 */
static struct pebs_record *get_pebs_records(int cpu)
{
    struct debug_store *ds = get_debug_store(cpu);
    struct pebs_record *pebs = NULL;

    if ( ds == NULL )
        goto out;

    pebs = (struct pebs_record *) ds->pebs_buffer_base;
 out:
    return pebs;
}







/*
 * Allocate a pebs record array for a given cpu.
 * This function will actually allocate space only if there is no record array
 * allocated for the given cpu.
 * Return 0 if there is a record array allocated for the specified cpu.
 */
/* static int alloc_pebs_records(int cpu) */
/* { */
/*     if ( per_cpu(pebs_records, cpu) != NULL ) */
/*         goto out; */

/*     per_cpu(pebs_records, cpu) = alloc_xenheap_page(); */
/*     if ( per_cpu(pebs_records, cpu) == NULL ) */
/*         goto out; */

/*  out: */
/*     return (per_cpu(pebs_records, cpu) != NULL) ? 0 : -1; */
/* } */

/*
 * Free the allocated pebs record array for the specified cpu.
 * Return 0 if there is no more record array allocated for the specified cpu.
 */
/* static int free_pebs_records(int cpu) */
/* { */
/*     if ( per_cpu(pebs_records, cpu) == NULL ) */
/*         goto out; */

/*     free_xenheap_page(per_cpu(pebs_records, cpu)); */
/*     per_cpu(pebs_records, cpu) = NULL; */

/*  out: */
/*     return 0; */
/* } */







/* DEFINE_PER_CPU(unsigned long, debug_store_refcnt); */


/* static struct debug_store *get_debug_store(void) */
/* { */
/*     struct debug_store *debug_store = NULL; */
/*     u64 ds_area; */
/*     int ret; */

/*     ret = rdmsr_safe(MSR_IA32_DS_AREA, ds_area); */
/*     if ( ret ) */
/*         goto err; */

/*     printk("ok\n"); */
    
/*     debug_store = (struct debug_store *) ds_area; */
/*     if ( debug_store != NULL ) */
/*         goto out; */

/*     printk("ok\n"); */
    
/*     debug_store = alloc_xenheap_page(); */

/*     printk("alloc_xenheap_page() = %p\n", debug_store); */
/*     return NULL; */

/*     if ( debug_store == NULL ) */
/*         goto err; */

/*     printk("ok\n"); */
/*     return NULL; */

/*     this_cpu(debug_store_refcnt) = 0; */
/*     memset(debug_store, 0, PAGE_SIZE); */

/*     ret = wrmsr_safe(MSR_IA32_DS_AREA, (u64) debug_store); */
/*     if ( ret ) */
/*     { */
/*         free_xenheap_page(debug_store); */
/*         debug_store = NULL; */
/*         goto err; */
/*     } */
    
/*  out: */
/*     this_cpu(debug_store_refcnt)++; */
/*  err: */
/*     return debug_store; */
/* } */

/* static void put_debug_store(void) */
/* { */
/*     struct debug_store *debug_store; */
/*     u64 ds_area; */
/*     int ret; */

/*     if ( this_cpu(debug_store_refcnt) == 0 ) */
/*     { */
/*         printk("%s:%d: fantom reference\n", __FILE__, __LINE__); */
/*         return; */
/*     } */

/*     this_cpu(debug_store_refcnt)--; */

/*     if ( this_cpu(debug_store_refcnt) > 0 ) */
/*         return; */

/*     ret = rdmsr_safe(MSR_IA32_DS_AREA, ds_area); */
/*     if ( ret ) */
/*     { */
/*         printk("%s:%d: MSR_IA32_DS_AREA broken\n", __FILE__, __LINE__); */
/*         return; */
/*     } */

/*     debug_store = (struct debug_store *) ds_area; */
/*     printk("__debug_store = %p\n", debug_store); */
/*     if ( debug_store == NULL ) */
/*     { */
/*         printk("%s:%d: MSR_IA32_DS_AREA overwritten\n", __FILE__, __LINE__); */
/*         return; */
/*     }; */

/*     ret = wrmsr_safe(MSR_IA32_DS_AREA, 0); */
/*     if ( ret ) */
/*     { */
/*         printk("%s:%d: MSR_IA32_DS_AREA broken\n", __FILE__, __LINE__); */
/*         return; */
/*     }; */
    
/*     if ( debug_store->bts_buffer_base ) */
/*         printk("%s:%d: possible BTS memory leak\n", __FILE__, __LINE__); */
/*     if ( debug_store->pebs_buffer_base ) */
/*         printk("%s:%d: possible PEBS memory leak\n", __FILE__, __LINE__); */

/*     free_xenheap_page(debug_store); */
/* } */


/* static unsigned long pebs_record_size = 0; */

/* static int init_pebs_record_size(void) */
/* { */
/*     pebs_record_size = PEBS_SIZE_HSW; */
/*     return 0; */
/* } */

/* DEFINE_PER_CPU(unsigned long, pebs_records_refcnt); */

/* static struct pebs_records *get_pebs_records(void) */
/* { */
/*     struct pebs_records *pebs_records = NULL; */
/*     struct debug_store *debug_store; */
/*     u64 base; */

/*     if ( !pebs_record_size && init_pebs_record_size() ) */
/*         goto err; */

/*     debug_store = get_debug_store();            /\* this ref will be released *\/ */
/*     if ( debug_store == NULL ) */
/*         goto err; */

/*     pebs_records = (struct pebs_records *) debug_store->pebs_buffer_base; */
/*     if ( pebs_records != NULL ) */
/*         goto out; */

/*     pebs_records = alloc_xenheap_page(); */
/*     if ( pebs_records == NULL ) */
/*         goto err_ds; */

/*     this_cpu(pebs_records_refcnt) = 0; */

/*     /\* */
/*      * This reference is taken when allocating a new PEBS record buffer and */
/*      * will be released when the PEBS record buffer is released too (in */
/*      * put_pebs_records). */
/*      *\/ */
/*     get_debug_store(); */
    
/*     base = (u64) pebs_records; */
/*     debug_store->pebs_buffer_base = base; */
/*     debug_store->pebs_index = base; */
/*     debug_store->pebs_absolute_maximum = base + PAGE_SIZE; */
/*     debug_store->pebs_interrupt_threshold = base + pebs_record_size; */
    
/*  out: */
/*     this_cpu(pebs_records_refcnt)++; */
/*  err_ds: */
/*     put_debug_store();                 /\* always release the first taken ref *\/ */
/*  err: */
/*     return pebs_records; */
/* } */

/* static __attribute__((unused)) void put_pebs_records(void) */
/* { */
/*     struct debug_store *debug_store; */
/*     struct pebs_record *pebs_records; */

/*     if ( this_cpu(pebs_records_refcnt) == 0 ) */
/*     { */
/*         printk("%s:%d: fantom reference\n", __FILE__, __LINE__); */
/*         return; */
/*     }; */
    
/*     this_cpu(pebs_records_refcnt)--; */

/*     if ( this_cpu(pebs_records_refcnt) > 0 ) */
/*         return; */

/*     debug_store = get_debug_store(); */
/*     if ( debug_store == NULL ) */
/*     { */
/*         printk("%s:%d: debug store overwritten\n", __FILE__, __LINE__); */
/*         return; */
/*     }; */

/*     pebs_records = (struct pebs_record *) debug_store->pebs_buffer_base; */
/*     if ( pebs_records == NULL ) */
/*     { */
/*         printk("%s:%d: PEBS buffer base overwritten\n", __FILE__, __LINE__); */
/*         return; */
/*     }; */

/*     debug_store->pebs_absolute_maximum = 0; */
/*     debug_store->pebs_buffer_base = 0; */
/*     debug_store->pebs_index = 0; */
/*     debug_store->pebs_interrupt_threshold = 0; */
/*     debug_store->pebs_counter_reset = 0; */

/*     free_xenheap_page(pebs_records); */
/*     put_debug_store(); */
/* } */


/* /\* static void __get_all_pebs_records(void *cpu_ret) *\/ */
/* /\* { *\/ */
/* /\*     struct pebs_records *ret; *\/ */
/* /\*     int *__cpu_ret = (int *) cpu_ret; *\/ */

/* /\*     printk("getting pebs on cpu %d\n", get_processor_id()); *\/ */
    
/* /\*     ret = get_pebs_records(); *\/ */
/* /\*     __cpu_ret[get_processor_id()] = (ret != NULL) ? 0 : -1; *\/ */
/* /\* } *\/ */

/* /\* static void __put_all_pebs_records(void *cpu_ret) *\/ */
/* /\* { *\/ */
/* /\*     int *__cpu_ret = (int *) cpu_ret; *\/ */
    
/* /\*     if ( !__cpu_ret || __cpu_ret[get_processor_id()] ) *\/ */
/* /\*     { *\/ */
/* /\*         printk("putting pebs on cpu %d\n", get_processor_id()); *\/ */
/* /\*         /\\* put_pebs_records(); *\\/ *\/ */
/* /\*     } *\/ */
/* /\* } *\/ */

/* /\* static int get_all_pebs_records(void) *\/ */
/* /\* { *\/ */
/* /\*     int cpu, ret = 0; *\/ */
/* /\*     int cpu_ret[NR_CPUS] = { 0 }; *\/ */
    
/* /\*     on_each_cpu(__get_all_pebs_records, cpu_ret, 1); *\/ */
/* /\*     for_each_cpu(cpu, &cpu_online_map) *\/ */
/* /\*     { *\/ */
/* /\*         ret = cpu_ret[cpu]; *\/ */
/* /\*         if ( ret ) *\/ */
/* /\*         { *\/ */
/* /\*             on_each_cpu(__put_all_pebs_records, cpu_ret, 1); *\/ */
/* /\*             goto out; *\/ */
/* /\*         } *\/ */
/* /\*     } *\/ */

/* /\*  out: *\/ */
/* /\*     return ret; *\/ */
/* /\* } *\/ */

/* /\* static void put_all_pebs_records(void) *\/ */
/* /\* { *\/ */
/* /\*     on_each_cpu(__put_all_pebs_records, NULL, 1); *\/ */
/* /\* } *\/ */





#define IA32_PERFCTR_START       0x0c1    /* First MSR address of PERFCTR */
#define IA32_PERFEVTSEL_START    0x186    /* First MSR address of PERFEVTSEL */
#define IA32_PERF_MAX            1        /* Maximum amount of supported PMC */

struct perf_counter
{
    unsigned long perfctr;       /* IA32_PERFCTR MSR address */
    unsigned long perfevtsel;    /* IA32_PERFEVTSEL MSR address */
    cpumask_t     usedmap;       /* on which cpu the PMC is used */
};

unsigned long  supported_pmcs_count = 0;      /* count of actually supported */
struct perf_counter supported_pmcs[IA32_PERF_MAX];         /* supported PMCs */


/*
 * Initialize global data structures about performance monitoring counters.
 * This function need to be called once before any related function can work.
 * For now, we support only an ad-hoc configuration.
 * Return 0 in case of success.
 * The function can fail if using a CPU which is not supported.
 */
static int init_supported_pmcs(void)
{
    supported_pmcs_count = 1;
    
    supported_pmcs[0].perfctr = IA32_PERFCTR_START;
    supported_pmcs[0].perfevtsel = IA32_PERFEVTSEL_START;
    cpumask_clear(&supported_pmcs[0].usedmap);

    return 0;
}


static struct perf_counter *alloc_perf_counter(int cpu)
{
    struct perf_counter *pmc = NULL;
    unsigned long i;
    
    for (i=0; i<supported_pmcs_count; i++)
        if ( !cpumask_test_cpu(cpu, &supported_pmcs[i].usedmap) )
        {
            cpumask_set_cpu(cpu, &supported_pmcs[i].usedmap);
            pmc = &supported_pmcs[i];
            break;
        }

    return pmc;
}

static void free_perf_counter(struct perf_counter *pmc, int cpu)
{
    cpumask_clear_cpu(cpu, &pmc->usedmap);
}





/* /\* */
/*  * Initialize a PEBS control unit for the given cpus. */
/*  * The total amount of PEBS control unit is hardware limited so this function */
/*  * can fail if there is no more free resources on the specified cpus. */
/*  * Return 0 in case of success. */
/*  *\/ */
/* int pebs_control_init(struct pebs_control *this, cpumask_t *cpumask) */
/* { */
/*     struct pebs_records *pebs_records; */
/*     struct pmc *pmc; */

/*     pebs_records = get_pebs_records(); */
/*     if ( pebs_records == NULL ) */
/*         goto err; */

/*     pmc = alloc_pmc(); */
/*     if ( pmc == NULL ) */
/*         goto put; */

/*     this->enabled = 0; */
/*     this->pmc = pmc; */
/*     this->handler = NULL; */
    
/*     return 0; */
/*  put: */
/*     put_pebs_records(); */
/*  err: */
/*     return -1; */
/* } */

/* /\* */
/*  * Finalize a PEBS control unit. */
/*  * Free the hardware resources of the given control unit on the according cpus. */
/*  * Return 0 in case of success. */
/*  *\/ */
/* int pebs_control_deinit(struct pebs_control *this) */
/* { */
/*     if ( this->enabled ) */
/*         pebs_control_disable(this); */
/*     free_pmc(this->pmc); */
/*     put_pebs_records(); */
/*     return 0; */
/* } */


/* /\* */
/*  * Set the event the PEBS control unit samples. */
/*  * The list of events can be found in the Intel IA32 Developer's Manual. */
/*  * Be sure to specify an event defined for the current model of cpu. */
/*  * Return 0 in case of success. */
/*  *\/ */
/* int pebs_control_setevent(struct pebs_control *this, unsigned long event) */
/* { */
/*     if ( this ) {} */
/*     if ( event ) {} */
/*     return 0; */
/* } */

/* /\* */
/*  * Set the sample rate for the PEBS control unit. */
/*  * The rate is the count of event triggering the sampled event to ignore before */
/*  * to tag an event to actually trigger the handler. */
/*  * More the rate is small, more often an interrupt will be triggered. */
/*  * Return 0 in case of success. */
/*  *\/ */
/* int pebs_control_setrate(struct pebs_control *this, unsigned long rate) */
/* { */
/*     if ( this ) {} */
/*     if ( rate ) {} */
/*     return 0; */
/* } */

/* /\* */
/*  * Set the handler to call when the interrupt is triggered. */
/*  * If the old parameter is not NULL, it is filled with the previous handler. */
/*  * You can specify the new parameter as NULL so no handler will be called. */
/*  * Return 0 in case of success. */
/*  *\/ */
/* int pebs_control_sethandler(struct pebs_control *this, pebs_handler_t new, */
/*                             pebs_handler_t *old) */
/* { */
/*     if ( this ) {} */
/*     if ( new ) {} */
/*     if ( old ) {} */
/*     return 0; */
/* } */


/* /\* */
/*  * Enable the PEBS control unit so it start the sampling. */
/*  * Return 0 in case of success. */
/*  *\/ */
/* int pebs_control_enable(struct pebs_control *this) */
/* { */
/*     if ( this ) {} */
/*     return 0; */
/* } */

/* /\* */
/*  * Disable the PEBS control unit so it does not sample anymore. */
/*  * Return 0 in case of success. */
/*  *\/ */
/* int pebs_control_disable(struct pebs_control *this) */
/* { */
/*     if ( this ) {} */
/*     return 0; */
/* } */



/* static struct pebs_control test_ctrl; */
/* static struct debug_store *__debug_store; */
/* static int __cpu; */

/* static void __test_setup(void *v __attribute__((unused))) */
/* { */
/*     /\* int ret; *\/ */

/*     /\* __debug_store = get_debug_store(); *\/ */
/*     /\* printk("__debug_store = %p\n", __debug_store); *\/ */
    
/*     /\* ret = pebs_control_init(&test_ctrl); *\/ */
/*     /\* printk("pebs_control_init() = %d\n", ret); *\/ */
/* } */


static struct perf_counter *pmcs[NR_CPUS];

void test_setup(void)
{
    int ret, cpu;
    u64 test;

    init_supported_pmcs();
    
    rdmsr_safe(MSR_IA32_DS_AREA, test);
    printk("test = %lx\n", test);

    for_each_cpu(cpu, &cpu_online_map)
    {
        ret = alloc_debug_store(cpu);
        printk("alloc_debug_store(%d) = %d\n", cpu, ret);
        printk("  debug_store at %p\n", get_debug_store(cpu));

        ret = alloc_pebs_records(cpu);
        printk("alloc_pebs_records(%d) = %d\n", cpu, ret);
        printk("  pebs_records at %p\n", get_pebs_records(cpu));

        pmcs[cpu] = alloc_perf_counter(cpu);
        printk("alloc_perf_counter(%d) = %p\n", cpu, pmcs[cpu]);
    }

    rdmsr_safe(MSR_IA32_DS_AREA, test);
    printk("test = %lx\n", test);
}

/* on_each_cpu(__test_setup, NULL, 1); */
    /* printk("current = %d\n", smp_processor_id()); */
    /* __test_setup(NULL); */

/* static void __test_teardown(void *v __attribute__((unused))) */
/* { */
/*     /\* int ret; *\/ */

/*     /\* put_debug_store(); *\/ */
    
/*     /\* ret = pebs_control_deinit(&test_ctrl); *\/ */
/*     /\* printk("pebs_control_deinit() = %d\n", ret); *\/ */
/* } */

void test_teardown(void)
{
    int ret, cpu;
    u64 test;
    
    rdmsr_safe(MSR_IA32_DS_AREA, test);
    printk("test = %lx\n", test);

    for_each_cpu(cpu, &cpu_online_map)
    {
        ret = free_pebs_records(cpu);
        printk("free_pebs_records(%d) = %d\n", cpu, ret);
        
        ret = free_debug_store(cpu);
        printk("free_debug_store(%d) = %d\n", cpu, ret);

        free_perf_counter(pmcs[cpu], cpu);
    }
/* on_each_cpu(__test_teardown, NULL, 1); */

    rdmsr_safe(MSR_IA32_DS_AREA, test);
    printk("test = %lx\n", test);
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
