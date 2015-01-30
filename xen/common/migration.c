#include <xen/hotlist.h>
#include <xen/migration.h>
#include <xen/mm.h>
#include <xen/numa.h>
#include <xen/rbtree.h>
#include <xen/sort.h>


#define DEFAULT_MINIMUM_RATE                 90
#define DEFAULT_MINIMUM_SCORE                64
#define DEFAULT_FLUSH_AFTER_REFILL            0


struct migration_candidate
{
    unsigned long   pgid;    /* id of the candidate page */
    unsigned int    dest;    /* destination node of the candidate */
    unsigned int    score;   /* score of the candidate destination */
    unsigned char   rate;    /* access rate of the destination on the page */
    struct rb_node  node;    /* rbtree node of the candidate */
};

struct heap_slot
{
    int                    cpu;     /* cpu of the hotlist */
    struct hotlist        *list;    /* hotlist */
    struct hotlist_entry  *entry;   /* current entry of the hotlist */
};


/* the hotlist of each cpu tracking page accesses */
static DEFINE_PER_CPU(struct hotlist, hotlist);

/*
 * a maximum heap for the hotlist sorted with the score of the not yet
 * processed hottest entry of the list
 */
static struct heap_slot hotlist_heap[NR_CPUS];

/* the size of the heap: count of remaining list */
static unsigned long hotlist_heap_size;


/* the memory pool of candidates to migration */
static struct migration_candidate *pool;

/* the size of the memory pool */
static unsigned long pool_capacity;

/* the actually used entry count in the memory pool */
static unsigned long pool_size;

/* the root of the candidate tree over the candidates int the memory pool */
static struct rb_root candidate_tree;


/* the migration buffer of the engine */
static struct migration_buffer buffer;

/* the migration buffer size */
static unsigned long buffer_capacity;

/* the minimum rate required to migrate */
static unsigned int minimum_rate;

/* the minimum score required to migrate */
static unsigned char minimum_score;

/* a flag indicating the hotlists need to be flushed after a refill */
static unsigned char flush_after_refill;


#ifdef BIGOS_MORE_STATS

static void stats_print_hotlists_quartiles(void)
{
    int cpu;
    struct hotlist *list;
    struct hotlist_entry *entry;
    unsigned long index, size;
    unsigned int minimum;
    unsigned int lowquart;
    unsigned int median;
    unsigned int hightquart;
    unsigned int maximum;

    for_each_online_cpu ( cpu )
    {
        list = &per_cpu(hotlist, cpu);
        entry = hottest_entry(list);
        size = 0;

        while ( entry != NULL )
        {
            size++;
            entry = cooler_entry(list, entry);
        }

        minimum = 0;
        lowquart = 0;
        median = 0;
        hightquart = 0;
        maximum = 0;

        entry = hottest_entry(list);
        index = 0;

        while ( entry != NULL )
        {
            if ( index == size-1 )
                minimum = entry_score(list, entry);
            if ( index == (size / 4) * 3 )
                lowquart = entry_score(list, entry);
            if ( index == size / 2 )
                median = entry_score(list, entry);
            if ( index == size / 4 )
                hightquart = entry_score(list, entry);
            if ( index == 0 )
                maximum = entry_score(list, entry);

            index++;
            entry = cooler_entry(list, entry);
        }

        if ( minimum != 0 && maximum != 0 )
            printk("hotlist[%2d] :: %u -- %u [ %u ] %u -- %u\n",
                   cpu, minimum, lowquart, median, hightquart, maximum);
    }
}

