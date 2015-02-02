#ifndef XENCARREFOUR_H
#define XENCARREFOUR_H


#include <stdlib.h>


struct bitmask
{
	unsigned long arr[0];
};


int numa_num_configured_cpus(void);

struct bitmask *numa_bitmask_alloc(unsigned int n);

int numa_node_to_cpus(int node, struct bitmask *mask);

int numa_bitmask_isbitset(const struct bitmask *bmp, unsigned int i);

void numa_bitmask_free(struct bitmask *bmp);

int numa_num_configured_nodes(void);


double gsl_stats_mean(const double data[], size_t stride, size_t n);

double gsl_stats_sd_m(const double data[], size_t stride, size_t n,
		      double mean);


#endif
