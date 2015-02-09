#ifndef XENCARREFOUR_H
#define XENCARREFOUR_H


#include <stdlib.h>
#include <linux/perf_event.h>
#include "carrefour.h"


unsigned long time_now(void);


/* === NUMA backend ======================================================== */

struct bitmask
{
	unsigned long arr[0];
};

int numa_num_configured_cpus(void);

struct bitmask *numa_bitmask_alloc(unsigned int n);

struct bitmask *numa_allocate_cpumask(void);

void numa_free_cpumask(struct bitmask *bmp);

int numa_node_to_cpus(int node, struct bitmask *mask);

int numa_bitmask_isbitset(const struct bitmask *bmp, unsigned int i);

void numa_bitmask_free(struct bitmask *bmp);

int numa_num_configured_nodes(void);


/* === Hypercall stub ====================================================== */

void wrmsr(unsigned long addr, unsigned long value, int cpu);

unsigned long rdmsr(unsigned long addr, int cpu);

double musage(void);

int xen_carrefour_send(const char *str, unsigned long count);


/* === Perf counters backend =============================================== */

int xen_open_hwc(const struct perf_event_attr *hw_event, pid_t pid, int cpu,
		 int group_fd, unsigned long flags);

int xen_read_hwc(int hwc, struct perf_read_ev *hw_read);


/* === GSL backend ========================================================= */

double gsl_stats_mean(const double data[], size_t stride, size_t n);

double gsl_stats_sd_m(const double data[], size_t stride, size_t n,
		      double mean);


#endif
