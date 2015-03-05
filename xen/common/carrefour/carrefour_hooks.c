#include <asm/p2m.h>
#include <xen/carrefour/carrefour_main.h>
#include <xen/domain.h>
#include <xen/lib.h>
#include <xen/mm.h>
#include <xen/sched.h>


struct carrefour_hook_stats_t carrefour_hook_stats;


int s_migrate_pages(int domain, unsigned long nr_pages, void ** pages,
		    int * nodes, int throttle) {
   unsigned long i;
   struct domain *d = get_domain_by_id(domain);

   for(i = 0; i < nr_pages; i++) {
      unsigned long addr = (unsigned long) pages[i];
      unsigned long gfn = addr >> PAGE_SHIFT;
      memory_move(d, gfn, nodes[i], 0);
   }

   put_domain(d);

   return 0;
}
