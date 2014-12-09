#include <asm/guest_access.h>
#include <asm/ibs.h>
#include <asm/page.h>
#include <asm/paging.h>
#include <asm/pebs.h>
#include <xen/cpumask.h>
#include <xen/config.h>
#include <xen/lib.h>
#include <xen/monitor.h>
#include <xen/percpu.h>
#include <xen/tasklet.h>


static int monitoring_started = 0;            /* is the monitoring running ? */

struct hotpage
{
    unsigned long    gfn;      /* guest frame number */
    unsigned long    mfn;      /* machine frame number */
    struct vcpu     *vcpu;     /* vcpu which has been sampled */
    unsigned long    score;    /* hot score (highter is hotter) */
    struct hotpage  *score_next;
    struct hotpage  *score_prev;
    struct hotpage  *pgid_next;    /* next sort hotpage (gfn < anext->gfn) */
    struct hotpage  *pgid_prev;    /* prev sort hotpage (gfn > aprev->gfn) */
};

struct hotlist
{
    struct hotpage  *pool;         /* memory pool of hotpages */
    struct hotpage  *score_head;   /* hotpage with maximum score */
    struct hotpage  *pgid_head;    /* hotpage with minimum pgid */
    unsigned int     score;        /* hotlist cpu local score */
};

static DEFINE_PER_CPU(struct hotlist, hotlist);

static unsigned long hotlist_size = BIGOS_HOTLIST_DEFAULT_SIZE;
static unsigned int  hotlist_score_enter = BIGOS_HOTLIST_DEFAULT_SCORE_ENTER;
static unsigned int  hotlist_score_incr = BIGOS_HOTLIST_DEFAULT_SCORE_INCR;
static unsigned int  hotlist_score_decr = BIGOS_HOTLIST_DEFAULT_SCORE_DECR;
static unsigned int  hotlist_score_max = BIGOS_HOTLIST_DEFAULT_SCORE_MAX;


#define MIGRATOR_IS_SCHEDULED    ((s_time_t) -1)
static s_time_t migrator_last_schedule = 0;

struct hotlist_heap
{
    struct hotpage  *arr[NR_CPUS];   /* the array of hotlist, 1 hotpage/list */
    size_t           size;           /* the size of the heap */
};

struct hotpage_info
{
    unsigned long    mfn;      /* mfn of the page to move */
    unsigned int     node;     /* destination node for move */
    unsigned int     lcrate;   /* destination node access rate */
    unsigned int     lcscore;  /* destination node local scores sum */
    unsigned long    gfn;      /* gfn of the page to move */
    struct domain   *domain;   /* domain in which the gfn is valid */
};

static struct hotpage_info  *info_buffer;

static unsigned long    migrator_cooldown = BIGOS_MIGRATOR_DEFAULT_COOLDOWN;
static unsigned long    migrator_size = BIGOS_MIGRATOR_DEFAULT_SIZE;
static unsigned int     migrator_lcrate = BIGOS_MIGRATOR_DEFAULT_LCRATE;
static unsigned int     migrator_lcscore = BIGOS_MIGRATOR_DEFAULT_LCSCORE;


static int cmphotpages(struct hotpage *a, struct hotpage *b)
{
    if ( a->mfn != b->mfn )
        return (a->mfn < b->mfn) ? -1 : 1;
    return 0;
}

/*
 * Free the hotlists pool of hotpages for each online cpu.
 * Hotlist cannot be used after this function has been called.
 */
static void free_hotlist(void)
{
    int cpu;
    unsigned long size = hotlist_size;
    unsigned int order = PAGE_ORDER_4K;
    struct hotlist *list;

    while ( (1ul << (PAGE_SHIFT + order)) < (size * sizeof(struct hotpage)) )
        order++;

    for_each_online_cpu ( cpu )
    {
        list = &per_cpu(hotlist, cpu);
        if ( list->pool != NULL )
        {
            free_xenheap_pages(list->pool, order);
            list->pool = NULL;
        }
    }
}

/*
 * Alloc the hotlists pool of hotpages for each online cpus.
 * The function init_hotlist() should be called before any attempt to use the
 * hotlists.
 * Return 0 in case of success.
 */
static int alloc_hotlist(void)
{
    int cpu;
    unsigned long size = hotlist_size;
    unsigned int order = PAGE_ORDER_4K;
    struct hotlist *list;

    while ( (1ul << (PAGE_SHIFT + order)) < (size * sizeof(struct hotpage)) )
        order++;

    for_each_online_cpu ( cpu )
    {
        list = &per_cpu(hotlist, cpu);
        list->pool = alloc_xenheap_pages(order, 0);
        if ( list->pool == NULL )
            goto err;
    }

    return 0;
err:
    free_hotlist();
    return -1;
}

