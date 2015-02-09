#include <xen/lib.h>


int s_migrate_pages(int pid, unsigned long nr_pages, void **pages, int *nodes,
		    int *status, int flags)
{
	unsigned long i;
	
	printk("s_migrate_pages(%d, %lu, [ ", pid, nr_pages);
	for (i=0; i<nr_pages && i<3; i++)
		printk("%p ", pages[i]);
	if (nr_pages > 3)
		printk("... ");
	printk("], [ ");
	for (i=0; i<nr_pages && i<3; i++)
		printk("%d ", nodes[i]);
	if (nr_pages > 3)
		printk("... ");
	printk("], %p, %d)\n", status, flags);
	
	return -42;
}
