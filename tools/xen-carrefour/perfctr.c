#define __XOPEN_SOURCE 700

#include <stdlib.h>
#include <pthread.h>

#include "model.h"
#include "xencarrefour.h"


/* #define MAX_CPU_GROUP  16 */
#define BAD_SLOT      ((unsigned long) -1)

#define STATE_READY    0
#define STATE_RUN      1
#define STATE_WAIT     2


struct schedule_unit
{
	int                   *fds;                      /* file descriptors */
	int                   *group_fds;         /* leader file descriptors */

	unsigned long         *provided;    /* provided counter capabilities */
	unsigned long         *required;      /* required counter capability */

	unsigned long         *evntsels;                  /* event selectors */
	unsigned long         *perfctrs;              /* performance counter */

	int                    state;                    /* scheduling state */

	unsigned long         *time_enabled;             /* time at openning */
	unsigned long         *time_running;     /* cummulative running time */

	struct schedule_unit  *next;                   /* next schedule unit */
	struct schedule_unit  *prev;               /* previous schedule unit */
};

#define node_count      (current_model->node_count)
#define core_per_node   (current_model->core_per_node)
#define core_count      (node_count * core_per_node)
#define pmc_count       (current_model->count)

static struct schedule_unit *schedule_list;

static int next_fd = 0;


static void print_schedule_list(void)
{
	struct schedule_unit *cur = schedule_list;
	unsigned long unit = 0, core, id;
	const char *state;
	int header;

	if (cur == NULL)
		printf("(no scheduled unit)\n");

	do {
		switch (cur->state) {
		case STATE_READY: state = "READY" ; break;
		case STATE_RUN  : state = "RUN"   ; break;
		case STATE_WAIT : state = "WAIT"  ; break;
		default         : state = "UNKWN" ; break;
		}

		printf("schedule unit %lu (%s):\n", unit, state);

		for (core = 0; core < core_count; core++) {
			header = 0;
			for (id = 0; id < pmc_count; id++) {

				if (cur->required[core * pmc_count + id] == 0)
					continue;

				if (!header) {
					printf("  core %lu:\n", core);
					header = 1;
				}

				printf("    counter %lu (fd = %3d / group "
				       "= %3d)  <-  %lx\n", id,
				       cur->fds[core * pmc_count + id],
				       cur->group_fds[core * pmc_count + id],
				       cur->evntsels[core * pmc_count + id]);
			}
		}

		cur = cur->next;
		unit++;
	} while (cur != schedule_list);
}


/*
 * Allocation part
 */


static struct schedule_unit *alloc_unit(void)
{
	struct schedule_unit *new;
	unsigned long size = core_count * pmc_count;

	new = malloc(sizeof (*new));
	if (new == NULL)
		goto err;

	new->fds = malloc(size * sizeof (int));
	new->group_fds = malloc(size * sizeof (int));
	new->provided = malloc(size * sizeof (unsigned long));
	new->required = malloc(size * sizeof (unsigned long));
	new->evntsels = malloc(size * sizeof (unsigned long));
	new->perfctrs = malloc(size * sizeof (unsigned long));
	new->time_enabled = malloc(size * sizeof (unsigned long));
	new->time_running = malloc(size * sizeof (unsigned long));

	if (new->fds == NULL || new->group_fds == NULL)
		goto err_field;
	if (new->provided == NULL || new->required == NULL)
		goto err_field;
	if (new->evntsels == NULL || new->perfctrs == NULL)
		goto err_field;
	if (new->time_enabled == NULL || new->time_running == NULL)
		goto err_field;

	return new;
 err_field:
	free(new->fds);
	free(new->group_fds);
	free(new->provided);
	free(new->required);
	free(new->evntsels);
	free(new->perfctrs);
	free(new->time_enabled);
	free(new->time_running);
	free(new);
 err:
	return NULL;
}

static void init_unit(struct schedule_unit *unit)
{
	unsigned long core, size = core_count * pmc_count;

	memset(unit->required, 0, size * sizeof (unsigned long));

	for (core = 0 ; core < core_count ; core++)
		memcpy(unit->provided + core * pmc_count,
		       current_model->capabilities,
		       pmc_count * sizeof (unsigned long));

	unit->state = STATE_READY;
	unit->next = NULL;
	unit->prev = NULL;
}

static struct schedule_unit *find_unit(int group_fd)
{
	struct schedule_unit *cur = schedule_list;
	unsigned long i, size = core_count * pmc_count;

	if (cur == NULL)
		return NULL;
	
	do {
		for (i=0; i<size; i++) {
			if (cur->required[i] == 0)
				continue;
			if (cur->fds[i] == group_fd)
				return cur;
		}

		cur = cur->next;
	} while (cur != schedule_list);

	return NULL;
}

