#ifndef __XEN_CARREFOUR_H__
#define __XEN_CARREFOUR_H__


#include <xen/lib.h>


int carrefour_init_module(void);

void carrefour_exit_module(void);

int ibs_proc_write(const char *buf, size_t count);


#endif
