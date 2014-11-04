#include <asm/apic.h>
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

struct pebs_record
{
    /* fields available for all 64 bits architectures */
    u64 flags, ip;
    u64 ax, bx, cx, dx;
    u64 si, di, bp, sp;
    u64 r8, r9, r10, r11;
    u64 r12, r13, r14, r15;

    /* fields available since Nehalem architecture */
    u64 ia32_perf_global_status;
    u64 data_linear_address;
    u64 data_source_encoding;
    u64 latency_value;

    /* fields available since Haswell architecture */
    u64 eventing_ip;
    u64 tx_abort_information;
} __attribute__((packed));

static size_t pebs_size = 0;
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
