#ifndef __MIGRATION_H__
#define __MIGRATION_H__


/*
 * A migration engine which can track, on each cpu, what access are done to
 * what page, and then, compute what page should me migrated from a node to
 * another accordingly to its access rates.
 */


struct migration_entry
{
    unsigned long       pgid;           /* id of the page to move */
    unsigned int        node;           /* node to move the page on */
};

struct migration_buffer
{
    unsigned long            size;           /* actual size of the array */
    struct migration_entry  *migrations;     /* array of pages to move */
};


/*
 * Allocate the memory for the migration engine, and its subcomponents,
 * accordingly to the specified sizes. The tracked size is the maximum amount
 * of page which can be tracked in a hotlist per cpu. The candidate size is
 * the amount of page inquired for access rate per buffer refill. The buffer
 * size is the size of the migration buffer.
 * Return 0 in case of success.
 */
int alloc_migration_engine(unsigned long tracked, unsigned long candidate,
                           unsigned long buffer);

/*
 * Initialize the migration engine. This function should be called right after
 * the engine allocation.
 */
void init_migration_engine(void);

/*
 * Set various parameters about what page can be selected for migration.
 * When the buffer refill is performed. The min_rate parameter is the minimum
 * rate of local access on the destination node to be migrated. The min_score
 * is the minimum node score, the sum of the scores of the cpus in the node,
 * for a page to be migrated. If the flush parameter is 1, then the hotlists
 * are flushed after a buffer refill.
 */
void param_migration_engine(unsigned char min_rate, unsigned int min_score,
                            unsigned char flush);

/*
 * Set various parameters about how hotlist account for registerd pages.
 * The score_insertion is the score of a page when it enter in the hotlist.
 * The score_increment is the score added to the page score when the page is
 * already in the hotlist. The score_decrement is the score every pages, other
 * than the regster page lose on a registration. The score_maximum is the
 * maximum score a page can have.
 */
void param_migration_lists(unsigned int score_insertion,
                           unsigned int score_increment,
                           unsigned int score_decrement,
                           unsigned int score_maximum);

/*
 * Free the memory used by the migration engine and by its subcomponents.
 */
void free_migration_engine(void);


/*
 * Register a page has been acceeded on the current cpu. The page is specified
 * by a pgid, which can be anything which identify a unique page, typically,
 * its mfn.
 */
void register_page_access(unsigned long pgid);

/*
 * Register a page has been acceeded on the specified cpu. The page is
 * specified by a pgid, which can be anything which identify a unique page,
 * typically, its mfn.
 */
void register_page_access_cpu(unsigned long pgid, int cpu);

/*
 * Register a page has been actually migrated and so the migration engine has
 * to reset the access counts for this page.
 */
void register_page_moved(unsigned long pgid);


/*
 * Compute the pages which should be migrated, accordingly to the previous
 * calls to register_page_access(), and fill a migration buffer with them, then
 * return this buffer.
 * The migration buffer can be empty, having its size field to 0.
 */
struct migration_buffer *refill_migration_buffer(void);

/*
 * Return the migration buffer as its was returned by the last
 * refill_migration_buffer() call. This function does not re-compute anything.
 */
struct migration_buffer *get_migration_buffer(void);


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
