#include <asm/guest_access.h>
#include <asm/ibs.h>
#include <asm/page.h>
#include <asm/paging.h>
#include <asm/pebs.h>
#include <asm/system.h>
#include <xen/cpumask.h>
#include <xen/config.h>
#include <xen/hotlist.h>
#include <xen/lib.h>
#include <xen/migration.h>
#include <xen/monitor.h>
#include <xen/percpu.h>
#include <xen/rbtree.h>


struct migration_query
{
    unsigned long   mfn;
    unsigned int    node;
    unsigned long   gfn;
    struct domain  *domain;
    unsigned int    tries;
    struct rb_node  rbnode;
};


static int monitoring_started = 0;            /* is the monitoring running ? */

static DEFINE_PER_CPU(unsigned long, migration_engine_owner);
#define OWNER_NONE      0
#define OWNER_SAMPLER   1
#define OWNER_DECIDER   2
#define OWNER_MIGRATOR  3

static struct rb_root          migration_tree;
static unsigned long           migration_alloc;
static struct migration_query *migration_pool;


static unsigned long  monitor_tracked = BIGOS_MONITOR_TRACKED;
static unsigned long  monitor_candidate = BIGOS_MONITOR_CANDIDATE;
static unsigned long  monitor_enqueued = BIGOS_MONITOR_ENQUEUED;
static unsigned int   monitor_enter = BIGOS_MONITOR_ENTER;
static unsigned int   monitor_increment = BIGOS_MONITOR_INCREMENT;
static unsigned int   monitor_decrement = BIGOS_MONITOR_DECREMENT;
static unsigned int   monitor_maximum = BIGOS_MONITOR_MAXIMUM;
static unsigned int   monitor_min_node_score = BIGOS_MONITOR_MIN_NODE_SCORE;
static unsigned int   monitor_min_node_rate = BIGOS_MONITOR_MIN_NODE_RATE;
static unsigned char  monitor_flush_after_refill = BIGOS_MONITOR_FLUSH;
static unsigned int   monitor_maxtries = BIGOS_MONITOR_MAXTRIES;
static unsigned long  monitor_rate = BIGOS_MONITOR_RATE;
static unsigned long  monitor_order = BIGOS_MONITOR_ORDER;


#ifdef BIGOS_STATS

static s_time_t         stats_start = 0;
static s_time_t         stats_end = 0;

static DEFINE_PER_CPU(s_time_t, time_counter_0);
static DEFINE_PER_CPU(s_time_t, time_counter_1);
static s_time_t         time_counter_2;

static DEFINE_PER_CPU(unsigned long, sampling_count);  /* IBS/PEBS count    */
static DEFINE_PER_CPU(s_time_t, sampling_total_time);  /* IBS/PEBS total ns */
static DEFINE_PER_CPU(s_time_t, sampling_accounting_time);    /* hotlist ns */
static DEFINE_PER_CPU(s_time_t, sampling_probing_time);/* info gathering ns */
static DEFINE_PER_CPU(unsigned long, probing_count);       /* # pfn probing */


static s_time_t         decision_total_time = 0;       /* planning time  ns */
static s_time_t         listwalk_total_time = 0;       /* popping time   ns */
static s_time_t         migration_total_time = 0;      /* migration time ns */

static unsigned long    decision_count = 0;        /* # decision process */
static unsigned long    migration_planned = 0;     /* # page in info_buffer */
static unsigned long    migration_tries = 0;       /* # memory_move call */
static unsigned long    migration_succeed = 0;     /* # memory_move return 0 */
static unsigned long    migration_aborted = 0;     /* # maxtries cancel */
static unsigned long    migration_nomove = 0;      /* # already good node */

static void stats_reset(void)
{
    int cpu;

    for_each_online_cpu ( cpu )
    {
        per_cpu(sampling_total_time, cpu) = 0;
        per_cpu(sampling_accounting_time, cpu) = 0;
        per_cpu(sampling_probing_time, cpu) = 0;
        per_cpu(sampling_count, cpu) = 0;
        per_cpu(probing_count, cpu) = 0;
    }
    decision_count = 0;
    decision_total_time = 0;
    listwalk_total_time = 0;
    migration_total_time = 0;
    migration_planned = 0;
    migration_tries = 0;
    migration_succeed = 0;
    migration_aborted = 0;
    migration_nomove = 0;
    stats_start = 0;
    stats_end = 0;
}

