#include <xen/hotlist.h>
#include <xen/migration.h>
#include <xen/mm.h>
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


static DEFINE_PER_CPU(struct hotlist, hotlist);

static struct heap_slot hotlist_heap[NR_CPUS];

static unsigned long hotlist_heap_size;


static struct migration_candidate *pool;

static unsigned long pool_capacity;

static unsigned long pool_size;

static struct rb_root candidate_tree;


static struct migration_buffer buffer;

static unsigned long buffer_capacity;

static unsigned int minimum_rate;

static unsigned char minimum_score;

static unsigned char flush_after_refill;


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


static void init_candidate_tree(void)
{
    unsigned long i;
    
    candidate_tree = RB_ROOT;

    for (i=0; i<pool_capacity; i++)
        RB_CLEAR_NODE(&pool[i].node);

    pool_size = 0;
}

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
        
        node = cpu; /* TODO: change */
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

static int compare_candidates(const void *_a, const void *_b)
{
    struct migration_candidate *a = (struct migration_candidate *) _a;
    struct migration_candidate *b = (struct migration_candidate *) _b;
    int a_valid_score = a->score >= minimum_score;
    int b_valid_score = b->score >= minimum_score;
    
    if ( a_valid_score != b_valid_score )
        return b_valid_score - a_valid_score;
    
    return b->rate - a->rate;
}


struct migration_buffer *refill_migration_buffer(void)
{
    unsigned long i;
    
    refill_candidate_tree();
    sort(pool, pool_size, sizeof(struct migration_candidate),
         compare_candidates, NULL);

    buffer.size = 0;
    for (i=0; i<pool_size; i++)
    {
        if ( buffer.size >= buffer_capacity )
            break;
        if ( pool[i].score < minimum_score )
            break;
        if ( pool[i].rate < minimum_rate )
            break;
        
        buffer.migrations[buffer.size].pgid = pool[i].pgid;
        buffer.migrations[buffer.size].node = pool[i].dest;
        buffer.size++;
    }
    
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
