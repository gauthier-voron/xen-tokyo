#include <xen/hotlist.h>
#include <xen/mm.h>


#define DEFAULT_INSERTION     0
#define DEFAULT_INCREMENT     8
#define DEFAULT_DECREMENT     1
#define DEFAULT_MAXIMUM    1024


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


int alloc_hotlist(struct hotlist *list, unsigned long size)
{
	int ret = 0;
	unsigned long order;

    if ( size <= 1 )
        return -1;

    order = allocation_order(size * sizeof(struct hotlist_entry));
    list->size = 0;
	list->pool = alloc_xenheap_pages(order, 0);
    
	if ( list->pool != NULL )
		list->size = size;
	else
		ret = -1;

	return ret;
}

void init_hotlist(struct hotlist *list)
{
    unsigned long i;
    
	list->score = 0;
    INIT_LIST_HEAD(&list->free);
    INIT_LIST_HEAD(&list->list);
    list->root = RB_ROOT;

    for (i=0; i<list->size; i++)
    {
        list_add_tail(&list->pool[i].list, &list->free);
        RB_CLEAR_NODE(&list->pool[i].node);
    }

    param_hotlist(list, DEFAULT_INSERTION, DEFAULT_INCREMENT,
                  DEFAULT_DECREMENT, DEFAULT_MAXIMUM);
}

void param_hotlist(struct hotlist *list, unsigned int score_insertion,
                   unsigned int score_increment, unsigned int score_decrement,
                   unsigned int score_maximum)
{
    list->insertion = score_insertion;
    list->increment = score_increment;
    list->decrement = score_decrement;
    list->maximum = score_maximum;
}

void free_hotlist(struct hotlist *list)
{
	unsigned long order;

    if ( list->size == 0 )
        return;
	order = allocation_order(list->size * sizeof(struct hotlist_entry));
    
	free_xenheap_pages(list->pool, order);
    list->size = 0;
}


/*
 * Allocate an entry from the freelist, and initialize it with the specified
 * pgid.
 * The returned entry has a score equals to the hotlist score, an empty
 * rb_tree node and an undefined list node.
 * The returned entry is not in the freelist nor in the hotlist.
 * The freelist cannot be empty, and so, this function should always return
 * a valid entry pointer.
 */
static struct hotlist_entry *alloc_hotlist_entry(struct hotlist *list,
                                                 unsigned long pgid)
{
    struct hotlist_entry *new;
    struct list_head *elm;

    elm = list->free.next;
    list_del(elm);

    new = container_of(elm, struct hotlist_entry, list);
    new->pgid = pgid;
    new->score = list->score;

    return new;
}

/*
 * Free the specified entry from the hotlist, by removing it from the rbtree
 * and by moving it from the list to the freelist.
 * The entry must be in the tree and in the list.
 */
static void free_hotlist_entry(struct hotlist *list,
                               struct hotlist_entry *entry)
{
    list_move(&entry->list, &list->free);
    rb_erase(&entry->node, &list->root);
    RB_CLEAR_NODE(&entry->node);
}

/*
 * Ensure the freelist is not empty.
 * If so, takes the last entry of the list and free it.
 * This function assume if the freelist is empty, then the actual hotlist
 * is not.
 */
static void ensure_freelist_not_empty(struct hotlist *list)
{
    struct hotlist_entry *last;
    
    if ( !list_empty(&list->free) )
        return;

    last = container_of(list->list.prev, struct hotlist_entry, list);
    free_hotlist_entry(list, last);
}

/*
 * Ensure the hotlist does not overflow.
 * There is two way for the list to overflow: by the list->score, or by an
 * entry->score.
 * When one of them reach the overflow limit, this function substract the
 * list->score to each entry's score (with a floor of 0), and reset the
 * list->score to 0.
 */
static void ensure_no_overflow(struct hotlist *list)
{
    struct hotlist_entry *cur;
    int need = 0;

    /*
     * The limits are UINT_MAX - 1 - XXX
     * XXX is list->decrement for the list->score overflow
     * XXX is list->maximum for an entry->score overflow
     * We always substract at least 1 to handle cases where
     * list->decrement == 0 or list->maximum == 0
     */
    
    if ( list->score >= ((unsigned int) (0 - 1 - list->decrement)) )
        need++;
    if ( list->score >= ((unsigned int) (0 - 1 - list->maximum)) )
        need++;

    if ( !need )
        return;

    list_for_each_entry(cur, &list->list, list)
        if ( cur->score < list->score )
            cur->score = 0;
        else
            cur->score -= list->score;

    list->score = 0;
}


/*
 * Return the entry of the specified hotlist which has the specified pgid.
 * If the looked entry is not present, returns the parent node where the entry
 * should be inserted, or NULL if the hotlist is empty.
 */
