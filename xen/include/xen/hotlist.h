#ifndef __HOTLIST_H__
#define __HOTLIST_H__


/*
 * A hotlist which is basically an LRU implementation.
 * This structure is not reentrant, thread-safe or interrupt-safe.
 * Once the structure is allocated, there is not more allocation needed, so
 * it can be safely used in the context of an interrupt handler.
 */


#include <xen/list.h>
#include <xen/rbtree.h>


/*
 * These are the structure used in the hotlist.
 * Do not use their fields directly, instead, use the accessor functions.
 */

struct hotlist_entry
{
	unsigned long     pgid;         /* id for the tracked page */
	unsigned int      score;        /* score of the tracked page */
	struct list_head  list;         /* either hotlist or freelist */
	struct rb_node    node;         /* either empty or pgid tree */
};

struct hotlist
{
	struct hotlist_entry  *pool;        /* memory pool of entries */
	struct list_head       free;        /* freelist of entries, never empty */
	struct list_head       list;        /* hotlist of tracked pages */
	struct rb_root         root;        /* root of pgid tree */
	unsigned int           score;       /* score base of the list */
	unsigned long          size;        /* size of the allocated pool */
    unsigned int           insertion;   /* score_insertion parameter */
    unsigned int           increment;   /* score_increment parameter */
    unsigned int           decrement;   /* score_decrement parameter */
    unsigned int           maximum;     /* score_maximum parameter */
};


/*
 * Allocate the memory for the specified hotlist with the given size (the
 * amount of entries). Actually, the amount of usable entries is (size - 1)
 * because the hotlist always keeps a free entry for insertions.
 * The size must be strictly greater than 1.
 * Return 0 on success.
 */
int alloc_hotlist(struct hotlist *list, unsigned long size);

/*
 * Initialize the specified hotlist. This should be called right after the
 * allocation.
 * This function can be used to reset the hotlist.
 */
void init_hotlist(struct hotlist *list);

/*
 * Set the parameters of the specified hotlist, controlling the accounting of
 * the list.
 * The score_insertion parameter is the score of an entry immediately after
 * being touched for the first time.
 * The score_increment parameter is the score added to an entry when it already
 * is in the hotlist and being touched.
 * The score_decrement parameter is the score every non touched entry lose
 * each time an entry is touched.
 * The score_maximum parameter is the maximum score of an entry.
 */
void param_hotlist(struct hotlist *list, unsigned int score_insertion,
                   unsigned int score_increment, unsigned int score_decrement,
                   unsigned int score_maximum);

/*
 * Free the memory used for the specified hotlist.
 */
void free_hotlist(struct hotlist *list);


/*
 * Touch an entry with the specified pgid in the specified hotlist. A pgid can
 * be anything which can be used to identify the page, for instance, its mfn.
 */
void touch_entry(struct hotlist *list, unsigned long pgid);

/*
 * Forget (remove) the specified pgid in the specified hotlist.
 * This function has no effect if the pgid is not in the list.
 */
void forget_entry(struct hotlist *list, unsigned long pgid);

/*
 * Set the score of every entries in the list to 0.
 * Keep the relative score of the entries.
 * Prefer to use this function instead of the slower init_hotlist().
 */
void flush_entries(struct hotlist *list);

/*
 * Garbage collect every entries in the list with a score of 0.
 * This call can takes time if there is many such entries, but may speed up
 * future calls to touch_entry() and pgid_entry().
 */
void gc_entries(struct hotlist *list);


/*
 * Return the entry of the specified hotlist which has the specified pgid.
 * Return NULL if no entry in the list has the pgid.
 */
struct hotlist_entry *pgid_entry(struct hotlist *list, unsigned long pgid);

/*
 * Return the entry of the specified list which has the best score (it has
 * recently been touched the most).
 * Return NULL if the hotlist is empty.
 */
struct hotlist_entry *hottest_entry(struct hotlist *list);

/*
 * Return the entry of the specified hotlist which is immediately cooler
 * (has a worse score) than the specified entry.
 * Return NULL if the specified entry is the coolest in the hotlist.
 */
struct hotlist_entry *cooler_entry(struct hotlist *list,
				   struct hotlist_entry *entry);


/*
 * Return the pgid of the entry.
 */
static inline unsigned long entry_pgid(struct hotlist *list
                                       __attribute__((unused)),
                                       struct hotlist_entry *entry)
{
    return entry->pgid;
}

/*
 * Return the score of an entry inside the hotlist.
 */
static inline unsigned int entry_score(struct hotlist *list,
                                       struct hotlist_entry *entry)
{
	if ( list->score > entry->score )
		return 0;
    return entry->score - list->score;
}

/*
 * Return the relative score of an entry inside the hotlist.
 * The relative score allows to compare every entries in the hotlist, even
 * the ones with a score (as returned by entry_score()) of 0.
 * However, the relative scores can be non consistent between two calls to
 * touch_entry().
 */
static inline unsigned int entry_relative_score(struct hotlist *list
                                                __attribute__((unused)),
                                                struct hotlist_entry *entry)
{
    return entry->score;
}


#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
