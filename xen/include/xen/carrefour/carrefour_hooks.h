#ifndef __LINUX_CARREFOUR_HOOKS_H
#define __LINUX_CARREFOUR_HOOKS_H


#include <xen/percpu.h>
#include <xen/types.h>


struct carrefour_options_t {
   int page_bouncing_fix_4k;
   int page_bouncing_fix_2M;
   int sync_thp_migration;
   int async_4k_migrations;
   int throttle_4k_migrations_limit; // In percent, 0 = no limit
   int throttle_2M_migrations_limit; // In percent, 0 = no limit
};
extern struct carrefour_options_t carrefour_options;

struct carrefour_hook_stats_t {
   u64 time_spent_in_migration_4k;
   u64 nr_4k_migrations;

   u64 time_spent_in_migration_2M;
   u64 nr_2M_migrations;

   u64 time_spent_in_split;
   u64 split_nb_calls;
};

extern struct carrefour_hook_stats_t carrefour_hook_stats;
/* extern rwlock_t carrefour_hook_stats_lock; */

struct carrefour_migration_stats_t {
   u64 time_spent_in_migration_4k;
   u64 nr_4k_migrations;

   u64 time_spent_in_migration_2M;
   u64 nr_2M_migrations;
};
DECLARE_PER_CPU(struct carrefour_migration_stats_t, carrefour_migration_stats);

#define INCR_MIGR_STAT_VALUE(type, time, nr) { \
   struct carrefour_migration_stats_t* stats; \
   read_lock(&carrefour_hook_stats_lock); \
   stats = get_cpu_ptr(&carrefour_migration_stats); \
   stats->time_spent_in_migration_##type += (time); \
   stats->nr_##type##_migrations += (nr); \
   put_cpu_ptr(&carrefour_migration_stats); \
   read_unlock(&carrefour_hook_stats_lock); \
}

enum thp_states{
   THP_DISABLED,
   THP_ALWAYS,
   THP_MADVISE
};

// These are our custom errors
#define EPAGENOTFOUND   65
#define EREPLICATEDPAGE 66
#define EINVALIDPAGE    67
#define ENOTMISPLACED   68
#define EBOUNCINGFIX    69

/* int is_shm(struct vm_area_struct *vma); */

// Returns 0 if the page is present, -<error> otherwise
// If the page is a regular huge page huge = 1, huge = 2 if it is a THP, huge = 0 otherwise
int page_status_for_carrefour(int pid, unsigned long addr, int * alread_treated, int * huge);

// -1 if the address is invalid, 0 if regular, 1 if trans_huge
int is_huge_addr_sloppy (int domain, unsigned long addr);

int s_migrate_pages(int domain, unsigned long nr_pages, void ** pages, int * nodes, int throttle);
int s_migrate_hugepages(int domain, unsigned long nr_pages, void ** pages, int * nodes);
int find_and_migrate_thp(int domain, unsigned long addr, int to_node);

// Returns 0 if we found and splitted a huge page
int find_and_split_thp(int domain, unsigned long addr);

int move_thread_to_node(int domain, int vcpu, int node);
/* struct task_struct * get_task_struct_from_pid(int pid); */

/* int is_valid_pid(int pid); */

/* void reset_carrefour_stats(void); */
/* struct carrefour_hook_stats_t get_carrefour_hook_stats(void); */

/* void reset_carrefour_hooks(void); */
/* void configure_carrefour_hooks(struct carrefour_options_t options); */
/* struct carrefour_options_t get_carrefour_hooks_conf(void); */

/* enum thp_states get_thp_state(void); */
/* void set_thp_state(enum thp_states state); */

/* unsigned migration_allowed_2M(void); */
/* unsigned migration_allowed_4k(void); */

/* typedef void (*migration_callback_t)(struct mm_struct * mm, unsigned long addr); */
/* extern migration_callback_t migration_callback; */

#endif