/*
 * Initialize the hotlists for each online cpus.
 * The hotlists must be allocated before to be initialized.
 * Hotlists are empty and ready to be used after this function returns.
 */
static void init_hotlist(void)
{
    int cpu;
    unsigned long i;
    struct hotlist *list;
    struct hotpage *pool;

    for_each_online_cpu(cpu)
    {
        list = &per_cpu(hotlist, cpu);
        pool = list->pool;

        for (i=0; i<hotlist_size; i++)
        {
            pool[i].score_next  = &pool[i+1];
            pool[i].score_prev  = &pool[i-1];
            pool[i].gfn   = INVALID_GFN;
            pool[i].mfn   = INVALID_MFN;
            pool[i].vcpu  = NULL;
            pool[i].score = 0;
            pool[i].pgid_next = &pool[i+1];
            pool[i].pgid_prev = &pool[i-1];
        }

        pool[i-1].score_next  = &pool[0];
        pool[i-1].pgid_next = &pool[0];
        pool[0].score_prev  = &pool[i-1];
        pool[0].pgid_prev = &pool[i-1];

        list->score_head = &pool[0];
        list->pgid_head = &pool[0];
        list->score = 0;
    }
}

void print_hotlist(int cpu)
{
    unsigned long i;
    struct hotpage *cur = per_cpu(hotlist, cpu).score_head;
    struct hotpage *lst = per_cpu(hotlist, cpu).pool;

    printk("===\n");

    for (i=0; i<hotlist_size; i++)
    {
        if (cur < lst || cur > &lst[hotlist_size - 1])
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

        cur = cur->score_next;
    }

    printk("===\n");
}

static void hotlist_score_move(struct hotpage *page, struct hotpage *where)
{
    if ( unlikely(page == where) )
        return;

    page->score_prev->score_next = page->score_next;
    page->score_next->score_prev = page->score_prev;

    page->score_next = where;
    page->score_prev = where->score_prev;

    where->score_prev->score_next = page;
    where->score_prev = page;
}

static void hotlist_score_update(struct hotpage *page)
{
    struct hotpage *cur = page->score_prev;
    struct hotpage *head = this_cpu(hotlist).score_head;

    if ( page->score > hotlist_score_max )
        page->score = hotlist_score_max;

    while ( cur->score <= page->score && cur != head )
        cur = cur->score_prev;

    if ( cur == head && head->score <= page->score )
    {
        hotlist_score_move(page, head);
        this_cpu(hotlist).score_head = page;
    }
    else
    {
        hotlist_score_move(page, cur->score_next);
    }
}

static void hotlist_pgid_move(struct hotpage *page, struct hotpage *where)
{
    if ( unlikely(page == where) )
        return;

    page->pgid_prev->pgid_next = page->pgid_next;
    page->pgid_next->pgid_prev = page->pgid_prev;

    page->pgid_next = where;
    page->pgid_prev = where->pgid_prev;

    where->pgid_prev->pgid_next = page;
    where->pgid_prev = page;
}

static void hotlist_pgid_update(struct hotpage *page)
{
    struct hotpage *cur = page;
    struct hotpage *sort = this_cpu(hotlist).pgid_head;

    while ( cmphotpages(page, cur->pgid_prev) < 0 && cur != sort )
        cur = cur->pgid_prev;
    while ( cmphotpages(page, cur) >= 0 && cur != sort )
        cur = cur->pgid_next;

    hotlist_pgid_move(page, cur);

    if ( cur == sort && cmphotpages(page, sort) < 0 )
        this_cpu(hotlist).pgid_head = page;
}

static void forget_page(unsigned long mfn)
{
    int cpu;
    struct hotpage *head, *page;

    for_each_online_cpu ( cpu )
    {
        head = per_cpu(hotlist, cpu).pgid_head;
        page = head;

        do {
            if ( page->mfn == mfn )
            {
                hotlist_pgid_move(page, per_cpu(hotlist, cpu).pgid_head);
                hotlist_score_move(page, per_cpu(hotlist, cpu).score_head);

                page->mfn = INVALID_MFN;
                page->score = 0;

                break;
            }

            if ( page->mfn > mfn )
                break;

            page = page->pgid_next;
        } while (head != page);
    }
}

static void touch_page(unsigned long gfn, unsigned long mfn,
                       struct vcpu *vcpu)
{
    struct hotpage *head = this_cpu(hotlist).score_head;
    struct hotpage *found = NULL;
    struct hotpage *cur = head;
    struct hotpage  new;

    if ( migrator_last_schedule == MIGRATOR_IS_SCHEDULED )
        goto ignore;

    new.gfn = gfn;
    new.mfn = mfn;
    new.vcpu = vcpu;

    do {
        if ( cur->score < hotlist_score_decr )
            cur->score = 0;
        else
            cur->score -= hotlist_score_decr;

        if ( !cmphotpages(&new, cur) )
            found = cur;

        cur = cur->score_next;
    } while (cur != head);