#define stats_start()        stats_start = NOW()
#define stats_end()          stats_end = NOW()

#define stats_start_sampling()                  \
    this_cpu(time_counter_0) = NOW(), this_cpu(sampling_count)++
#define stats_stop_sampling()                                           \
    this_cpu(sampling_total_time) += (NOW() - this_cpu(time_counter_0))

#define stats_start_accounting()   this_cpu(time_counter_1) = NOW()
#define stats_stop_accounting()                                         \
    this_cpu(sampling_accounting_time) += (NOW() - this_cpu(time_counter_1))

#define stats_start_probing()                   \
    this_cpu(time_counter_1) = NOW(), this_cpu(probing_count)++
#define stats_stop_probing()                                            \
    this_cpu(sampling_probing_time) += (NOW() - this_cpu(time_counter_1))

#define stats_start_decision()     \
    time_counter_2 = NOW(), decision_count++
#define stats_stop_decision()                       \
    decision_total_time += (NOW() - time_counter_2)

#define stats_start_migration()    time_counter_2 = NOW()
#define stats_stop_migration()                          \
    migration_total_time += (NOW() - time_counter_2)


#define stats_account_migration_plan()          \
    migration_planned++

#define stats_account_migration_abort()         \
    migration_aborted++

#define stats_account_migration_nomove()        \
    migration_nomove++

#define stats_account_migration_try(ret)            \
    migration_tries++, migration_succeed += (!(ret))


#define MIN_MAX_AVG(percpu, min, max, avg)          \
    {                                               \
        unsigned int ____cpu;                       \
        unsigned long ____count = 0;                \
        min = -1; max = 0; avg = 0;                 \
        for_each_online_cpu ( ____cpu )             \
        {                                           \
            ____count++;                            \
            if ( per_cpu(percpu, ____cpu) < min )   \
                min = per_cpu(percpu, ____cpu);     \
            if ( per_cpu(percpu, ____cpu) > max )   \
                max = per_cpu(percpu, ____cpu);     \
            avg += per_cpu(percpu, ____cpu);        \
        }                                           \
        avg /= ____count;                           \
    }

static void stats_display(void)
{
    unsigned long min, max, avg;

    printk("   ***   BIGOS STATISTICS   ***   \n");
    printk("statistics over %lu nanoseconds\n", stats_end - stats_start);
    printk("\n");

    MIN_MAX_AVG(sampling_count, min, max, avg);
    printk("sampling total count         %lu/%lu/%lu\n", min, max, avg);
    MIN_MAX_AVG(sampling_total_time, min, max, avg);
    printk("sampling total time          %lu/%lu/%lu ns\n", min, max, avg);
    MIN_MAX_AVG(sampling_accounting_time, min, max, avg);
    printk("sampling accounting time     %lu/%lu/%lu ns\n", min, max, avg);
    MIN_MAX_AVG(probing_count, min, max, avg);
    printk("sampling probing count       %lu/%lu/%lu\n", min, max, avg);
    MIN_MAX_AVG(sampling_probing_time, min, max, avg);
    printk("sampling probing time        %lu/%lu/%lu ns\n", min, max, avg);
    printk("\n");
    printk("decision total count         %lu\n", decision_count);
    printk("decision total time          %lu ns\n", decision_total_time);
    printk("\n");
    printk("migration total time         %lu ns\n", migration_total_time);
    printk("migration planned            %lu\n", migration_planned);
    printk("migration tries              %lu\n", migration_tries);
    printk("migration succeed            %lu\n", migration_succeed);
    printk("migration aborted            %lu\n", migration_aborted);
    printk("migration useless            %lu\n", migration_nomove);
    printk("\n");

    MIN_MAX_AVG(sampling_total_time, min, max, avg);
    printk("total overhead               %lu%%\n",
           ((max + decision_total_time + migration_total_time)
            * 100) / (stats_end - stats_start + 1));
}

#else /* ifndef BIGOS_STATS */