static void insert_unit(struct schedule_unit *unit)
{
	if (schedule_list == NULL) {
		schedule_list = unit;
		unit->next = unit;
		unit->prev = unit;
	} else {
		unit->next = schedule_list;
		unit->prev = schedule_list->prev;
		schedule_list->prev->next = unit;
		schedule_list->prev = unit;
	}
}

static struct schedule_unit *obtain_unit(void)
{
	struct schedule_unit *unit = alloc_unit();

	if (unit != NULL) {
		init_unit(unit);
		insert_unit(unit);
	}

	return unit;
}


static unsigned long take_empty_slot(struct schedule_unit *unit, int core,
				     unsigned long require)
{
	unsigned long i, off;

	off = core * pmc_count;

	for (i=0; i<pmc_count; i++) {
		if (unit->required[off + i] != 0)
			continue;
		if ((unit->provided[off + i] & require) != require)
			continue;

		unit->required[off + i] = require;
		return off + i;
	}

	return BAD_SLOT;
}

static int fixpoint_group_fd(struct schedule_unit *unit, unsigned long slot,
			     int *group_fd)
{
	unsigned long i, size = core_count * pmc_count;

	if (*group_fd == -1)
		return 0;

	for (i=0; i<size; i++) {
		if (i == slot)
			continue;
		if (unit->required[i] == 0)
			continue;
		if (unit->fds[i] == *group_fd)
			break;
	}

	if (i == size)
		return -1;
	
	if (unit->group_fds[i] != -1)
		*group_fd = unit->group_fds[i];
	return 0;
}

static int setup_hwc(const struct perf_event_attr *hw_event,
		     struct schedule_unit *unit, unsigned long slot,
		     int group_fd)
{
	if (fixpoint_group_fd(unit, slot, &group_fd))
		return -1;

	unit->fds[slot] = next_fd++;
	unit->group_fds[slot] = group_fd;

	unit->time_enabled[slot] = time_now();
	unit->time_running[slot] = 0;

	unit->evntsels[slot] = hw_event->config;
	if (!hw_event->exclude_user)
		unit->evntsels[slot] |= (1 << 16);
	if (!hw_event->exclude_kernel)
		unit->evntsels[slot] |= (1 << 17);
	unit->evntsels[slot] |= (1 << 22);

	return unit->fds[slot];
}

static int alloc_hwc(const struct perf_event_attr *hw_event, int core,
		     int group_fd)
{
	struct schedule_unit *unit;
	unsigned long slot, require;

	if (group_fd >= 0) {
		unit = find_unit(group_fd);
	} else {
		unit = obtain_unit();
	}

	if (unit == NULL)
		return -1;

	require = COUNTER_CAP_CORE;
	if ((hw_event->config & 0x1000000e0) == 0x1000000e0)
		require = COUNTER_CAP_NB;

	slot = take_empty_slot(unit, core, require);

	if (slot == BAD_SLOT)
		return -1;

	return setup_hwc(hw_event, unit, slot, group_fd);
}


/*
 * Scheduling part
 */


static struct schedule_unit *hardware;

static unsigned long schedule_start;

static unsigned long schedule_stop;


static int try_consume_unit(struct schedule_unit *unit, int uid)
{
	unsigned long core, pmc, off, i, size;
	unsigned long required;

	for (core = 0 ; core < core_count ; core++) {
		off = core * pmc_count;

		for (pmc = 0 ; pmc < pmc_count ; pmc++) {
			required = unit->required[off + pmc];
			if (required == 0)
				continue;

			for (i=0; i<pmc_count; i++) {
				if ((hardware->provided[off + i] & required)
				    != required)
					continue;
				if (hardware->required[off + i] != 0)
					continue;

				hardware->fds[off + i] = uid;
				hardware->required[off + i] = 1 + pmc;
				hardware->evntsels[off + i] =
					unit->evntsels[off + pmc];
				break;
			}

			if (i == pmc_count)
				goto failed;
		}
	}

	return 0;
 failed:
	size = core_count * pmc_count;
	for (i=0; i<size; i++)
		if (hardware->fds[i] == uid)
			hardware->required[i] = 0;
	return -1;
}

static void update_unit(struct schedule_unit *unit, int uid)
{
	unsigned long core, pmc, off, i;

	for (core = 0 ; core < core_count ; core++) {
		off = core * pmc_count;
		for (i = 0 ; i < pmc_count ; i++) {
			if (hardware->required[off + i] == 0)
				continue;
			if (hardware->fds[off + i] != uid)
				continue;

			pmc = hardware->required[off + i] - 1;
			unit->perfctrs[off + pmc] +=
				hardware->perfctrs[off + i];
			unit->time_running[off + pmc] +=
				schedule_stop - schedule_start;
		}
	}
}


