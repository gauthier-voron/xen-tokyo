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
    unsigned long    gfn;      /* guest frame number */
    unsigned long    mfn;      /* machine frame number */
    struct vcpu     *vcpu;     /* vcpu which has been sampled */
    unsigned long    score;    /* hot score (highter is hotter) */
    struct hotpage  *next;     /* next hotpage (colder except for the last) */
    struct hotpage  *prev;     /* prev hotpage (hotter except for the first) */
    struct hotpage  *anext;    /* next sort hotpage (gfn < anext->gfn) */
    struct hotpage  *aprev;    /* prev sort hotpage (gfn > aprev->gfn) */
};

static DEFINE_PER_CPU(struct hotpage, hotlist[BIGOS_HOTLIST_SIZE]);
static DEFINE_PER_CPU(struct hotpage *, hotlist_head);
static DEFINE_PER_CPU(struct hotpage *, hotlist_tail);
static DEFINE_PER_CPU(struct hotpage *, hotlist_sort);
static DEFINE_PER_CPU(unsigned long, cpuscore);


#define MIGRATOR_IS_SCHEDULED    ((s_time_t) -1)
static s_time_t migrator_last_schedule = 0;


struct distpage
{
    unsigned long     gfn;     /* guest physical frame number of the page */
    unsigned long     mfn;     /* machine frame number of the page */
    struct domain    *domain;  /* domain the page belongs to */
    int               major;   /* id of the node using the page the most */
    unsigned long     local;   /* score of the major node on the page */
    unsigned long     distant; /* sum score of the non major nodes */
};


static int cmphotpages(struct hotpage *a, struct hotpage *b)
{
    if ( a->mfn != b->mfn )
        return (a->mfn < b->mfn) ? -1 : 1;
    return 0;
}


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
            list[i].anext = &list[i+1];
            list[i].aprev = &list[i-1];
        }

        list[i-1].next  = &list[0];
        list[i-1].anext = &list[0];
        list[0].prev  = &list[i-1];
        list[0].aprev = &list[i-1];

        per_cpu(hotlist_head, cpu) = &list[0];
        per_cpu(hotlist_tail, cpu) = &list[i-1];
        per_cpu(hotlist_sort, cpu) = &list[0];
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
            printk("list[%3lu] (d%dv%d@%-10lx = %lu)\n",
                   (cur - lst), cur->vcpu->domain->domain_id,
                   cur->vcpu->vcpu_id, cur->mfn, cur->score);
        else
            printk("list[%3lu] (d_v_@%-10lx = %lu)\n",
                   (cur - lst), cur->mfn, cur->score);

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

