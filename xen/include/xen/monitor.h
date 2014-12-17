#ifndef __MONITOR_H__
#define __MONITOR_H__


/*
 * Set the hotlist size. The specified size is the amount of page which can
 * be handled as hot pages for each cpus.
 * Changing the size of the hotlist cause the monitoring to stop and restart.
 * Return 0 in case of success.
 */
int monitor_hotlist_setsize(unsigned long size);

/*
 * Set the parameters for the hotlist accounting. The score_enter is the score
 * of a page which is inserted on the hotlist. The score_incr is the score
 * a page in the list gain when it is acceeded. The score_decr is the score
 * each page loose when a page is acceeded. The score_max is the maximum
 * local score a page can reach.
 */
void monitor_hotlist_setparm(unsigned int score_enter, unsigned int score_incr,
			     unsigned int score_decr, unsigned int score_max);


/*
 * Set the maximum amount of page which can be moved at each migration phase.
 * Changing this size cause the monitoring to stop and restart.
 * Return 0 in case of success.
 */
int monitor_migration_setsize(unsigned long size);

/*
 * Set the parameters of the migration phases. The cooldown is the minimum
 * amount of millisecond separating each migration phase. The min_local_score
 * is the minimum score a page have to reach in one of the cpu of the candidate
 * destination node to be moved. The min_local_rate is the minimum percentage
 * of access from the candidate node to be moved.
 */
void monitor_migration_setparm(unsigned long cooldown,
			       unsigned int min_local_score,
			       unsigned int min_local_rate);





int monitor_migration_settracked(unsigned long tracked);

int monitor_migration_setcandidate(unsigned long candidate);

int monitor_migration_setenqueued(unsigned long enqueued);

int monitor_migration_setscores(unsigned int enter, unsigned int increment,
				unsigned int decrement, unsigned int maximum);

int monitor_migration_setcriterias(unsigned int min_node_score,
				   unsigned int min_node_rate);

int monitor_migration_setrules(unsigned int maxtries);



int decide_migration(void);

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
