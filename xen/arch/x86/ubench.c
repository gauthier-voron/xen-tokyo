#include <asm/msr.h>
#include <asm/paging.h>
#include <asm/types.h>
#include <asm/ubench.h>
#include <xen/lib.h>
#include <xen/time.h>


struct ubench
{
	cpumask_t       cpus;               /* cores on which to run */
    int             node;
	void           *memory;             /* memory to access */
	unsigned long   size;               /* size of the memory to access */
	unsigned long   time;               /* running time */
	
	unsigned long   count[NR_CPUS];     /* count of processed bytes */
	unsigned long   nseconds[NR_CPUS];  /* count of elapsed nanosecond */
	unsigned long   cycles[NR_CPUS];    /* count of elapsed cycles */
};


static int benches_alloc[UBENCH_MAX_BENCHES];
static struct ubench benches[UBENCH_MAX_BENCHES];

extern u64 wrsse2(void *memory, u64 size, u64 cycles);
extern u64 repstos(void *memory, u64 size, u64 cycles);


static int alloc_bench(void)
{
	int i;

	for (i=0; i<UBENCH_MAX_BENCHES; i++)
		if (cmpxchg(&benches_alloc[i], 0, 1) == 0)
			return i;

	return -1;
}

static void free_bench(int bd)
{
	benches_alloc[bd] = 0;
}


int prepare_ubench(int node, unsigned long size, unsigned long time)
{
	unsigned int memflags = MEMF_node(node) | MEMF_exact_node;
	int bd = alloc_bench();
	struct ubench *bench = &benches[bd];
	unsigned long order;
    int i;

	cpumask_clear(&bench->cpus);
    bench->node = node;
	bench->time = time;
    bench->size = size;

    for (i=0; i<NR_CPUS; i++)
    {
        bench->count[i] = 0;
        bench->nseconds[i] = 0;
        bench->cycles[i] = 0;
    }

	order = get_order_from_bytes(size);
	bench->memory = alloc_xenheap_pages(order, memflags);
	if ( bench->memory == NULL )
    {
        free_bench(bd);
        bd = -1;
    }

    return bd;
}

void affect_ubench(int bd, int core)
{
    struct ubench *bench = &benches[bd];
    cpumask_set_cpu(core, &bench->cpus);
}

static void __run_ubench(void *args)
{
    int bd = *((int *) args);
    struct ubench *bench = &benches[bd];
    u64 cstart, cend;
    u64 tstart, tend;
    int cpu = get_processor_id();

    tstart = NOW();
    rdtscll(cstart);
    bench->count[cpu] += wrsse2(bench->memory, bench->size, bench->time)
        * bench->size;
    rdtscll(cend);
    tend = NOW();

    bench->cycles[cpu] += cend - cstart;
    bench->nseconds[cpu] += tend - tstart;
}

void run_ubench(int bd)
{
    struct ubench *bench = &benches[bd];
    on_selected_cpus(&bench->cpus, __run_ubench, &bd, 1);
}

void finalize_ubench(int bd)
{
    struct ubench *bench = &benches[bd];
	unsigned long order = get_order_from_bytes(bench->size);
    unsigned long sum = 0;
    int i;

    printk("ubench %d:\n", bd);
    printk("- allocation node %d\n", bench->node);
    printk("- allocation size %lu bytes\n", bench->size);
    for_each_cpu (i, &bench->cpus)
    {
        if ( bench->nseconds[i] / 1000 == 0 )
        {
            printk("  - core %d : no information\n", i);
            continue;
        }
        
        printk("  - core %d : throughput: %lu MB/s\n", i,
               bench->count[i] / ( bench->nseconds[i] / 1000 ));
        sum += bench->count[i] / ( bench->nseconds[i] / 1000 );
    }
    printk("- total throughput: %lu MB/s\n", sum);
    
    free_xenheap_pages(bench->memory, order);
    free_bench(bd);
}


/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
