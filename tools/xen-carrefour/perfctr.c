#define __XOPEN_SOURCE 700

#include <stdlib.h>
#include <pthread.h>
#include "xencarrefour.h"


#define PERFCTR_PER_GROUP   4
#define PERFCTR_STATE_RDY   0
#define PERFCTR_STATE_RUN   1
#define PERFCTR_STATE_STP   2


struct perfctr_group
{
	int fd_count;
	int fds[PERFCTR_PER_GROUP];
	unsigned long date_scheduled;
	unsigned long date_enabled[PERFCTR_PER_GROUP];
	unsigned long time_running[PERFCTR_PER_GROUP];
	unsigned long evntsel[PERFCTR_PER_GROUP];
	unsigned long perfctr[PERFCTR_PER_GROUP];
	int state;
	struct perfctr_group *next;
};


static int perfctr_next_fd;
static struct perfctr_group **perfctr_groups;


static struct perfctr_group *find_perfctr(int cpu, int fd)
{
	struct perfctr_group *cell = perfctr_groups[cpu];
	int i;

	while (cell) {
		for (i=0; i<cell->fd_count; i++)
			if (cell->fds[i] == fd)
				return cell;
		cell = cell->next;
	}

	return cell;
}

static void setup_perfctr(const struct perf_event_attr *hw_event,
			  struct perfctr_group *group, int idx)
{
	group->fds[idx] = perfctr_next_fd++;
	group->date_enabled[idx] = 0;
	group->time_running[idx] = 0;
	group->perfctr[idx] = 0;

	group->evntsel[idx] = hw_event->config;
	if (!hw_event->exclude_user)
		group->evntsel[idx] |= (1 << 16);
	if (!hw_event->exclude_kernel)
		group->evntsel[idx] |= (1 << 17);
	group->evntsel[idx] |= (1 << 22);
}

static int alloc_perfctr(const struct perf_event_attr *hw_event, int cpu,
			 int group_fd)
{
	struct perfctr_group *cell = find_perfctr(cpu, group_fd);

	if (!cell) {
		cell = malloc(sizeof(struct perfctr_group));
		cell->fd_count = 0;
		cell->state = PERFCTR_STATE_RDY;
		cell->next = perfctr_groups[cpu];
		perfctr_groups[cpu] = cell;
	}

	if (cell->fd_count >= PERFCTR_PER_GROUP)
		return -1;
	setup_perfctr(hw_event, cell, cell->fd_count);
	
	return cell->fds[cell->fd_count++];
}



#define GROUP_PER_SCHED     16
#define PERFCTR_PER_SCHED   4
#define EVNTSEL0            0xc0010000
#define PERFCTR0            0xc0010004
#define SCHED_PERIOD        250000ul


struct perfctr_sched
{
	int group_count;
	int perfctr_count;
	struct perfctr_group *groups[GROUP_PER_SCHED];
};


static pthread_mutex_t sched_mutex = PTHREAD_MUTEX_INITIALIZER;
static int perfctr_sched_count;
static unsigned long last_scheduled;
static struct perfctr_sched *perfctr_scheds;


static unsigned long __schedule_out(struct perfctr_group *group,
				    unsigned long addr, int cpu)
{
	unsigned long now = time_now();
	int i;

	for (i=0; i<group->fd_count; i++)
		if (group->date_enabled[i] < group->date_scheduled) {
			group->time_running[i] += now - group->date_scheduled;
			group->perfctr[i] += rdmsr(addr, cpu);
			addr++;
		}
	
	group->state = PERFCTR_STATE_STP;
	return addr;
}

static void schedule_out(void)
{
	int i, j;
	unsigned long addr;

	for (i=0; i<perfctr_sched_count; i++) {
		addr = PERFCTR0;
		for (j=0; j<perfctr_scheds[i].group_count; j++)
			addr = __schedule_out(perfctr_scheds[i].groups[j],
					      addr, i);
	}
}

static int reschedulable(void)
{
	int i;
	struct perfctr_group *cell;

	for (i=0; i<perfctr_sched_count; i++) {
		cell = perfctr_groups[i];
		while (cell) {
			if (cell->state == PERFCTR_STATE_RDY)
				return 0;
			cell = cell->next;
		}
	}

	return 1;
}

