#include <xen/mcooldown.h>
#include <xen/mm.h>


int alloc_mcooldown(struct mcooldown *this, unsigned long size)
{
	int ret = 0;
	unsigned long order;
	
    order = get_order_from_bytes(size * sizeof(s_time_t));
    this->size = 0;
	this->pool = alloc_xenheap_pages(order, 0);

	if ( this->pool != NULL )
		this->size = size;
	else
		ret = -1;

	return ret;
}

void init_mcooldown(struct mcooldown *this, s_time_t reset)
{
    unsigned long i;
    
    this->reset = reset;

    for (i=0; i<this->size; i++)
        this->pool[i] = 0;
}

void free_mcooldown(struct mcooldown *this)
{
	unsigned long order;

    if ( this->size == 0 )
        return;
	order = get_order_from_bytes(this->size * sizeof(s_time_t));

	free_xenheap_pages(this->pool, order);
    this->size = 0;
}


void arm_mcooldown(struct mcooldown *this, unsigned long slot)
{
    this->pool[slot] = NOW();
}

int check_cooldown(struct mcooldown *this, unsigned long slot)
{
    return this->pool[slot] + this->reset > NOW();
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