#define stats_reset()                      {}
#define stats_start()                      {}
#define stats_end()                        {}
#define stats_start_sampling()             {}
#define stats_stop_sampling()              {}
#define stats_start_accounting()           {}
#define stats_stop_accounting()            {}
#define stats_start_probing()              {}
#define stats_stop_probing()               {}
#define stats_start_decision()             {}
#define stats_stop_decision()              {}
#define stats_start_migration()            {}
#define stats_stop_migration()             {}
#define stats_account_migration_plan()     {}
#define stats_account_migration_abort()    {}
#define stats_account_migration_nomove()   {}
#define stats_account_migration_try(ret)   {}
#define stats_display()                    {}

#endif /* ifndef BIGOS_STATS */


#ifdef BIGOS_MEMORY_STATS

struct mstats_page
{
    unsigned long   memory_access : 27;
    unsigned long   cache_access  : 27;
    unsigned long   moves         : 10;
};

static struct mstats_page *mstats_pool;


static int mstats_alloc(void)
{
    int ret = 0;
	unsigned long order, size = total_pages;

    order = get_order_from_bytes(size * sizeof(struct mstats_page));
	mstats_pool = alloc_xenheap_pages(order, 0);

    printk("Allocated %lu bytes for memory statistics\n",
           size * sizeof(struct mstats_page));

	if ( mstats_pool == NULL )
		ret = -1;
	return ret;
}

static void mstats_init(void)
{
    unsigned long i;

    for (i=0; i<total_pages; i++)
    {
        mstats_pool[i].memory_access = 0;
        mstats_pool[i].cache_access = 0;
        mstats_pool[i].moves = 0;
    }

    printk("Initialized %lu entries for memory statistics\n", total_pages);
}

static int mstats_reset(void)
{
    int ret = 0;

    if ( mstats_pool == NULL )
        ret = mstats_alloc();
    if ( ret == 0 )
        mstats_init();

    return ret;
}


static inline void mstats_memory_access(unsigned long mfn)
{
    mstats_pool[mfn].memory_access++;
}

static inline void mstats_cache_access(unsigned long mfn)
{
    mstats_pool[mfn].cache_access++;
}

static inline void mstats_memory_moved(unsigned long mfn)
{
    mstats_pool[mfn].moves++;
}

int mstats_get_page(unsigned long mfn, unsigned long *memory,
                    unsigned long *cache, unsigned long *moves,
                    unsigned long *next)
{
    if ( mstats_pool == NULL )
        return -1;
    if ( mfn >= total_pages )
        return -1;

    *memory = mstats_pool[mfn].memory_access;
    *cache = mstats_pool[mfn].cache_access;
    *moves = mstats_pool[mfn].moves;

    *next = mfn;
    for (mfn++; mfn<total_pages; mfn++)
    {
        if ( mstats_pool[mfn].memory_access != 0 )
            break;
        if ( mstats_pool[mfn].cache_access != 0 )
            break;
        if ( mstats_pool[mfn].moves != 0 )
            break;
    }

    if ( mfn < total_pages )
        *next = mfn;

    return 0;
}

#else /* ifndef BIGOS_MEMORY_STATS */

#define mstats_reset()                     (0)
#define mstats_memory_access(mfn)          do { } while (0)
#define mstats_cache_access(mfn)           do { } while (0)
#define mstats_memory_moved(mfn)           do { } while (0)

#endif /* ifndef BIGOS_MEMORY_STATS */


static int alloc_migration_queue(void)
{
	int ret = 0;
	unsigned long order, size = monitor_enqueued;

    order = get_order_from_bytes(size * sizeof(struct migration_query));
	migration_pool = alloc_xenheap_pages(order, 0);

	if ( migration_pool == NULL )
		ret = -1;
	return ret;
}

static void init_migration_queue(void)
{
    unsigned long i;

    migration_tree = RB_ROOT;
    migration_alloc = 0;

    for (i=0; i<monitor_enqueued; i++)
    {
        migration_pool[i].mfn = INVALID_MFN;
        RB_CLEAR_NODE(&migration_pool[i].rbnode);
    }
}

static void free_migration_queue(void)
{
	unsigned long order, size = monitor_enqueued;

    if ( migration_pool == NULL )
        return;
	order = get_order_from_bytes(size * sizeof(struct migration_query));

	free_xenheap_pages(migration_pool, order);
    migration_pool = NULL;
}