static void stats_print_pool_quartiles(void)
{
    unsigned int minimum;
    unsigned int lowquart;
    unsigned int median;
    unsigned int hightquart;
    unsigned int maximum;

    if ( pool_size == 0 )
        return;

    minimum = pool[pool_size - 1].score;
    lowquart = pool[(pool_size / 4) * 3].score;
    median = pool[pool_size / 2].score;
    hightquart = pool[pool_size / 4].score;
    maximum = pool[0].score;

    if ( minimum != 0 && maximum != 0 )
        printk("pool scores :: %u -- %u [ %u ] %u -- %u\n",
               minimum, lowquart, median, hightquart, maximum);

    minimum = pool[pool_size - 1].rate;
    lowquart = pool[(pool_size / 4) * 3].rate;
    median = pool[pool_size / 2].rate;
    hightquart = pool[pool_size / 4].rate;
    maximum = pool[0].rate;

    if ( minimum != 0 && maximum != 0 )
        printk("pool rates  :: %u -- %u [ %u ] %u -- %u\n",
               minimum, lowquart, median, hightquart, maximum);
}

#else

#define stats_print_hotlists_quartiles()     {}
#define stats_print_pool_quartiles()         {}

#endif /* ifdef BIGOS_MORE_STATS */


/*
 * Return the allocation order (to be passed to alloc_xenheap_pages)
 * corresponding to the given size (in bytes).
 */
static unsigned long allocation_order(unsigned long size)
{
	unsigned long order = 0;

	while ( 1ul << (order + PAGE_SHIFT) < size )
		order++;
	return order;
}

/*
 * Allocate the candidate pool with the specified size.
 * This size should be equal or greater than the migration candidate count.
 * Return 0 in case of success.
 */
static int alloc_candidate_pool(unsigned long size)
{
    unsigned long order;
    int ret = 0;

    order = allocation_order(size * sizeof(struct migration_candidate));
    pool_capacity = 0;
    pool = alloc_xenheap_pages(order, 0);

    if ( pool != NULL )
        pool_capacity = size;
    else
        ret = -1;

    return ret;
}

/*
 * Free the memory used by the candidate pool.
 */
static void free_candidate_pool(void)
{
    unsigned long order;

    if ( pool_capacity == 0 )
        return;
    order = allocation_order(pool_capacity*sizeof(struct migration_candidate));

    free_xenheap_pages(pool, order);
    pool_capacity = 0;
}

/*
 * Allocate the migration entry array of the migration buffer with the
 * specified size.
 * This size should be equal or greater than the amount of simultaneously
 * decided migration.
 * Return 0 in case of success.
 */
static int alloc_buffer(unsigned long size)
{
    unsigned long order;
    int ret = 0;

    order = allocation_order(size * sizeof(struct migration_entry));
    buffer_capacity = 0;
    buffer.migrations = alloc_xenheap_pages(order, 0);

    if ( buffer.migrations != NULL )
        buffer_capacity = size;
    else
        ret = -1;

    return ret;
}

/*
 * Free the memory used by the migration buffer.
 */
static void free_buffer(void)
{
    unsigned long order;

    if ( buffer_capacity == 0 )
        return;
    order = allocation_order(buffer_capacity * sizeof(struct migration_entry));

    free_xenheap_pages(buffer.migrations, order);
    buffer_capacity = 0;
}

int alloc_migration_engine(unsigned long tracked, unsigned long candidate,
                           unsigned long buffer)
{
    int cpu, ret = 0;

    for_each_online_cpu ( cpu )
        if ( alloc_hotlist(&per_cpu(hotlist, cpu), tracked) != 0 )
            ret = -1;
    if ( ret != 0 )
        goto err;

    if ( alloc_candidate_pool(candidate) != 0 )
        goto err;
    if ( alloc_buffer(buffer) != 0 )
        goto err_pool;

    return 0;
err_pool:
    free_candidate_pool();
err:
    free_migration_engine();
    return -1;
}

void init_migration_engine(void)
{
    int cpu;

    for_each_online_cpu ( cpu )
        init_hotlist(&per_cpu(hotlist, cpu));

    buffer.size = 0;

    param_migration_engine(DEFAULT_MINIMUM_RATE, DEFAULT_MINIMUM_SCORE,
                           DEFAULT_FLUSH_AFTER_REFILL);
}

