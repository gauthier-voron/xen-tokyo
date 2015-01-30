#ifndef __MCOOLDOWN_H__
#define __MCOOLDOWN_H__


#include <xen/time.h>


struct mcooldown
{
	unsigned long   size;
	s_time_t        reset;
	s_time_t       *pool;
};


int alloc_mcooldown(struct mcooldown *this, unsigned long size);

void init_mcooldown(struct mcooldown *this, s_time_t reset);

void free_mcooldown(struct mcooldown *this);


void arm_mcooldown(struct mcooldown *this, unsigned long slot);

int check_cooldown(struct mcooldown *this, unsigned long slot);


#endif