static struct migration_query *find_migration_query(unsigned long mfn)
{
    struct rb_node *node = migration_tree.rb_node;
    struct migration_query *query = NULL;

    while ( node )
    {
        query = container_of(node, struct migration_query, rbnode);

        if ( mfn < query->mfn )
            node = node->rb_left;
        else if ( mfn > query->mfn )
            node = node->rb_right;
        else
            break;
    }

    return query;
}

static void insert_migration_query(struct migration_query *new,
                                   struct migration_query *parent)
{
    struct rb_node *parent_node, **parent_ptr;

    if ( unlikely(parent == NULL) )
    {
        parent_node = NULL;
        parent_ptr = &migration_tree.rb_node;
    }
    else
    {
        parent_node = &parent->rbnode;
        if ( new->mfn < parent->mfn )
            parent_ptr = &parent_node->rb_left;
        else
            parent_ptr = &parent_node->rb_right;
    }

    rb_link_node(&new->rbnode, parent_node, parent_ptr);
    rb_insert_color(&new->rbnode, &migration_tree);
}

static void fill_migration_queue(struct migration_buffer *buffer)
{
    unsigned long i, mfn;
    struct migration_query *query, *parent;

    for (i=0; i<buffer->size; i++)
    {
        mfn = buffer->migrations[i].pgid;
        query = find_migration_query(mfn);

        if ( query != NULL && query->mfn == mfn )
            continue;

        stats_account_migration_plan();

        if ( migration_alloc == monitor_enqueued )
            continue;

        parent = query;
        query = &migration_pool[migration_alloc];
        migration_alloc++;

        query->mfn = mfn;
        query->node = buffer->migrations[i].node;
        query->gfn = INVALID_GFN;
        query->domain = NULL;
        query->tries = 0;

        insert_migration_query(query, parent);
    }
}

static void gc_migration_queue(void)
{
    unsigned long i = 0, j = migration_alloc;

    while ( i < j )
        if ( migration_pool[i].mfn == INVALID_MFN )
        {
            while ( j > i && migration_pool[j-1].mfn == INVALID_MFN )
                j--;

            if ( i == j )
                break;

            j--;

            migration_pool[i] = migration_pool[j];
            rb_replace_node(&migration_pool[j].rbnode,
                            &migration_pool[i].rbnode, &migration_tree);
        }
        else
        {
            i++;
        }

    migration_alloc = j;
}

static void drain_migration_queue(void)
{
    struct migration_query *query;
    unsigned long i, j, nid, tmp;
    unsigned long cnt, mask;
    int ret;

    for (i=0; i<migration_alloc; i++)
    {
        query = &migration_pool[i];

        nid = phys_to_nid(query->mfn << PAGE_SHIFT);
        if ( query->node == nid )
        {
            register_page_moved(query->mfn);
            stats_account_migration_nomove();
            goto garbage;
        }

        if ( query->gfn == INVALID_GFN )
        {
            if ( ++(query->tries) >= monitor_maxtries )
            {
                stats_account_migration_abort();
                goto garbage;
            }
            continue;
        }

        cnt = 1 << monitor_order;
        mask = ~(cnt - 1);

        for (j=0; j<cnt; j++)
        {
            tmp = (query->gfn & mask) + j;

            ret = memory_move(query->domain, tmp, query->node);
            stats_account_migration_try(ret);

            tmp = (query->mfn & mask) + j;
            mstats_memory_moved(tmp);
        }

        register_page_moved(query->mfn);

     garbage:
        rb_erase(&query->rbnode, &migration_tree);
        query->mfn = INVALID_MFN;
    }

    gc_migration_queue();
}


int decide_migration(void)
{
    int cpu;
    struct migration_buffer *buffer;

    if ( !monitoring_started )
        return -1;

    for_each_online_cpu ( cpu )
        while ( cmpxchg(&per_cpu(migration_engine_owner, cpu), OWNER_NONE,
                        OWNER_DECIDER) != OWNER_NONE )
            ;

    stats_start_decision();
    buffer = refill_migration_buffer();
    fill_migration_queue(buffer);
    stats_stop_decision();

    for_each_online_cpu ( cpu )
        cmpxchg(&per_cpu(migration_engine_owner, cpu), OWNER_DECIDER,
                OWNER_NONE);
    return 0;
}