    if ( found == NULL )
    {
        found = head->score_prev;

        found->score = 0;
        found->gfn = gfn;
        found->mfn = mfn;
        found->vcpu = vcpu;

        hotlist_pgid_update(found);
    }
    
    if ( found->score == 0 )
        found->score = hotlist_score_enter;
    else
        found->score += hotlist_score_incr;
    hotlist_score_update(found);

    this_cpu(hotlist).score += hotlist_score_decr;
ignore:
    return;
}



#define SWAP_HOTPAGES(a, b)                     \
    {                                           \
        struct hotpage *____swap = (a);         \
        (a) = (b);                              \
        (b) = ____swap;                         \
    }

static void init_hotlist_heap(struct hotlist_heap *heap)
{
    struct hotpage **arr = heap->arr;
    size_t i, index = 0;
    int cpu;

    /* make the arr start at index 1 */
    arr--;
    index++;

    for_each_online_cpu ( cpu )
    {
        i = index++;
        arr[i] = per_cpu(hotlist, cpu).pgid_head;
        while ( i > 1 && cmphotpages(arr[i], arr[i/2]) < 0 )
        {
            SWAP_HOTPAGES(arr[i], arr[i/2]);
            i /= 2;
        }
    }

    heap->size = index - 1;
}

static void decrease_hotlist_heap(struct hotlist_heap *heap)
{
    struct hotpage **arr = heap->arr;
    size_t i = 0, size = heap->size;
    int side;

    /* make the heap start at index 1 */
    arr--;
    i++;
    size++;

    while ( (i*2) < size ) {
        side = 0;
        if ( ((i*2+1) < size) && (cmphotpages(arr[i*2+1], arr[i*2]) < 0) )
            side = 1;

        if ( cmphotpages(arr[i], arr[i*2+side]) <= 0 )
            break;

        SWAP_HOTPAGES(arr[i], arr[i*2+side]);
        i = i * 2 + side;
    }
}

static struct hotpage *pop_hotlist_heap(struct hotlist_heap *heap)
{
    struct hotpage *ret = NULL;
    struct hotpage **arr = heap->arr;

    if ( heap->size == 0 )
        goto out;
    if ( arr[0]->mfn == INVALID_MFN )
        goto out;
    
    ret = arr[0];
    arr[0] = arr[0]->pgid_next;

    if ( cmphotpages(ret, arr[0]) > 0 )
    {
        heap->size--;
        SWAP_HOTPAGES(arr[0], arr[heap->size]);
    }

    decrease_hotlist_heap(heap);
out:
    return ret;
}

static void rebase_hotlist_scores(void)
{
    int cpu;
    unsigned int minscore = (unsigned int) -1;

    for_each_online_cpu ( cpu )
        if ( per_cpu(hotlist, cpu).score < minscore )
            minscore = per_cpu(hotlist, cpu).score;
    for_each_online_cpu ( cpu )
        per_cpu(hotlist, cpu).score -= minscore;
}


static int alloc_info_buffer(void)
{
    int ret = 0;
    unsigned long size = migrator_size;
    unsigned int order = PAGE_ORDER_4K;

    while ( (1ul << (PAGE_SHIFT+order)) < (size*sizeof(struct hotpage_info)) )
        order++;

    info_buffer = alloc_xenheap_pages(order, 0);
    if ( info_buffer == NULL )
        ret = -1;

    return ret;
}

static void free_info_buffer(void)
{
    unsigned long size = migrator_size;
    unsigned int order = PAGE_ORDER_4K;

    while ( (1ul << (PAGE_SHIFT+order)) < (size*sizeof(struct hotpage_info)) )
        order++;

    free_xenheap_pages(info_buffer, order);
    info_buffer = NULL;
}

static void reset_info_buffer_slot(unsigned long slot)
{
    info_buffer[slot].mfn = INVALID_MFN;
    info_buffer[slot].gfn = INVALID_GFN;
    info_buffer[slot].domain = NULL;
    info_buffer[slot].lcrate = 0;
    info_buffer[slot].lcscore = 0;
}

static void init_info_buffer(void)
{
    unsigned long i;

    for (i=0; i<migrator_size; i++)
        reset_info_buffer_slot(i);
}


static void compute_scores(struct hotpage_info *dest, unsigned int *scores,
                           unsigned long size)
{
    unsigned long i;
    unsigned int total = 0;

    dest->node = 0;
    for (i=0; i<size; i++)
        if ( scores[i] > scores[dest->node] )
            dest->node = i;

    for (i=0; i<size; i++)
        total += scores[i];

    dest->lcscore = scores[dest->node];

    if ( total != 0 )
        dest->lcrate = (scores[dest->node] * 100) / total;
    else
        dest->lcrate = 0;
}

