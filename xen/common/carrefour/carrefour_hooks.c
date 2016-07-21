#include <asm/p2m.h>
#include <xen/carrefour/carrefour.h>
#include <xen/carrefour/carrefour_main.h>
#include <xen/domain.h>
#include <xen/lib.h>
#include <xen/mm.h>
#include <xen/time.h>
#include <xen/sched.h>


struct carrefour_hook_stats_t carrefour_hook_stats;

/* quick test - remove */
int movelog_domains[8];
int movelog_source;
unsigned long movelog[8][8][8];



int s_migrate_pages(int domain, unsigned long nr_pages, void ** pages,
                    int * nodes, int throttle) {
    unsigned long i, addr, gfn, start, end;
    struct domain *d = get_domain_by_id(domain);

    start = NOW();

    for(i = 0; i < nr_pages; i++) {
        addr = (unsigned long) pages[i];
        gfn = addr >> PAGE_SHIFT;

        memory_move(d, gfn, nodes[i], 0);

        /* quick test - remove */
        {
            int id;
            int bridge = smp_processor_id() / 8;

            for (id=0; i<8; id++)
                if (movelog_domains[id] == domain)
                    break;
                else if (movelog_domains[id] == 0) {
                    movelog_domains[id] = domain;
                    break;
                }

            if (id < 8) {
                movelog[id][movelog_source][bridge]++;
                movelog[id][bridge][nodes[i]]++;
                /* movelog[id][movelog_source][nodes[i]]++; */
            }
        }
    }

    put_domain(d);

    end = NOW();
    run_stats.time_spent_in_migration += end - start;
    this_cpu(core_run_stats).time_spent_in_migration += end - start;

    return 0;
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