int perform_migration(void)
{
    int cpu;

    if ( !monitoring_started )
        return -1;

    for_each_online_cpu ( cpu )
        while ( cmpxchg(&per_cpu(migration_engine_owner, cpu), OWNER_NONE,
                        OWNER_MIGRATOR) != OWNER_NONE )
            ;

    stats_start_migration();
    drain_migration_queue();
    stats_stop_migration();

    for_each_online_cpu ( cpu )
        cmpxchg(&per_cpu(migration_engine_owner, cpu), OWNER_MIGRATOR,
                OWNER_NONE);
    return 0;
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
    struct migration_buffer *buffer;
    unsigned long i;

    alloc_migration_engine(4, 6, 4);
    init_migration_engine();
    param_migration_engine(75, 8, 0);

    refill_migration_buffer();

    register_page_access_cpu(42, 0);
    register_page_access_cpu(23, 0);
    register_page_access_cpu(42, 0);
    register_page_access_cpu(42, 0);

    register_page_access_cpu(18, 1);

    register_page_access_cpu(17, 2);
    register_page_access_cpu(42, 2);

    register_page_access_cpu(18, 3);
    register_page_access_cpu(18, 3);
    register_page_access_cpu(18, 3);
    register_page_access_cpu(23, 3);
    register_page_access_cpu(23, 3);
    register_page_access_cpu(23, 3);

    buffer = refill_migration_buffer();
    buffer->migrations[0].node = 1;
    buffer->migrations[2].node = 2;

    for (i=0; i<buffer->size; i++)
        printk("migration of %lu to %u\n", buffer->migrations[i].pgid,
               buffer->migrations[i].node);

    fill_migration_queue(buffer);
    drain_migration_queue();
    fill_migration_queue(buffer);

    free_migration_engine();

    mstats_memory_access(48);
    mstats_memory_access(1031);
    mstats_memory_access(4124);

    mstats_cache_access(48);
    mstats_cache_access(3445);
    mstats_cache_access(7564);

    mstats_memory_moved(3453);
    mstats_memory_moved(8343);

    /* pebs_disable(); */
    /* pebs_release(); */
}

static void ibs_nmi_handler(struct ibs_record *record)
{
    unsigned long vaddr, gfn, ogfn, mfn;
    struct migration_query *query;
    uint32_t pfec;

    if ( cmpxchg(&this_cpu(migration_engine_owner), OWNER_NONE,
                 OWNER_SAMPLER) != OWNER_NONE )
        return;

    stats_start_sampling();

    if ( !(record->record_mode & IBS_RECORD_MODE_OP) )
        goto out;
    if ( !(record->record_mode & IBS_RECORD_MODE_DPA) )
        goto out;
    if ( current->domain->domain_id >= DOMID_FIRST_RESERVED )
        goto out;
    if ( current->domain->guest_type != guest_type_hvm )
        goto out;

    vaddr = record->data_linear_address;
    mfn = record->data_physical_address >> PAGE_SHIFT;
    mfn &= ~((1 << monitor_order) - 1);

    if ( record->cache_infos & IBS_RECORD_DCMISS )
        mstats_memory_access(mfn);
    else
        mstats_cache_access(mfn);

    query = find_migration_query(mfn);
    if ( query != NULL && query->mfn == mfn && query->gfn == INVALID_GFN )
    {

        /*
         * This lines reject the sample if the current cpu is performing an
         * asynchronous context switch.
         * If so, we are likely going to receive an IPI during the call to
         * try_paging_gva_to_gfn() for performing remote TLB flush.
         * This will trigger a synchronous context_switch while we are looking
         * the guest page table, which would be fatal.
         */

        if ( this_cpu(curr_vcpu) != current )
            goto out;

        local_irq_enable();
        pfec = PFEC_page_present;

        stats_start_probing();
        gfn = try_paging_gva_to_gfn(current, vaddr, &pfec);
        stats_stop_probing();

        local_irq_disable();

        ogfn = INVALID_GFN;
        if ( cmpxchg(&query->gfn, ogfn, gfn) == ogfn )
            query->domain = current->domain;
    }

    stats_start_accounting();
    register_page_access(mfn);
    stats_stop_accounting();

out:
    stats_stop_sampling();
    cmpxchg(&this_cpu(migration_engine_owner), OWNER_SAMPLER, OWNER_NONE);
}