void param_migration_engine(unsigned char min_rate, unsigned int min_score,
                            unsigned char flush)
{
    minimum_rate = min_rate;
    minimum_score = min_score;
    flush_after_refill = flush;
}

void param_migration_lists(unsigned int score_insertion,
                           unsigned int score_increment,
                           unsigned int score_decrement,
                           unsigned int score_maximum)
{
    int cpu;

    for_each_online_cpu ( cpu )
        param_hotlist(&per_cpu(hotlist, cpu), score_insertion, score_increment,
                      score_decrement, score_maximum);
}

void free_migration_engine(void)
{
    int cpu;

    free_buffer();
    free_candidate_pool();

    for_each_online_cpu ( cpu )
        free_hotlist(&per_cpu(hotlist, cpu));
}


void register_page_access(unsigned long pgid)
{
    register_page_access_cpu(pgid, get_processor_id());
}

void register_page_access_cpu(unsigned long pgid, int cpu)
{
    touch_entry(&per_cpu(hotlist, cpu), pgid);
    gc_entries(&per_cpu(hotlist, cpu));
}

void register_page_moved(unsigned long pgid)
{
    int cpu;

    for_each_online_cpu ( cpu )
        forget_entry(&per_cpu(hotlist, cpu), pgid);
}


/*
 * Low-level heap manipulation primitives.
 * HEAP_PARENT, HEAP_LEFT and HEAP_RIGHT work exclusively on the heap indices
 * assuming the heap start at the index 1.
 * HEAP_SWAP perform a swap of two structures in memory.
 */
#define HEAP_PARENT(i)        ((i) >> 1)
#define HEAP_LEFT(i)          ((i) << 1)
#define HEAP_RIGHT(i)        (((i) << 1) | 1)
#define HEAP_SWAP(s, t)                         \
    {                                           \
        typeof(s) ____swap = s;                 \
        s = t;                                  \
        t = ____swap;                           \
    }

/*
 * Insert the data contained in the given slot in the hotlist heap.
 * This function assume the heap is not already full.
 * The specified slot can be reused after this function returns, the data are
 * deep copied.
 */
static void insert_hotlist_heap(const struct heap_slot *slot)
{
    struct heap_slot *arr = hotlist_heap - 1;        /* array start at 1 */
    unsigned long j, i = hotlist_heap_size + 1;      /* last array slot */
    unsigned int cur, par;

    arr[i] = *slot;
    while ( i > 1 )
    {
        j = HEAP_PARENT(i);
        cur = entry_score(arr[i].list, arr[i].entry);
        par = entry_score(arr[j].list, arr[j].entry);
        if ( cur <= par )
            break;

        HEAP_SWAP(arr[i], arr[j]);
        i = j;
    }

    hotlist_heap_size++;
}

/*
 * Handle a key decreasing of the head of the hotlist heap.
 * This method must be used after the entry of the heap head has changed, or
 * the heap may be corrupted.
 */
static void decrease_hotlist_heap(void)
{
    struct heap_slot *arr = hotlist_heap - 1;
    unsigned long l, r, j, i = 1;
    unsigned int cur, chl, sl, sr;

    while ( (l = HEAP_LEFT(i)) <= hotlist_heap_size )
    {
        cur = entry_score(arr[i].list, arr[i].entry);

        if ( (r = HEAP_RIGHT(i)) <= hotlist_heap_size )
        {
            sl = entry_score(arr[l].list, arr[l].entry);
            sr = entry_score(arr[r].list, arr[r].entry);
            j   = (sl >= sr) ?  l :  r;
            chl = (sl >= sr) ? sl : sr;
        }
        else
        {
            j   = l;
            chl = entry_score(arr[l].list, arr[l].entry);
        }

        if ( chl <= cur )
            break;

        HEAP_SWAP(arr[i], arr[j]);
        i = j;
    }
}

/*
 * Initialize the hotlist heap by filling it with the hottest entries of each
 * non-empty hotlist.
 * This function set the hotlist heap size accordingly.
 */