static void consume_schedule_units(void)
{
	struct schedule_unit *cur = schedule_list;
	int uid = 0;

	init_unit(hardware);

	if (cur == NULL)
		return;
	do {
		if (cur->state != STATE_READY)
			goto next;
		if (try_consume_unit(cur, uid))
			goto next;

		cur->state = STATE_RUN;

	next:
		uid++;
		cur = cur->next;
	} while (cur != schedule_list);
}

static void update_schedule_units(void)
{
	struct schedule_unit *cur = schedule_list;
	int uid = 0;

	if (cur == NULL)
		return;
	do {
		if (cur->state != STATE_RUN)
			goto next;
		
		update_unit(cur, uid);
		cur->state = STATE_WAIT;

	next:
		uid++;
		cur = cur->next;
	} while (cur != schedule_list);
}


static int is_all_unit_waiting(void)
{
	struct schedule_unit *cur = schedule_list;

	if (cur == NULL)
		return 1;
	do {
		if (cur->state != STATE_WAIT)
			return 0;
		cur = cur->next;
	} while (cur != schedule_list);

	return 1;
}

static void set_all_unit_ready(void)
{
	struct schedule_unit *cur = schedule_list;

	if (cur == NULL)
		return;
	do {
		cur->state = STATE_READY;
		cur = cur->next;
	} while (cur != schedule_list);
}


static void start_hardware(void)
{
	unsigned long core, pmc;

	schedule_start = time_now();

	for (core = 0 ; core < core_count ; core++)
		for (pmc = 0 ; pmc < pmc_count ; pmc++) {
			if (hardware->required[core * pmc_count + pmc] == 0)
				continue;
			wrmsr(current_model->evntsels[pmc],
			      hardware->evntsels[core*pmc_count + pmc], core);
			wrmsr(current_model->perfctrs[pmc], 0, core);
		}
}

static void stop_hardware(void)
{
	unsigned long core, pmc;

	schedule_stop = time_now();

	for (core = 0 ; core < core_count ; core++)
		for (pmc = 0 ; pmc < pmc_count ; pmc++) {
			if (hardware->required[core * pmc_count + pmc] == 0)
				continue;
			hardware->perfctrs[core * pmc_count + pmc] =
				rdmsr(current_model->perfctrs[pmc], core);
		}
}


/*
 * Threading part
 */


#define SCHED_PERIOD  250000ul

static pthread_mutex_t sched_mutex = PTHREAD_MUTEX_INITIALIZER;

static int initialized = 0;


static void schedule(void)
{
	pthread_mutex_lock(&sched_mutex);

	stop_hardware();
	update_schedule_units();

	if (is_all_unit_waiting())
		set_all_unit_ready();

	if (0)
		print_schedule_list();

	consume_schedule_units();
	start_hardware();

	pthread_mutex_unlock(&sched_mutex);
}

static void *scheduler(void *a __attribute__((unused)))
{
	while (1) {
		usleep(SCHED_PERIOD);
		schedule();
	}

	return NULL;
}

static void initialize(void)
{
	pthread_t tid;

	hardware = alloc_unit();
	
	pthread_create(&tid, NULL, scheduler, NULL);
	pthread_detach(tid);

	initialized = 1;
}


/*
 * Interface
 */


int xen_open_hwc(const struct perf_event_attr *hw_event, pid_t pid, int cpu,
		 int group_fd, unsigned long flags)
{
	int fd = -1;

	if (!initialized)
		initialize();

	pthread_mutex_lock(&sched_mutex);

	if (pid != -1) {
		fprintf(stderr, "ERROR: local HWC no implemented\n");
		goto out;
	} else if (flags != 0) {
		fprintf(stderr, "ERROR: HWC flags not implemented\n");
		goto out;
	} else if (hw_event->type != PERF_TYPE_RAW) {
		fprintf(stderr, "ERROR: only support PERF_TYPE_RAW\n");
		goto out;
	} else if ((fd = alloc_hwc(hw_event, cpu, group_fd)) == -1) {
		goto out;
	}

 out:
	pthread_mutex_unlock(&sched_mutex);
	return fd;
}

int xen_read_hwc(int hwc, struct perf_read_ev *hw_read)
{
	int /* i, */ ret = 0;
	/* struct perfctr_group *group; */
	struct schedule_unit *unit;
	unsigned long i, size = core_count * pmc_count;
	
	if (!initialized)
		initialize();

	pthread_mutex_lock(&sched_mutex);

	unit = find_unit(hwc);
	if (unit == NULL)
		goto out;

	for (i=0; i<size; i++) {
		if (unit->required[i] == 0)
			continue;
		if (unit->fds[i] != hwc)
			continue;

		hw_read->value = unit->perfctrs[i];
		hw_read->time_enabled = time_now() - unit->time_enabled[i];
		hw_read->time_running = unit->time_running[i];

		break;
	}

	ret = sizeof(*hw_read);
 out:
	pthread_mutex_unlock(&sched_mutex);
	return ret;
}
