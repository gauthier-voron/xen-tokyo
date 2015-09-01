#include <stdlib.h>
#include <time.h>
#include <math.h>
/* #include "xc_private.h" */

/* #ifdef PAGE_SIZE */
/* #  undef PAGE_SIZE */
/* #endif */

#include "xencarrefour.h"
#include "model.h"


unsigned long time_now(void)
{
	struct timespec tv;

	clock_gettime(CLOCK_REALTIME, &tv);
	return tv.tv_sec * 1000000000ul + tv.tv_nsec;
}



int numa_num_configured_cpus(void)
{
	return current_model->node_count * current_model->core_per_node;
}

struct bitmask *numa_bitmask_alloc(unsigned int n)
{
	size_t s = sizeof(unsigned long);
	return calloc((n / s) + !(n % s), s);
}

struct bitmask *numa_allocate_cpumask(void)
{
	return numa_bitmask_alloc(numa_num_configured_cpus());
}

void numa_free_cpumask(struct bitmask *bmp)
{
	free(bmp);
}

int numa_node_to_cpus(int node, struct bitmask *mask)
{
	unsigned int i, core;
	size_t s = sizeof(unsigned long);

	core = current_model->core_per_node;
	for (i=node*core; i<(node+1)*core; i++)
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
	return current_model->node_count;
}


double gsl_stats_mean(const double data[], size_t stride, size_t size)
{
	long double mean = 0;
	size_t i;
	
	for (i = 0; i < size; i++)
		{
			mean += (data[i * stride] - mean) / (i + 1);
		}
	
	return mean;
}

static double compute_variance(const double data[], const size_t stride,
			       const size_t n, const double mean)
{
	/* takes a dataset and finds the variance */

	long double variance = 0 ;

	size_t i;

	/* find the sum of the squares */
	for (i = 0; i < n; i++)
		{
			const long double delta = (data[i * stride] - mean);
			variance += (delta * delta - variance) / (i + 1);
		}

	return variance ;
}



double gsl_stats_sd_m(const double data[], size_t stride, size_t n,
		      double mean)
{
	const double variance = compute_variance(data, stride, n, mean);
	const double sd = sqrt (variance * ((double)n / (double)(n - 1)));

	return sd;
}