static struct hotlist_entry *find_pgid_entry(struct hotlist *list,
                                             unsigned long pgid)
{
    struct rb_node *node = list->root.rb_node;
    struct hotlist_entry *entry = NULL;

    while ( node )
    {
        entry = container_of(node, struct hotlist_entry, node);

        if ( pgid < entry->pgid )
            node = node->rb_left;
        else if ( pgid > entry->pgid )
            node = node->rb_right;
        else
            break;
    }

    return entry;
}

/*
 * Insert the specified new entry in the specified hotlist tree, knowing it
 * should be inserted under the specified parent node.
 * The function may rebalance the tree, so the specified parent is not
 * necessary the parent of the specified new node when the function returns.
 */
static void insert_tree_entry(struct hotlist *list,
                              struct hotlist_entry *new,
                              struct hotlist_entry *parent)
{
    struct rb_node *parent_node, **parent_ptr;

    if ( unlikely(parent == NULL) )
    {
        parent_node = NULL;
        parent_ptr = &list->root.rb_node;
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
    rb_insert_color(&new->node, &list->root);
}

/*
 * Move the specified entry up in the hotlist, until it becomes the head, or
 * its predecessor has a score strictly greater then its own one.
 */
static void moveup_list_entry(struct hotlist *list,
                              struct hotlist_entry *entry)
{
    struct list_head *seek = entry->list.prev;
    struct hotlist_entry *temp;

    while ( seek != &list->list )
    {
        temp = container_of(seek, struct hotlist_entry, list);
        if ( temp->score > entry->score )
            break;
        seek = seek->prev;
    }

    if ( seek != entry->list.prev )
        list_move(&entry->list, seek);
}

/*
 * Insert the specified new entry in the specified hotlist.
 * The new entry should not be in any list when the function is called,
 * otherwise, the old list will be corrupted.
 */
static void insert_list_entry(struct hotlist *list,
                              struct hotlist_entry *new)
{
    struct list_head *cur;
    unsigned int score = new->score;
    
    list_for_each(cur, &list->list)
        if ( container_of(cur, struct hotlist_entry, list)->score <= score )
            break;
    
    list_add_tail(&new->list, cur);
}


void touch_entry(struct hotlist *list, unsigned long pgid)
{
    struct hotlist_entry *found = find_pgid_entry(list, pgid);
    struct hotlist_entry *parent;
    unsigned int ceil, add;

    list->score += list->decrement;
    ensure_no_overflow(list);
    
    if ( found == NULL || found->pgid != pgid )
    {
        parent = found;
        found = alloc_hotlist_entry(list, pgid);

        /*
         * Here, the newly allocated found entry is not in the freelist but
         * not yet in the hotlist or in the rbtree.
         */
        
        insert_tree_entry(list, found, parent);

        /*
         * Now the entry is in the rbtree. The next call to
         * ensure_freelist_not_empty() assume if the freelist is empty, then
         * the hotlist is not.
         * Because the hotlist has at least a size of two, we know this is
         * always true.
         */
        
        ensure_freelist_not_empty(list);

        found->score = list->score + list->insertion;
        insert_list_entry(list, found);
    }
    else
    {

        /*
         * Be carefull about overflows when increasing the score.
         * We know list->score + list->maximum does not overflow because of
         * the call to ensure_no_overflow().
         * Remeber list->score + list->maximum can be 0 (unlikely).
         */

        ceil = list->score + list->maximum;
        add = list->increment + list->decrement;
        
        if ( likely(ceil > add) && (found->score > ceil - add) )
            found->score = ceil;
        else
            found->score += add;
        
        moveup_list_entry(list, found);
    }
}

void forget_entry(struct hotlist *list, unsigned long pgid)
{
    struct hotlist_entry *found = find_pgid_entry(list, pgid);

    if ( found != NULL && found->pgid == pgid )
        free_hotlist_entry(list, found);
}


struct hotlist_entry *pgid_entry(struct hotlist *list, unsigned long pgid)
{
    struct hotlist_entry *entry = find_pgid_entry(list, pgid);

    if ( entry != NULL && entry->pgid != pgid )
        entry = NULL;
    return entry;
}

struct hotlist_entry *hottest_entry(struct hotlist *list)
{
    struct hotlist_entry *entry = NULL;

    if ( !list_empty(&list->list) )
    {
        entry = container_of(list->list.next, struct hotlist_entry, list);
        prefetch(entry->list.next);
    }
    
    return entry;
}

struct hotlist_entry *cooler_entry(struct hotlist *list,
                                   struct hotlist_entry *entry)
{
    struct hotlist_entry *next = NULL;

    if ( entry->list.next != &list->list )
    {
        next = container_of(entry->list.next, struct hotlist_entry, list);
        prefetch(next->list.next);
    }
    
    return next;
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
