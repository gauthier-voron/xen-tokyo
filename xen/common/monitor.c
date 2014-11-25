#include <asm/ibs.h>
#include <asm/page.h>
#include <asm/paging.h>
#include <asm/pebs.h>
#include <xen/cpumask.h>
#include <xen/config.h>
#include <xen/lib.h>
#include <xen/percpu.h>
#include <xen/tasklet.h>


struct hotpage
{
    struct hotpage  *next;
    struct hotpage  *prev;
    unsigned long    gfn;
    struct vcpu     *vcpu;
    unsigned long    score;
};

static DEFINE_PER_CPU(struct hotpage, hotlist[BIGOS_HOTLIST_SIZE]);
static DEFINE_PER_CPU(struct hotpage *, hotlist_head);
static DEFINE_PER_CPU(struct hotpage *, hotlist_tail);


static void init_hotlists(void)
{
    int cpu;
    unsigned long i;
    struct hotpage *list;

    for_each_online_cpu(cpu)
    {
        list = per_cpu(hotlist, cpu);

        for (i=0; i<BIGOS_HOTLIST_SIZE; i++)
        {
            list[i].next  = &list[i+1];
            list[i].prev  = &list[i-1];
            list[i].gfn   = INVALID_GFN;
            list[i].vcpu  = NULL;
            list[i].score = 0;
        }
        list[i-1].next = &list[0];
        list[0].prev = &list[i-1];

        per_cpu(hotlist_head, cpu) = &list[0];
        per_cpu(hotlist_tail, cpu) = &list[i-1];
    }
}

static void print_hotlist(int cpu)
{
    unsigned long i;
    struct hotpage *cur = per_cpu(hotlist_head, cpu);
    struct hotpage *lst = per_cpu(hotlist, cpu);

    printk("===\n");

    for (i=0; i<BIGOS_HOTLIST_SIZE; i++)
    {
        if (cur < lst || cur > &lst[BIGOS_HOTLIST_SIZE - 1])
        {
            printk("invalid address (0x%lx)\n", (unsigned long) cur);
            return;
        }

        if ( cur->vcpu != NULL )
            printk("list[%-3lu] (d%dv%d@%10lx = %lu)\n",
                   (cur - lst), cur->vcpu->domain->domain_id,
                   cur->vcpu->vcpu_id, cur->gfn, cur->score);
        else
            printk("list[%-3lu] (d_v_@%lx = %lu)\n",
                   (cur - lst), cur->gfn, cur->score);

        cur = cur->next;
    }

    printk("===\n");
}

static void move_hotpage(struct hotpage *page, struct hotpage *where)
{
    if ( unlikely(page == where) )
        return;

    page->prev->next = page->next;
    page->next->prev = page->prev;

    page->next = where;
    page->prev = where->prev;

    where->prev->next = page;
    where->prev = page;
}

static void incr_hotpage(int cpu, struct hotpage *page, unsigned long score)
{
    struct hotpage *cur = page->prev;
    struct hotpage *head = per_cpu(hotlist_head, cpu);

    page->score += score;
    if ( page->score > BIGOS_HOTPAGE_CEIL )
        page->score = BIGOS_HOTPAGE_CEIL;

    while ( cur->score <= page->score && cur != head )
        cur = cur->prev;

    if ( cur == head && head->score <= page->score )
    {
        move_hotpage(page, head);
        per_cpu(hotlist_head, cpu) = page;
    }
    else
    {
        move_hotpage(page, cur->next);
    }
}

static void register_access(int cpu, unsigned long gfn, struct vcpu *vcpu)
{
    struct hotpage *start = per_cpu(hotlist_head, cpu);
    struct hotpage *end = per_cpu(hotlist_tail, cpu);
    struct hotpage *own = NULL;
    struct hotpage *cur = start;

    do {
        if ( cur->score < BIGOS_HOTPAGE_DECR )
            cur->score = 0;
        else
            cur->score -= BIGOS_HOTPAGE_DECR;

        if ( cur->gfn == gfn )
            own = cur;

        cur = cur->next;
    } while (cur != start);

    if ( own == NULL )
    {
        own = end;
        per_cpu(hotlist_tail, cpu) = own->prev;

        own->gfn = gfn;
        own->vcpu = vcpu;
        own->score = 0;
        incr_hotpage(cpu, own, BIGOS_HOTPAGE_INIT);
    }
    else if ( own->score == 0 )
    {
        incr_hotpage(cpu, own, BIGOS_HOTPAGE_INIT);
    }
    else
    {
        incr_hotpage(cpu, own, BIGOS_HOTPAGE_INCR);
    }
}


static s_time_t migrator_last_schedule = 0;

static void migrator_main(unsigned long arg __attribute__((unused)))
{
    void *mem = alloc_xenheap_page();

    printk("@time = %lu\n", NOW());
    print_hotlist(0);

    free_xenheap_page(mem);
}

static DECLARE_TASKLET(migrator, migrator_main, 0);

static void schedule_migrator(void)
{
    s_time_t now = NOW();

    now -= BIGOS_MIGRATOR_SLEEPMS * 1000000;
    if ( now < migrator_last_schedule )
        return;

    tasklet_schedule(&migrator);
    migrator_last_schedule = NOW();
}


/* static void pebs_nmi_handler(struct pebs_record *record, int cpu) */
/* { */
/*     printk("CPU[%d] <= 0x%lx\n", cpu, record->data_linear_address); */
/* } */

static int enable_monitoring_pebs(void)
{
    /* int ret; */

    /* ret = pebs_acquire(); */
    /* if ( ret ) */
    /*     return ret; */

    /* pebs_setevent(PEBS_MUOPS | PEBS_MUOPS_ALLLD); */
    /* pebs_setrate(0x10000); */
    /* pebs_sethandler(pebs_nmi_handler); */
    /* pebs_enable(); */
    printk("PEBS useless in virtualization context !\n");
    return -1;
}

static void disable_monitoring_pebs(void)
{
    /* pebs_disable(); */
    /* pebs_release(); */
}


static void ibs_nmi_handler(struct ibs_record *record, int cpu)
{
    if ( !(record->record_mode & IBS_RECORD_MODE_OP) )
        return;
    if ( !(record->record_mode & IBS_RECORD_MODE_DPA) )
        return;
    if ( current->domain->domain_id >= DOMID_FIRST_RESERVED )
        return;
    register_access(cpu, record->data_physical_address >> PAGE_SHIFT, current);
    schedule_migrator();
}

static int enable_monitoring_ibs(void)
{
    int ret;

    ret = ibs_acquire();
    if ( ret )
        return ret;

    ibs_setevent(IBS_EVENT_OP);
    ibs_setrate(0x10000000);
    ibs_sethandler(ibs_nmi_handler);
    ibs_enable();

    return 0;
}

static void disable_monitoring_ibs(void)
{
    ibs_disable();
    ibs_release();
}


int enable_monitoring(void)
{
    init_hotlists();

    if ( ibs_capable() )
        return enable_monitoring_ibs();
    else if ( pebs_capable() )
        return enable_monitoring_pebs();

    printk("Cannot find monitoring facility\n");
    return -1;
}

void disable_monitoring(void)
{
    if ( ibs_capable() )
        disable_monitoring_ibs();
    else if ( pebs_capable() )
        disable_monitoring_pebs();
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
