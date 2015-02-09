#include <xen/mm.h>


void *kmalloc(unsigned long size)
{
    unsigned long order = get_order_from_bytes(size + sizeof(unsigned long));
    void *ret = alloc_xenheap_pages(order, 0);

	if ( ret != NULL )
    {
        ((unsigned long *) ret)[0] = order;
		ret += sizeof(unsigned long);
    }
    
	return ret;
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