static void prepare_hotpage(struct hotpage_info *new)
{
    unsigned long i;
    unsigned long slot_min_lcrate = 0;

    for (i=0; i<migrator_size; i++)
        if ( info_buffer[i].lcrate < info_buffer[slot_min_lcrate].lcrate )
            slot_min_lcrate = i;

    if ( info_buffer[slot_min_lcrate].lcrate >= new->lcrate )
        return;

    info_buffer[slot_min_lcrate] = *new;
}

static void fill_info_buffer(void)
{
    static struct hotlist_heap heap;
    static unsigned int score_per_node[NR_CPUS];      /* NR_CPUS >= NR_NODES */
    struct hotpage_info new;
    struct hotpage *page;
    int cpu, node;

    init_hotlist_heap(&heap);

    new.mfn = INVALID_MFN;
    for_each_online_cpu ( node )
        score_per_node[node] = 0;

    while ( (page = pop_hotlist_heap(&heap)) != NULL )
    {
        if ( page->mfn != new.mfn )
        {
            compute_scores(&new, score_per_node, NR_CPUS);

            if ( new.lcscore >= migrator_lcscore
                 && new.lcrate >= migrator_lcrate )
                prepare_hotpage(&new);

            for_each_online_cpu ( node )
                score_per_node[node] = 0;
            new.mfn = page->mfn;
            new.gfn = page->gfn; /* no */
            new.domain = page->vcpu->domain; /* no */
        }

        cpu  = page->vcpu->processor;
        node = cpu_to_node(cpu);
        score_per_node[node] += page->score + per_cpu(hotlist, cpu).score;
    }
}

static unsigned long moved = 0;
static void flush_info_buffer(void)
{
    unsigned long i;

    for (i=0; i<migrator_size; i++)
        if ( info_buffer[i].domain && info_buffer[i].gfn != INVALID_GFN )
        {
            memory_move(info_buffer[i].domain, info_buffer[i].gfn,
                        info_buffer[i].node);

            moved++;
            forget_page(info_buffer[i].mfn);

            reset_info_buffer_slot(i);
        }
}

static void migrator_main(unsigned long arg __attribute__((unused)))
{
    if ( !monitoring_started )
        return;

    rebase_hotlist_scores();
    fill_info_buffer();
    flush_info_buffer();

    migrator_last_schedule = NOW();
}

static DECLARE_TASKLET(migrator, migrator_main, 0);

static void schedule_migrator(void)
{
    s_time_t now = NOW();

    if ( smp_processor_id() != 0 )
        return;

    now -= migrator_cooldown * 1000000ul;
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
    return 0;
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

    touch_page(gfn, record->data_physical_address >> PAGE_SHIFT, current);
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


int monitor_hotlist_setsize(unsigned long size)
{
    int restart = monitoring_started;
    
    stop_monitoring();
    hotlist_size = size;
    
    if ( restart )
        return start_monitoring();
    return 0;
}

void monitor_hotlist_setparm(unsigned int score_enter, unsigned int score_incr,
                             unsigned int score_decr, unsigned int score_max)
{
    hotlist_score_enter = score_enter;
    hotlist_score_incr = score_incr;
    hotlist_score_decr = score_decr;
    hotlist_score_max = score_max;
}


int monitor_migration_setsize(unsigned long size)
{
    int restart = monitoring_started;

    stop_monitoring();
    migrator_size = size;

    if ( restart )
        return start_monitoring();
    return 0;
}

void monitor_migration_setparm(unsigned long cooldown,
			       unsigned int min_local_score,
			       unsigned int min_local_rate)
{
    migrator_cooldown = cooldown;
    migrator_lcscore = min_local_score;
    migrator_lcrate = min_local_rate;
}


int start_monitoring(void)
{
    if ( monitoring_started )
        return -1;
    
    if ( alloc_info_buffer() != 0 )
        goto err;
    if ( alloc_hotlist() != 0 )
        goto err_ibuf;

    init_info_buffer();
    init_hotlist();

    if ( ibs_capable() && enable_monitoring_ibs() == 0 )
        goto out;
    if ( pebs_capable() && enable_monitoring_pebs() == 0 )
        goto out;
    goto err_hlst;

out:
    monitoring_started = 1;
    return 0;
err_hlst:
    printk("Cannot find monitoring facility\n");
    free_hotlist();
err_ibuf:
    free_info_buffer();
err:
    return -1;
}

void stop_monitoring(void)
{
    if ( !monitoring_started )
        return;
    
    if ( ibs_capable() )
        disable_monitoring_ibs();
    else if ( pebs_capable() )
        disable_monitoring_pebs();

    free_hotlist();
    free_info_buffer();

    printk("moved = %lu\n", moved);
    moved = 0;

    monitoring_started = 0;
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