static void move_sort_hotpage(struct hotpage *page, struct hotpage *where)
{
    if ( unlikely(page == where) )
        return;

    page->aprev->anext = page->anext;
    page->anext->aprev = page->aprev;

    page->anext = where;
    page->aprev = where->aprev;

    where->aprev->anext = page;
    where->aprev = page;
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

static void change_hotpage(struct hotpage *page, unsigned long gfn,
                           unsigned long mfn, struct vcpu *vcpu)
{
    struct hotpage *cur = page;
    struct hotpage *sort = this_cpu(hotlist_sort);

    page->gfn = gfn;
    page->mfn = mfn;
    page->vcpu = vcpu;

    while ( cmphotpages(page, cur->aprev) < 0 && cur != sort )
        cur = cur->aprev;
    while ( cmphotpages(page, cur) >= 0 && cur != sort )
        cur = cur->anext;

    move_sort_hotpage(page, cur);

    if ( cur == sort && cmphotpages(page, sort) < 0 )
        this_cpu(hotlist_sort) = page;
}

static void register_access(unsigned long gfn, unsigned long mfn,
                            struct vcpu *vcpu)
{
    struct hotpage *start = this_cpu(hotlist_head);
    struct hotpage *end = this_cpu(hotlist_tail);
    struct hotpage *own = NULL;
    struct hotpage *cur = start;
    struct hotpage  new;

    if ( migrator_last_schedule == MIGRATOR_IS_SCHEDULED )
        goto ignore;

    new.gfn = gfn;
    new.mfn = mfn;
    new.vcpu = vcpu;

    do {
        if ( cur->score < BIGOS_HOTPAGE_DECR )
            cur->score = 0;
        else
            cur->score -= BIGOS_HOTPAGE_DECR;

        if ( !cmphotpages(&new, cur) )
            own = cur;

        cur = cur->next;
    } while (cur != start);

    if ( own == NULL )
    {
        own = end;
        this_cpu(hotlist_tail) = own->prev;

        own->score = 0;
        incr_hotpage(own, BIGOS_HOTPAGE_INIT);
        change_hotpage(own, gfn, mfn, vcpu);
    }
    else if ( own->score == 0 )
    {
        incr_hotpage(own, BIGOS_HOTPAGE_INIT);
    }
    else
    {
        incr_hotpage(own, BIGOS_HOTPAGE_INCR);
    }

    this_cpu(cpuscore)++;
ignore:
    return;
}



static void init_hotlist_heap(struct hotpage **heap, size_t *size)
{
    struct hotpage *swap;
    size_t i, index = 0;
    int cpu;

    /* make the heap start at index 1 */
    heap--;
    index++;

    for_each_online_cpu ( cpu )
    {
        i = index++;
        heap[i] = per_cpu(hotlist_sort, cpu);
        while ( i > 1 && cmphotpages(heap[i], heap[i/2]) < 0 )
        {
            swap = heap[i];
            heap[i] = heap[i/2];
            heap[i/2] = swap;
            i /= 2;
        }
    }

    *size = index - 1;
}

static struct hotpage *pop_hotlist_heap(struct hotpage **heap, size_t *size)
{
    struct hotpage *ret = heap[0];
    struct hotpage *swap;
    size_t i = 0;
    int side;

    heap[0] = heap[0]->anext;

    if ( cmphotpages(ret, heap[0]) > 0 )
    {
        (*size)--;
        swap = heap[0];
        heap[0] = heap[*size];
        heap[*size] = swap;
    }

    /* make the heap start at index 1 */
    heap--;
    i++;
    (*size)++;

    while ( (i*2) < *size ) {
        side = 0;
        if ( ((i*2+1) < *size) && (cmphotpages(heap[i*2+1], heap[i*2]) < 0) )
            side = 1;

        if ( cmphotpages(heap[i], heap[i*2+side]) <= 0 )
            break;

        swap = heap[i];
        heap[i] = heap[i*2+side];
        heap[i*2+side] = swap;
        i = i * 2 + side;
    }

    (*size)--;

    return ret;
}

static void normalize_cpuscores(void)
{
    int cpu, mincpu;

    mincpu = 0;
    for_each_online_cpu ( cpu )
        if ( per_cpu(cpuscore, cpu) < per_cpu(cpuscore, mincpu) )
            mincpu = cpu;
    for_each_online_cpu ( cpu )
        if ( cpu != mincpu )
            per_cpu(cpuscore, cpu) -= per_cpu(cpuscore, mincpu);
    per_cpu(cpuscore, mincpu) = 0;
}

static void finalize_distpage(struct distpage *dist, unsigned long *scores)
{
    int node;

    dist->major = 0;
    for_each_online_cpu ( node )
        if ( scores[node] > scores[dist->major] )
            dist->major = node;

    dist->local = scores[dist->major];
    dist->distant = 0;
    for_each_online_cpu ( node )
        if ( node != dist->major )
            dist->distant += scores[node];
}

static void process_distpage(struct distpage *distants, size_t size,
                             size_t *distsize, const struct distpage *dist)
{
    unsigned long node = phys_to_nid(dist->mfn << PAGE_SHIFT);
    unsigned long crate = (dist->local * 100) / (dist->local + dist->distant);
    unsigned long tmp;
    size_t i, worst;

    if ( dist->major == node )
        return;
    if ( dist->local < BIGOS_MIGRATOR_LCSCORE )
        return;
    if ( crate < BIGOS_MIGRATOR_LOCRATE )
        return;

    if ( *distsize < size )
    {
        distants[*distsize] = *dist;
        (*distsize)++;
        return;
    }

    worst = (size_t) -1;
    for (i=0; i<size; i++)
    {
        tmp  = (distants[i].local * 100);
        tmp /= (distants[i].local + distants[i].distant);
        if ( tmp < crate )
        {
            crate = tmp;
            worst = i;
        }
    }

    if ( worst == (size_t) -1 )
        return;

    distants[worst] = *dist;
}

static size_t find_distant_accesses(struct distpage *distants, size_t size)
{
    static struct hotpage *heap[NR_CPUS];
    static unsigned long scores[NR_CPUS];
    size_t heapsize, distsize = 0;
    struct distpage dist;
    struct hotpage *page;
    int node;

    normalize_cpuscores();
    init_hotlist_heap(heap, &heapsize);

    dist.mfn = INVALID_MFN;
    while ( heapsize > 0 )
    {
        page = pop_hotlist_heap(heap, &heapsize);
        if ( page->mfn == INVALID_MFN )
            break;
        if ( page->score == 0 )
            break;

        if ( page->mfn != dist.mfn )
        {
            if ( likely(dist.mfn != INVALID_MFN) )
            {
                finalize_distpage(&dist, scores);
                process_distpage(distants, size, &distsize, &dist);
            }

            for_each_online_cpu ( node )
                scores[node] = 0;
            dist.mfn = page->mfn;
            dist.gfn = page->gfn;
            dist.domain = page->vcpu->domain;
        }

        scores[cpu_to_node(page->vcpu->processor)] += page->score
            + per_cpu(cpuscore, page->vcpu->processor);
    }

    return distsize;
}

static void migrator_main(unsigned long arg __attribute__((unused)))
{
    struct distpage distants[BIGOS_MIGRATOR_MAXMOVE];
    size_t i, size;

    size = find_distant_accesses(distants, BIGOS_MIGRATOR_MAXMOVE);

    for (i=0; i<size; i++)
        memory_move(distants[i].domain, distants[i].gfn, distants[i].major);

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
    migrator_last_schedule = MIGRATOR_IS_SCHEDULED;
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

    local_irq_enable();
    pfec = PFEC_page_present;
    gfn = try_paging_gva_to_gfn(current, record->data_linear_address, &pfec);
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