static void reschedule(void)
{
	int i;
	struct perfctr_group *cell;

	for (i=0; i<perfctr_sched_count; i++) {
		cell = perfctr_groups[i];
		while (cell) {
			cell->state = PERFCTR_STATE_RDY;
			cell = cell->next;
		}
	}
}

static void __schedule_in(int cpu)
{
	struct perfctr_group *cell = perfctr_groups[cpu];
	unsigned long evntsel = EVNTSEL0;
	unsigned long perfctr = PERFCTR0;
	int gcount = 0, pcount = 0;
	int i;

	while (cell) {
		if (cell->state != PERFCTR_STATE_RDY)
			goto next;

		if (cell->fd_count <= (PERFCTR_PER_SCHED - pcount)) {
			pcount += cell->fd_count;
			perfctr_scheds[cpu].groups[gcount++] = cell;

			for (i=0; i<cell->fd_count; i++) {
				wrmsr(evntsel++, cell->evntsel[i], cpu);
				wrmsr(perfctr++, 0, cpu);
			}

			cell->date_scheduled = time_now();
		}

		if (pcount == PERFCTR_PER_SCHED)
			break;
		if (gcount == GROUP_PER_SCHED)
			break;
		
	next:
		cell = cell->next;
	}

	perfctr_scheds[cpu].group_count = gcount;
	perfctr_scheds[cpu].perfctr_count = pcount;
}

static void schedule_in(void)
{
	int i;

	for (i=0; i<perfctr_sched_count; i++)
		__schedule_in(i);
}

static void schedule_perfctr(void)
{
	pthread_mutex_lock(&sched_mutex);
	
	schedule_out();
	if (reschedulable())
		reschedule();
	schedule_in();

	last_scheduled = time_now();

	pthread_mutex_unlock(&sched_mutex);
}

static void *scheduler(void *a __attribute__((unused)))
{
	while (1) {
		usleep(SCHED_PERIOD);
		schedule_perfctr();
	}

	return NULL;
}



static int inited = 0;
static void init(void)
{
	int i, size = numa_num_configured_cpus();
	pthread_t tid;

	perfctr_next_fd = 0;
	perfctr_groups = malloc(size * sizeof(struct perfctr_group *));
	
	for (i=0; i<size; i++)
		perfctr_groups[i] = NULL;

	perfctr_sched_count = size;
	last_scheduled = 0;
	perfctr_scheds = malloc(size * sizeof(struct perfctr_sched));

	for (i=0; i<size; i++)
		perfctr_scheds[i].group_count = 0;

	pthread_create(&tid, NULL, scheduler, NULL);
	pthread_detach(tid);

	inited = 1;
}

int xen_open_hwc(const struct perf_event_attr *hw_event, pid_t pid, int cpu,
		 int group_fd, unsigned long flags)
{
	int fd = -1;

	if (!inited)
		init();

	pthread_mutex_lock(&sched_mutex);

	if (pid != -1) {
		fprintf(stderr, "ERROR: local HWC no implemented\n");
		goto out;
	} else if (flags != 0) {
		fprintf(stderr, "ERROR: HWC flags not implemented\n");
		goto out;
	} else if ((fd = alloc_perfctr(hw_event, cpu, group_fd)) == -1) {
		goto out;
	}

 out:
	pthread_mutex_unlock(&sched_mutex);
	return fd;
}

int xen_read_hwc(int hwc, struct perf_read_ev *hw_read)
{
	int i, ret = 0;
	struct perfctr_group *group;
	
	if (!inited)
		init();

	pthread_mutex_lock(&sched_mutex);
	
	for (i=0; i<perfctr_sched_count; i++)
		if ((group = find_perfctr(i, hwc)) != NULL)
			break;
	if (!group)
		goto out;

	for (i=0; i<group->fd_count; i++)
		if (group->fds[i] == hwc)
			break;
	if (i == group->fd_count)
		goto out;

	hw_read->value = group->perfctr[i];
	hw_read->time_enabled = time_now() - group->date_enabled[i];
	hw_read->time_running = group->time_running[i];

	ret = sizeof(*hw_read);
 out:
	pthread_mutex_unlock(&sched_mutex);
	return ret;
}
