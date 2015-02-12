/*
Copyright (C) 2013  
Fabien Gaud <fgaud@sfu.ca>, Baptiste Lepers <baptiste.lepers@inria.fr>,
Mohammad Dashti <mdashti@sfu.ca>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
version 2, as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifndef __CARREFOUR_MAIN__
#define __CARREFOUR_MAIN__ 

#define FAKE_IBS           0

#ifdef LEGACY_MADV_REP
#define MADV_REPLICATE     16
#define MADV_DONTREPLICATE 17
#else
#define MADV_REPLICATE     63
#define MADV_DONTREPLICATE 64
#endif

#define HPAGE_SIZE  (PAGE_SIZE << PAGE_ORDER_2M)
#define SDPAGE_MASK (~(PAGE_SIZE-1))
#define HGPAGE_MASK (~(HPAGE_SIZE-1))

int start_profiling(void);
int stop_profiling(void);

/* This declaration is made inside the kernel but not exported; duplicate it here */
typedef struct page *new_page_t(struct page *, unsigned long private, int **);
struct page_to_node {
   unsigned long addr;
   struct page *page;
   int node;
   int status;
};

/* #include "console.h" */
#include <xen/carrefour/carrefour_hooks.h>
#include <xen/carrefour/carrefour_options.h>
#include <xen/carrefour/carrefour_ibs.h>
#include <xen/carrefour/carrefour.h>
#include <xen/carrefour/carrefour_machine.h>
#include <xen/carrefour/carrefour_rbtree.h>
/* #include "carrefour_tid_replication.h" */

#endif
