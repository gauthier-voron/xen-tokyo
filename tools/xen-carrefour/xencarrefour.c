#include <stdlib.h>
#include <time.h>
#include "xencarrefour.h"


static unsigned long time_now(void)
{
	struct timespec tv;

	clock_gettime(CLOCK_REALTIME, &tv);
	return tv.tv_sec * 1000000000ul + tv.tv_nsec;
}


#define CPUCOUNT    6
#define NODCOUNT    8


int numa_num_configured_cpus(void)
{
	return CPUCOUNT * NODCOUNT;
}

struct bitmask *numa_bitmask_alloc(unsigned int n)
{
	size_t s = sizeof(unsigned long);
	return calloc((n / s) + !(n % s), s);
}

int numa_node_to_cpus(int node, struct bitmask *mask)
{
	unsigned int i;
	size_t s = sizeof(unsigned long);

	for (i=node*CPUCOUNT; i<(node+1)*CPUCOUNT; i++)
		mask->arr[i/s] |= (1 << (i%s));

	return 0;
}

int numa_bitmask_isbitset(const struct bitmask *bmp, unsigned int i)
{
	size_t s = sizeof(unsigned long);
	return bmp->arr[i/s] & (1 << (i%s));
}

void numa_bitmask_free(struct bitmask *bmp)
{
	free(bmp);
}

int numa_num_configured_nodes(void)
{
	return NODCOUNT;
}


#define EVNTSEL0    0xc0010000
#define EVNTSEL1    0xc0010001
#define EVNTSEL2    0xc0010002
#define EVNTSEL3    0xc0010003

#define PERFCTR0    0xc0010004
#define PERFCTR1    0xc0010005
#define PERFCTR2    0xc0010006
#define PERFCTR3    0xc0010007

#define HARDWHWC    4
#define GROUPHWC    10
#define TOTALHWC   (HARDWHWC * GROUPHWC * CPUCOUNT * NODCOUNT)

#define HWCFREE     0
#define HWCALLOC    1
#define HWCRUN      2
#define HWCWAIT     3

#define SCHEDPRD    500000000ul

static unsigned long last_schedule = 0;
static int hardware_counter_fds[TOTALHWC];
static unsigned long evntsel_values[TOTALHWC];
static unsigned long perfctr_values[TOTALHWC];
static unsigned long time_enabled[TOTALHWC];
static unsigned long time_running[TOTALHWC];

static int alloc_hwc(int cpu, int group_fd)
{
	int i;
	int start, end, incr, group;

	if (group_fd == -1) {
		start = cpu * HARDWHWC * GROUPHWC;
		end = start + HARDWHWC * GROUPHWC;
		incr = HARDWHWC;
	} else {
		group = group_fd % (HARDWHWC * GROUPHWC);
		start = cpu * HARDWHWC * GROUPHWC + group * HARDWHWC;
		end = start + HARDWHWC;
		incr = 1;
	}

	for (i=start; i<end; i+=incr)
		if (hardware_counter_fds[i] == HWCFREE)
			break;

	if (i == end) {
		fprintf(stderr, "ERROR: cannot allocate more counters\n");
		return -1;
	}

	hardware_counter_fds[i] = HWCALLOC;
	time_enabled[i] = time_now();
	return i;
}

static void hwc_setup(int fd, const struct perf_event_attr *hw_event)
{
	evntsel_values[fd] = hw_event->config;
	perfctr_values[fd] = 0;

	if (!hw_event->exclude_user)
		evntsel_values[fd] |= (1 << 16);
	if (!hw_event->exclude_kernel)
		evntsel_values[fd] |= (1 << 17);
}

static void hwc_schedule_cpu(int cpu)
{
	int idx;
	int schd, group, elem, scnt, gcnt;
	int scheduled[HARDWHWC];
	int grouped[GROUPHWC];

	for (schd=0; schd<HARDWHWC; schd++)
		scheduled[schd] = 0;
	scnt = HARDWHWC;

	for (group=0; group<GROUPHWC; group++) {
		gcnt = 0;

		for (elem=0; elem<HARDWHWC; elem++) {
			idx  = cpu * HARDWHWC * GROUPHWC;
			idx += group * HARDWHWC;
			idx += elem;

			if (hardware_counter_fds[idx] == HWCWAIT) {
				gcnt = 0;
				break;
			} else if (hardware_counter_fds[idx] == HWCALLOC) {
				grouped[gcnt++] = idx;
			}
		}

		if (gcnt <= scnt) {
			while (gcnt--)
				scheduled[--scnt] = grouped[gcnt];
		}

		if (scnt == 0)
			break;
	}

	for (schd=scnt; schd<HARDWHWC; schd++) {
		idx = scheduled[schd];
		hardware_counter_fds[idx] = HWCRUN;
	}
}

static void hwc_deschedule(void)
{
	int idx;
	int alloc = 0, wait = 0;
	unsigned long now = time_now();

	for (idx=0; idx<TOTALHWC; idx++)
		if (hardware_counter_fds[idx] == HWCRUN) {
			hardware_counter_fds[idx] = HWCWAIT;
			time_running[idx] += now - last_schedule;
			perfctr_values[idx] = 30;
			wait++;
		} else if (hardware_counter_fds[idx] == HWCALLOC) {
			alloc++;
		}

	if (!alloc && wait)
		for (idx=0; idx<TOTALHWC; idx++)
			if (hardware_counter_fds[idx] != HWCFREE)
				hardware_counter_fds[idx] = HWCALLOC;
}

static void hwc_schedule(void)
{
	int node, cpu;
	unsigned long now = time_now();

	if (last_schedule + SCHEDPRD > now)
		return;

	hwc_deschedule();

	for (node=0; node<NODCOUNT; node++)
		for (cpu=0; cpu<CPUCOUNT; cpu++)
			hwc_schedule_cpu(cpu + node * CPUCOUNT);

	last_schedule = now;
}

int xen_open_hwc(const struct perf_event_attr *hw_event, pid_t pid, int cpu,
		 int group_fd, unsigned long flags)
{
	int fd;

	if (pid != -1) {
		fprintf(stderr, "ERROR: local HWC no implemented\n");
		return -1;
	} else if (flags != 0) {
		fprintf(stderr, "ERROR: HWC flags not implemented\n");
		return -1;
	} else if ((fd = alloc_hwc(cpu, group_fd)) == -1) {
		return -1;
	}

	hwc_setup(fd, hw_event);
	return fd;
}

int xen_read_hwc(int hwc, struct perf_read_ev *hw_read)
{
	hwc_schedule();

	hw_read->value = perfctr_values[hwc];
	hw_read->time_enabled = time_now() - time_enabled[hwc];
	hw_read->time_running = time_running[hwc];

	return sizeof(*hw_read);
}


double gsl_stats_mean(const double data[], size_t stride, size_t n)
{
	return 1.0;
}

double gsl_stats_sd_m(const double data[], size_t stride, size_t n,
		      double mean)
{
	return 1.0;
}
