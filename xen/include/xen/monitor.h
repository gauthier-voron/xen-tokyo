#ifndef __MONITOR_H__
#define __MONITOR_H__


/*
 * Set the amount of pages which can be tracked for access simultaneously on
 * a given cpu.
 * Return 0 in case of success.
 */
int monitor_migration_settracked(unsigned long tracked);

/*
 * Set the amount of pages which can be inquired for migration across all the
 * cpus.
 * Return 0 in case of success.
 */
int monitor_migration_setcandidate(unsigned long candidate);

/*
 * Set the amount of pages which can be enqueued for migration waiting to get
 * migration-specific informations.
 * Return 0 in case of success.
 */
int monitor_migration_setenqueued(unsigned long enqueued);

/*
 * Set the hotlists score parameters for page migrations.
 * See param_migration_lists() in xen/migration.h for more details.
 * Return 0 in case of success.
 */
int monitor_migration_setscores(unsigned int enter, unsigned int increment,
				unsigned int decrement, unsigned int maximum);

/*
 * Set the migration engine score parameters for page migrations.
 * See param_migration_engine() un xen/migration.h for more details.
 * Return 0 in case of success.
 */
int monitor_migration_setcriterias(unsigned int min_node_score,
				   unsigned int min_node_rate,
				   unsigned char flush_after_refill);

/*
 * Set the amount of migration decision a given page can stay in the migration
 * queue without the migration be aborted.
 * Return 0 in case of success.
 */
int monitor_migration_setrules(unsigned int maxtries);

/*
 * Set the sampling rate of memory access.
 * More the rate is low, more the sampling are precise about what memory access
 * are done, but more important is the sampling overhead.
 * Return 0 in case of success.
 */
int monitor_migration_setrate(unsigned long rate);


/*
 * Perform a decision about what page to migrate and place these pages in a
 * migration queue.
 * Return 0 in case of success.
 */
int decide_migration(void);

/*
 * Perform actual migration basing on what have been decided previously.
 * Look in the migration queue for the pages planned which are ready.
 * Return 0 in case of success.
 */
int perform_migration(void);

/*
 * Start the monitoring of the system.
 * This allocate the memory necessary for the monitoring structure to work
 * then enable the hardware monitoring facilities.
 * Return 0 on success.
 */
int start_monitoring(void);

/*
 * Stop the monitoring of the system.
 * This disable the hardware monitoring facilities and free the memory
 * allocated by start_monitoring().
 */
void stop_monitoring(void);


#endif
