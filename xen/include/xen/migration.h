#ifndef __MIGRATION_H__
#define __MIGRATION_H__


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


int alloc_migration_engine(unsigned long tracked, unsigned long candidate,
                           unsigned long buffer);

void init_migration_engine(void);

void param_migration_engine(unsigned char min_rate, unsigned int min_score,
                            unsigned char flush);

void param_migration_lists(unsigned int score_insertion,
                           unsigned int score_increment,
                           unsigned int score_decrement,
                           unsigned int score_maximum);

void free_migration_engine(void);


void register_page_access(unsigned long pgid);

void register_page_access_cpu(unsigned long pgid, int cpu);

void register_page_moved(unsigned long pgid);


struct migration_buffer *refill_migration_buffer(void);

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
