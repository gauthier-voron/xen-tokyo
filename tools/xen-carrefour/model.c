#include <stdlib.h>
#include "model.h"


unsigned long fam10h_evntsels[] = {
	0xc0010000,
	0xc0010001,
	0xc0010002,
	0xc0010003
};

unsigned long fam10h_perfctrs[] = {
	0xc0010004,
	0xc0010005,
	0xc0010006,
	0xc0010007
};

unsigned long fam10h_capabilities[] = {
	COUNTER_CAP_CORE | COUNTER_CAP_NB,
	COUNTER_CAP_CORE | COUNTER_CAP_NB,
	COUNTER_CAP_CORE | COUNTER_CAP_NB,
	COUNTER_CAP_CORE | COUNTER_CAP_NB
};

struct model fam10h_8_6 = {
	.node_count = 8,
	.core_per_node = 6,

	.evntsels = fam10h_evntsels,
	.perfctrs = fam10h_perfctrs,
	.capabilities = fam10h_capabilities,
	.count = 4
};


unsigned long fam15h_evntsels[] = {
	0xc0010200,
	0xc0010202,
	0xc0010204,
	0xc0010206,
	0xc0010208,
	0xc001020a,
	0xc0010240,
	0xc0010242,
	0xc0010244,
	0xc0010246
};

unsigned long fam15h_perfctrs[] = {
	0xc0010201,
	0xc0010203,
	0xc0010205,
	0xc0010207,
	0xc0010209,
	0xc001020b,
	0xc0010241,
	0xc0010243,
	0xc0010245,
	0xc0010247
};

unsigned long fam15h_capabilities[] = {
	COUNTER_CAP_CORE,
	COUNTER_CAP_CORE,
	COUNTER_CAP_CORE,
	COUNTER_CAP_CORE,
	COUNTER_CAP_CORE,
	COUNTER_CAP_CORE,
	COUNTER_CAP_NB,
	COUNTER_CAP_NB,
	COUNTER_CAP_NB,
	COUNTER_CAP_NB,
};

struct model fam15h_8_8 = {
	.node_count = 8,
	.core_per_node = 8,

	.evntsels = fam15h_evntsels,
	.perfctrs = fam15h_perfctrs,
	.capabilities = fam15h_capabilities,
	.count = 10
};


struct cpuid_0000_0001_eax
{
	unsigned int stepping    : 4;
	unsigned int base_model  : 4;
	unsigned int base_family : 4;
	unsigned int _reserved_0 : 4;
	unsigned int ext_model   : 4;
	unsigned int ext_family  : 8;
	unsigned int _reserved_1 : 4;
} __attribute__((packed));


/* So quick and dirty, but I love it so much ;-) */
int get_current_model(struct model *dest)
{
	struct cpuid_0000_0001_eax info;
	unsigned int family;
	
	asm ("cpuid" : "=a" (info) : "a" (0x00000001));
	family = info.base_family + info.ext_family;
		
	switch (family) {
	case 0x10:
		*dest = fam10h_8_6;
		break;
	case 0x15:
		*dest = fam15h_8_8;
		break;
	default:
		return -1;
	}

	return 0;
}


static struct model __current_model;
static int __current_model_init = 0;


const struct model *_current_model(void)
{
	int ret;
	
	if (!__current_model_init) {
		ret = get_current_model(&__current_model);
		if (ret)
			return NULL;
		__current_model_init = 1;
	}

	return &__current_model;
}