static void init_hotlist_heap(void)
{
    struct heap_slot slot;

    hotlist_heap_size = 0;
    for_each_online_cpu ( slot.cpu )
    {
        slot.list = &per_cpu(hotlist, slot.cpu);
        slot.entry = hottest_entry(slot.list);
        if ( slot.entry == NULL )
            continue;
        insert_hotlist_heap(&slot);
    }
}

/*
 * Pop an entry from the heap.
 * Take the head of the heap, and replace it by the cooler entry of the same
 * hotlist, then ensure the heap is still a heap.
 * This function allows to look the next hottest entry in all the hotlist and
 * place it at the head of the heap.
 * This function assume the heap is not already empty.
 */
static void pop_hotlist_heap(void)
{
    struct heap_slot *slot = &hotlist_heap[0];

    slot->entry = cooler_entry(slot->list, slot->entry);

    if ( slot->entry == NULL )
    {
        hotlist_heap_size--;
        SWAP(*slot, hotlist_heap[hotlist_heap_size]);
    }

    if ( hotlist_heap_size > 0 )
        decrease_hotlist_heap();
}


/*
 * Initialize the candidate tree and memory pool.
 * The candidate tree become an empty tree and every candidates in the pool is
 * cleaned.
 */
static void init_candidate_tree(void)
{
    unsigned long i;

    candidate_tree = RB_ROOT;

    for (i=0; i<pool_capacity; i++)
        RB_CLEAR_NODE(&pool[i].node);

    pool_size = 0;
}

/*
 * Allocate a candidate from unused entries in the candidate memory pool, then
 * set the candidate pgid accordingly to the specified one.
 * This function assume les than pool_capacity candidates have been allocated.
 * There is no free_candidate() function, except init_candidate_tree() which
 * free every allocated candidates.
 * Return the newly allocated candidate.
 */
static struct migration_candidate *alloc_candidate(unsigned long pgid)
{
    struct migration_candidate *new = &pool[pool_size];

    new->pgid = pgid;
    new->dest = 0;
    new->score = 0;
    new->rate = 0;

    pool_size++;
    return new;
}

/*
 * Find a candidate in the candidate tree, searching with its pgid.
 * If the searched candidate is not in the candidate tree, return the parent
 * the searched candidate should be inserted below, or NULL if the candidate
 * tree is empty.
 * The user can know the searched candidate has been found by checking if it
 * is not NULL and comparing the returned candidate pgid with the looked for
 * one.
 */
static struct migration_candidate *find_candidate(unsigned long pgid)
{
    struct rb_node *node = candidate_tree.rb_node;
    struct migration_candidate *candidate = NULL;

    while ( node )
    {
        candidate = container_of(node, struct migration_candidate, node);

        if ( pgid < candidate->pgid )
            node = node->rb_left;
        else if ( pgid > candidate->pgid )
            node = node->rb_right;
        else
            break;
    }

    return candidate;
}

/*
 * Collect informations about the specified candidate across all the hotlists.
 * The informations are, for the node with the maximum access rate: the score
 * and the access rate of this node.
 * The score is the sum of the scores of every cpus in this node, and the rate
 * is a percentage of this score compared to the percentage of the other nodes
 * score.
 * These informations are set in the candidate structure.
 */
static void inquire_candidate(struct migration_candidate *candidate)
{
    int cpu, node, max_node = -1;
    unsigned int tmp, scores[NR_CPUS];
    unsigned long ulong, total = 0;
    struct hotlist_entry *entry;
    struct hotlist *list;

    for_each_online_cpu ( cpu )
        scores[cpu] = 0;

    candidate->dest = 0;

    for_each_online_cpu ( cpu )
    {
        list = &per_cpu(hotlist, cpu);
        entry = pgid_entry(list, candidate->pgid);
        if ( entry == NULL )
            continue;

        node = cpu_to_node(cpu);
        tmp = entry_score(list, entry);

        if ( max_node == -1 )
            candidate->dest = node;
        if ( node > max_node )
            max_node = node;

        total += tmp;
        scores[node] += tmp;
        if ( scores[node] > scores[candidate->dest] )
            candidate->dest = node;
    }

    ulong = scores[candidate->dest];
    total += !total;

    candidate->score = ulong;
    candidate->rate = (unsigned char) ((ulong * 100) / total);
}

