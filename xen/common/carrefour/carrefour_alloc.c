#include <xen/mm.h>


static void *prepare_pages(void *addr, unsigned long order)
{
	if ( addr != NULL )
    {
        ((unsigned long *) addr)[0] = order;
		addr += sizeof(unsigned long);
    }
    
    return addr;
}

void *kmalloc(unsigned long size)
{
    unsigned long order = get_order_from_bytes(size + sizeof(unsigned long));
    void *ret = alloc_xenheap_pages(order, 0);
	return prepare_pages(ret, order);
}

void *kmalloc_node(unsigned long size, int node)
{
    unsigned int memflags = MEMF_node(node) | MEMF_exact_node;
    unsigned long order = get_order_from_bytes(size + sizeof(unsigned long));
    void *ret = alloc_xenheap_pages(order, memflags);
	return prepare_pages(ret, order);
}

void kfree(void *addr)
{
    void *taddr = addr -= sizeof(unsigned long);
	unsigned long order = ((unsigned long *) taddr)[0];
    
    free_xenheap_pages(taddr, order);
}

void *krealloc(void *old, unsigned long size)
{
    void *taddr;
    void *new = kmalloc(size);
    unsigned long order, osize = 0;

    if (old != NULL)
    {
        taddr = old - sizeof(unsigned long);
        order = ((unsigned long *) taddr)[0];
        osize = (1 << (order + PAGE_SIZE)) - sizeof(unsigned long);
    }
    
    if (new != NULL)
        memcpy(new, old, osize);

    if (old != NULL)
        free_xenheap_pages(taddr, order);
    
    return new;
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
