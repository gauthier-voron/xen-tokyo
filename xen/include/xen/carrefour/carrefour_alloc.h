#ifndef ibs_ALLOC
#define ibs_ALLOC


#include <xen/string.h>


void *kmalloc(unsigned long size);

#define vmalloc(size)  kmalloc(size)

static inline void *kzalloc(unsigned long size)
{
	void *addr = kmalloc(size);
	if (addr != NULL)
		memset(addr, 0, size);
	return addr;
}

#define vzalloc(size)  kzalloc(size)


void *kmalloc_node(unsigned long size, int node);

#define vmalloc_node(size, node)  kmalloc_node(size, node)

static inline void *kzalloc_node(unsigned long size, int node)
{
	void *addr = kmalloc_node(size, node);
	if (addr != NULL)
		memset(addr, 0, size);
	return addr;
}

#define vzalloc_node(size, node)  kzalloc_node(size, node)


void kfree(void *addr);

#define vfree(addr)  kfree(addr)


void *krealloc(void *old, unsigned long size);

#define vrealloc(old, size)  krealloc(old, size)


#endif