/*
 * Insert a new candidate in the candidate tree, which is indexed by the pgid.
 * The new candidate is inserted as the child of the specified parent
 * candidate which should be obtained with find_candidate().
 * Be carefull to not rebalance the tree between the call to find_candidate()
 * and the call to insert_candidate(), otherwise, the tree may be corrupted.
 */
static void insert_candidate(struct migration_candidate *parent,
                             struct migration_candidate *new)
{
    struct rb_node *parent_node, **parent_ptr;

    if ( unlikely(parent == NULL) )
    {
        parent_node = NULL;
        parent_ptr = &candidate_tree.rb_node;
    }
    else
    {
        parent_node = &parent->node;
        if ( new->pgid < parent->pgid )
            parent_ptr = &parent_node->rb_left;
        else
            parent_ptr = &parent_node->rb_right;
    }

    rb_link_node(&new->node, parent_node, parent_ptr);
    rb_insert_color(&new->node, &candidate_tree);
}

/*
 * Update the candidate tree, and the candidate pool, accordingly to the
 * hotlist content.
 * The candidates inserted in the tree are, the entries of the lists having the
 * best score. If an entry is present in more than one list, it is inseted
 * only once.
 */
static void refill_candidate_tree(void)
{
    unsigned long i, pgid;
    struct migration_candidate *candidate, *parent;

    init_hotlist_heap();
    init_candidate_tree();

    i = 0;
    while ( (i < pool_capacity) && (hotlist_heap_size > 0) )
    {
        pgid = entry_pgid(hotlist_heap[0].list, hotlist_heap[0].entry);
        candidate = find_candidate(pgid);
        if ( candidate != NULL && candidate->pgid == pgid )
            goto next;

        parent = candidate;
        candidate = alloc_candidate(pgid);
        inquire_candidate(candidate);
        insert_candidate(parent, candidate);

        i++;
    next:
        pop_hotlist_heap();
    }
}

/*
 * Compare two candidates besing on their rate then score.
 * Return a negative number if _a is more interesting to migrate than _b.
 * Return a positive number if _b is more interesting to migrate than _a.
 */
static int compare_candidates(const void *_a, const void *_b)
{
    struct migration_candidate *a = (struct migration_candidate *) _a;
    struct migration_candidate *b = (struct migration_candidate *) _b;

    if ( a->rate != b->rate )
        return b->rate - a->rate;
    return b->score - a->score;
}

/*
 * Flush every cpu hotlist.
 */
static void flush_hotlists(void)
{
    int cpu;

    for_each_online_cpu ( cpu )
    {
        flush_entries(&per_cpu(hotlist, cpu));
        gc_entries(&per_cpu(hotlist, cpu));
    }
}

struct migration_buffer *refill_migration_buffer(void)
{
    unsigned long i;

    stats_print_hotlists_quartiles();

    refill_candidate_tree();
    sort(pool, pool_size, sizeof(struct migration_candidate),
         compare_candidates, NULL);

    stats_print_pool_quartiles();

    buffer.size = 0;
    for (i=0; i<pool_size; i++)
    {
        if ( buffer.size >= buffer_capacity )
            break;
        if ( pool[i].score < minimum_score )
            continue;
        if ( pool[i].rate < minimum_rate )
            break;

        buffer.migrations[buffer.size].pgid = pool[i].pgid;
        buffer.migrations[buffer.size].node = pool[i].dest;
        buffer.size++;
    }

    if ( flush_after_refill )
        flush_hotlists();

	return &buffer;
}

struct migration_buffer *get_migration_buffer(void)
{
	return &buffer;
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
