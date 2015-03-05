#include <xen/mm.h>


static void *prepare_pages(void *addr, unsigned long size)
{
	if ( addr != NULL )
    {
        ((unsigned long *) addr)[0] = size;
		addr += sizeof(unsigned long);
    }
    
    return addr;
}

void *kmalloc(unsigned long size)
{
    unsigned long order = get_order_from_bytes(size + sizeof(unsigned long));
    void *ret = alloc_xenheap_pages(order, 0);
	return prepare_pages(ret, size);
}

void *kmalloc_node(unsigned long size, int node)
{
    unsigned int memflags = MEMF_node(node) | MEMF_exact_node;
    unsigned long order = get_order_from_bytes(size + sizeof(unsigned long));
    void *ret = alloc_xenheap_pages(order, memflags);
	return prepare_pages(ret, size);
}

void kfree(void *addr)
{
    void *taddr;
	unsigned long size, order;

    if (addr == NULL)
        return;
    
    taddr = addr -= sizeof(unsigned long);
	size = ((unsigned long *) taddr)[0];
    order = get_order_from_bytes(size + sizeof(unsigned long));
    free_xenheap_pages(taddr, order);
}

void *krealloc(void *old, unsigned long size)
{
    void *taddr;
    void *new = kmalloc(size);
    unsigned long osize;

    if (old != NULL)
    {
        taddr = old - sizeof(unsigned long);
        osize = ((unsigned long *) taddr)[0];
    
        if (new != NULL)
            memcpy(new, old, osize);

        kfree(old);
    }
    
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
