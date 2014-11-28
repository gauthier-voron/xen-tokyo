#include <asm/guest_access.h>
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
    struct hotpage  *next;     /* next hotpage (colder except for the last) */
    struct hotpage  *prev;     /* prev hotpage (hotter except for the first) */
    unsigned long    gfn;      /* guest frame number */
    unsigned long    mfn;      /* machine frame number */
    struct vcpu     *vcpu;     /* vcpu which has been sampled */
    unsigned long    score;    /* hot score (highter is hotter) */
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
            list[i].mfn   = INVALID_MFN;
            list[i].vcpu  = NULL;
            list[i].score = 0;
        }
        list[i-1].next = &list[0];
        list[0].prev = &list[i-1];

        per_cpu(hotlist_head, cpu) = &list[0];
        per_cpu(hotlist_tail, cpu) = &list[i-1];
    }
}

void print_hotlist(int cpu)
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

static void incr_hotpage(struct hotpage *page, unsigned long score)
{
    struct hotpage *cur = page->prev;
    struct hotpage *head = this_cpu(hotlist_head);

    page->score += score;
    if ( page->score > BIGOS_HOTPAGE_CEIL )
        page->score = BIGOS_HOTPAGE_CEIL;

    while ( cur->score <= page->score && cur != head )
        cur = cur->prev;

    if ( cur == head && head->score <= page->score )
    {
        move_hotpage(page, head);
        this_cpu(hotlist_head) = page;
    }
    else
    {
        move_hotpage(page, cur->next);
    }
}

static void register_access(unsigned long gfn, unsigned long mfn,
                            struct vcpu *vcpu)
{
    struct hotpage *start = this_cpu(hotlist_head);
    struct hotpage *end = this_cpu(hotlist_tail);
    struct hotpage *own = NULL;
    struct hotpage *cur = start;

    do {
        if ( cur->score < BIGOS_HOTPAGE_DECR )
            cur->score = 0;
        else
            cur->score -= BIGOS_HOTPAGE_DECR;

        if ( cur->gfn == gfn && cur->vcpu->domain == vcpu->domain )
            own = cur;

        cur = cur->next;
    } while (cur != start);

    if ( own == NULL )
    {
        own = end;
        this_cpu(hotlist_tail) = own->prev;

        own->gfn = gfn;
        own->vcpu = vcpu;
        own->score = 0;
        incr_hotpage(own, BIGOS_HOTPAGE_INIT);
    }
    else if ( own->score == 0 )
    {
        incr_hotpage(own, BIGOS_HOTPAGE_INIT);
    }
    else
    {
        incr_hotpage(own, BIGOS_HOTPAGE_INCR);
    }

    own->mfn = mfn;
}


static s_time_t migrator_last_schedule = 0;

static void migrator_main(unsigned long arg __attribute__((unused)))
{
    struct hotpage *head;
    struct hotpage *cur;
    unsigned long node;
    int cpu;

    for_each_online_cpu(cpu)
    {
        head = per_cpu(hotlist_head, cpu);
        cur = head;

        do {
            if ( cur->vcpu == NULL )
                break;

            node = phys_to_nid(cur->mfn << PAGE_SHIFT);
            printk("node=%lu gfn=0x%lx score=%lu\n",
                   node, cur->gfn, cur->score);

            cur = cur->next;
        } while ( cur != head );
    }

    migrator_last_schedule = NOW();
}

static DECLARE_TASKLET(migrator, migrator_main, 0);

static void schedule_migrator(void)
{
    s_time_t now = NOW();

    if ( smp_processor_id() != 0 )
        return;

    now -= BIGOS_MIGRATOR_SLEEPMS * 1000000ul;
    if ( now < migrator_last_schedule )
        return;

    tasklet_schedule(&migrator);
    migrator_last_schedule = (s_time_t) -1;
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


static void ibs_nmi_handler(struct ibs_record *record)
{
    unsigned long gfn;
    uint32_t pfec;

    if ( !(record->record_mode & IBS_RECORD_MODE_OP) )
        return;
    if ( !(record->record_mode & IBS_RECORD_MODE_DPA) )
        return;
    if ( current->domain->domain_id >= DOMID_FIRST_RESERVED )
        return;
    if ( current->domain->guest_type != guest_type_hvm )
        return;

    /*
     * FIXME: possible deadlock
     * If the interrupt occurs when we are already locking the p2m, the
     * interrupt handler will wait forever a lock which can be released only
     * if it stops waiting.
     * Possible fix: perform a trylock() on the p2m lock and abort the sample
     * if already locked.
     */
    local_irq_enable();
    pfec = PFEC_page_present;
    gfn = paging_gva_to_gfn(current, record->data_linear_address, &pfec);
    local_irq_disable();

    if (gfn == INVALID_MFN)
        return;

    register_access(gfn, record->data_physical_address >> PAGE_SHIFT, current);
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