static int enable_monitoring_ibs(void)
{
    int ret;

    ret = ibs_acquire();
    if ( ret )
        return ret;

    ibs_setevent(IBS_EVENT_OP);
    ibs_setrate(monitor_rate);
    ibs_sethandler(ibs_nmi_handler);
    ibs_enable();

    return 0;
}

static void disable_monitoring_ibs(void)
{
    ibs_disable();
    ibs_release();
}


int monitor_migration_settracked(unsigned long tracked)
{
    int restart = monitoring_started;

    stop_monitoring();
    monitor_tracked = tracked;

    if ( restart )
        return start_monitoring();
    return 0;
}

int monitor_migration_setcandidate(unsigned long candidate)
{
    int restart = monitoring_started;

    stop_monitoring();
    monitor_candidate = candidate;

    if ( restart )
        return start_monitoring();
    return 0;
}

int monitor_migration_setenqueued(unsigned long enqueued)
{
    int restart = monitoring_started;

    stop_monitoring();
    monitor_enqueued = enqueued;

    if ( restart )
        return start_monitoring();
    return 0;
}

int monitor_migration_setscores(unsigned int enter, unsigned int increment,
                                unsigned int decrement, unsigned int maximum)
{
    monitor_enter = enter;
    monitor_increment = increment;
    monitor_decrement = decrement;
    monitor_maximum = maximum;

    if ( monitoring_started )
        param_migration_lists(enter, increment, decrement, maximum);

    return 0;
}

int monitor_migration_setcriterias(unsigned int min_node_score,
                                   unsigned int min_node_rate,
                                   unsigned char flush_after_refill)
{
    monitor_min_node_score = min_node_score;
    monitor_min_node_rate = min_node_rate;
    monitor_flush_after_refill = flush_after_refill;

    if ( monitoring_started )
        param_migration_engine(min_node_score, min_node_rate,
                               flush_after_refill);

    return 0;
}

int monitor_migration_setrules(unsigned int maxtries)
{
    monitor_maxtries = maxtries;
    return 0;
}

int monitor_migration_setrate(unsigned long rate)
{
    monitor_rate = rate;

    if ( monitoring_started )
    {
        if ( ibs_capable() )
            ibs_setrate(rate);
        else if ( pebs_capable() )
            ;
        else
            return -1;
        return 0;
    }

    return 0;
}

int monitor_migration_setorder(unsigned long order)
{
    int restart = monitoring_started;

    stop_monitoring();
    monitor_order = order;

    if ( restart )
        return start_monitoring();
    return 0;
}


int start_monitoring(void)
{
    if ( monitoring_started )
        return -1;

    stats_reset();
    if ( mstats_reset() != 0 )
        goto err;

    if ( alloc_migration_queue() != 0 )
        goto err;
    if ( alloc_migration_engine(monitor_tracked, monitor_candidate,
                                monitor_enqueued) != 0 )
        goto err_queue;

    init_migration_queue();
    init_migration_engine();

    param_migration_lists(monitor_enter, monitor_increment,
                          monitor_decrement, monitor_maximum);
    param_migration_engine(monitor_min_node_rate, monitor_min_node_score,
                           monitor_flush_after_refill);

    if ( ibs_capable() && enable_monitoring_ibs() == 0 )
        goto out;
    if ( pebs_capable() && enable_monitoring_pebs() == 0 )
        goto out;
    goto err_engine;

out:
    monitoring_started = 1;
    stats_start();
    return 0;
err_engine:
    free_migration_engine();
err_queue:
    free_migration_queue();
err:
    return -1;
}

void stop_monitoring(void)
{
    if ( !monitoring_started )
        return;

    stats_end();

    if ( ibs_capable() )
        disable_monitoring_ibs();
    else if ( pebs_capable() )
        disable_monitoring_pebs();

    monitoring_started = 0;

    free_migration_engine();
    free_migration_queue();

    stats_display();
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
