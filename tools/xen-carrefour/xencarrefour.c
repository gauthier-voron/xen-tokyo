#include <stdlib.h>
#include "xencarrefour.h"


int numa_num_configured_cpus(void)
{
	return 48;
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

	for (i=node*6; i<(node+1)*6; i++)
		mask->arr[i/s] |= (1 << (i%s));

	return 0;
}

int numa_bitmask_isbitset(const struct bitmask *bmp, unsigned int i)
{
	size_t s = sizeof(unsigned long);
	return bmp->arr[i/s] & ~(1 << (i%s));
}

void numa_bitmask_free(struct bitmask *bmp)
{
	free(bmp);
}

int numa_num_configured_nodes(void)
{
	return 8;
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
