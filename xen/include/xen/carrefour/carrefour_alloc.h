#ifndef ibs_ALLOC
#define ibs_ALLOC


void *kmalloc(unsigned long size);

void kfree(void *addr);

void *krealloc(void *old, unsigned long size);


#endif
