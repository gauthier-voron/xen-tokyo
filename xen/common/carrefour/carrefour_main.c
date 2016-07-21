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

#include <asm/mm.h>
#include <asm/msr.h>
#include <xen/carrefour/carrefour_alloc.h>
#include <xen/carrefour/carrefour_main.h>
#include <xen/cpumask.h>
#include <xen/lib.h>
#include <xen/nodemask.h>
#include <xen/smp.h>
#include <xen/string.h>

/**
 * /proc/inter_cntl
 */
static int running;
static int loaded;
extern int enable_carrefour;

extern unsigned enable_replication;
extern unsigned enable_interleaving;
extern unsigned enable_migration;

extern unsigned long sampling_rate;
extern unsigned long nr_accesses_node[MAX_NUMNODES];

int ibs_proc_write(char *buf, unsigned long count) {
   char c;

   if (count) {
      c = *buf;
      if (c == 'b' && !running) {
         start_profiling();
         running = 1;
      } 
      else if (c == 'e' && running) {
         enable_carrefour = 1;
         stop_profiling();
         running = 0;
      } 
      else if (c == 'x' && running) {
         enable_carrefour = 0;
         stop_profiling();
        
         if(carrefour_module_options[ADAPTIVE_SAMPLING].value) {
            //printk("[ADAPTIVE] Carrefour disabled, reducing the IBS sampling rate\n");
            sampling_rate = (unsigned long) carrefour_module_options[IBS_RATE_CHEAP].value;
         }

         start_profiling();
      }
      else if (c == 'k' && running) {
         enable_carrefour = 0;
         stop_profiling();
         running = 0;
      }
      else if (c == 'i') {
         enable_interleaving = 0;
      }
      else if (c == 'I') {
         enable_interleaving = 1;
      }
      else if (c == 'T') {
         if(count > 1) {
            /* get buffer size */
            char * index = buf;
            char * next_idx;
            int node = 0;

            // Skip the I
            index++;

            for (next_idx = index; next_idx < buf + count; next_idx++) {
               if(*next_idx == ',' || next_idx == (buf + count -1)) {
                  unsigned long value;
                  if(*next_idx == ',') {
                     *next_idx = 0;
                  }

                  if((value = simple_strtol(index, NULL, 10)) < 0) {
                     printk("Value is %s (%lu)\n", index, value);
                     printk("Strange bug\n");
                     memset(&nr_accesses_node, 0, sizeof(unsigned long) * MAX_NUMNODES);
                     break;
                  }
                  nr_accesses_node[node++] = value;
                  index = next_idx+1;

                  //printk("Node %d --> %lu\n", node -1, nr_accesses_node[node-1]);
               }
            }
         }
      }
      else if (c == 'Z') {
         int pid, enabled;
         pid = simple_strtol(buf + 2, (const char **) &buf, 10);
         enabled = simple_strtol(buf + 1, NULL, 10);
         if(pid < 0 || enabled < 0) {
            printk("Error %s\n", buf);
         } else {
            printk("Replication for pid %d => %d\n", pid, enabled);
            printk("change_replication_state(pid, enabled);\n");

            if(enabled) {
               enable_replication = 1;
            }
         }
      }
      else if (c == 'r') {
         enable_replication = 0;
      }
      else if (c == 'R') {
         enable_replication = 1;
      }
      else if (c == 'M') {
         enable_migration = 1;
      }
      else if (c == 'm') {
         enable_migration = 0;
      }

      else if (c == 'F' && carrefour_module_options[ADAPTIVE_SAMPLING].value) {
         // Increases the ibs frequency
         sampling_rate = carrefour_module_options[IBS_RATE_ACCURATE].value;
      }
      else if (c == 'f' && carrefour_module_options[ADAPTIVE_SAMPLING].value) {
         // Decreases the ibs frequency
         sampling_rate = carrefour_module_options[IBS_RATE_CHEAP].value;
      }
      /* else if (c == 'a') { */
      /*    struct carrefour_options_t opt; */
      /*    opt = get_carrefour_hooks_conf(); */
      /*    opt.async_4k_migrations  = 0; */
      /*    configure_carrefour_hooks(opt); */
      /* } */
   }
   return count;
}

/** What to do when starting profiling **/
/** Must be called with NMI disabled   **/
unsigned long time_start_profiling;

int start_profiling(void) {
   rdtscll(time_start_profiling);

   rbtree_init();
   carrefour_init();

   carrefour_ibs_start();

   return 0;
} 

/** And when stoping profiling         **/
/** Must be called with NMI disabled   **/
int stop_profiling(void) {
   //rbtree_print(); //debug
   //show_tid_sharing_map(); //debug

   enable_migration = carrefour_module_options[ENABLE_MIGRATION].value && enable_migration;
   enable_replication = carrefour_module_options[ENABLE_REPLICATION].value && enable_replication;
   enable_interleaving = carrefour_module_options[ENABLE_INTERLEAVING].value && enable_interleaving;

   if(!enable_replication && !enable_interleaving && !enable_migration) {
      enable_carrefour = 0;
   }

   printu("-- Carrefour %s, replication %s, interleaving %s, migration %s, frequency %s\n", 
            enable_carrefour    ? "enabled" : "disabled",
            enable_replication  ? "enabled" : "disabled",
            enable_interleaving ? "enabled" : "disabled",
            enable_migration    ? "enabled" : "disabled",
            sampling_rate == carrefour_module_options[IBS_RATE_ACCURATE].value ? "accurate" : "cheap");

   carrefour_ibs_stop();

   if(enable_carrefour) {
      carrefour();
   }
   
   if(carrefour_module_options[ADAPTIVE_SAMPLING].value) {
      if(run_stats.total_nr_orders < carrefour_module_options[IBS_ADAPTIVE_MAGIC].value) {
         //printk("[ADAPTIVE] Did not take enough decision (%lu). Reducing the IBS sampling rate\n", run_stats.total_nr_orders);
         sampling_rate = carrefour_module_options[IBS_RATE_CHEAP].value;
      }
      else {
         //printk("[ADAPTIVE] Took lots of decisions (%lu). Increasing the IBS sampling rate\n", run_stats.total_nr_orders);
         sampling_rate = carrefour_module_options[IBS_RATE_ACCURATE].value;
      }
   }

   /** free all memory **/
   rbtree_clean();
   carrefour_clean();
   return 0;
}

/**
 * Init and exit
 */
extern unsigned long min_lin_address;
extern unsigned long max_lin_address;

extern struct carrefour_global_stats global_stats;

/* quick test - remove */
#include <xen/sched.h>
extern unsigned long movelog[8][8][8];
extern int movelog_domains[8];

int carrefour_init_module(void) {
   int cpu, err;
   struct carrefour_options_t options;

   if ( loaded )
       return -1;
   loaded = 1;

   memset(&global_stats, 0, sizeof(struct carrefour_global_stats));

   printk("max_lin_address = %lx\n", max_lin_address);
   printk("min_lin_address = %lx\n", min_lin_address);

   printk("NPPT  = Nr processed page touched\n"); 
   printk("NSAAO = Nr samples after an order\n"); 
   printk("TNO   = Total nr of orders\n"); 
   printk("TNSIT = Total number samples in the tree\n"); 
   printk("TNSM  = Total number sample missed\n"); 

   // Module options
   if(! validate_module_options()) {
      printk("Invalid options\n");
      return -1;
   }

   print_module_options();
   //

   err = carrefour_ibs_init();

   if(err) {
      return err;
   }

   machine_init();

   rbtree_load_module();

   printk("reset_carrefour_hooks();\n");

   memset(&options, 0, sizeof(struct carrefour_options_t));
   memset(&run_stats, 0, sizeof(run_stats));
   for_each_online_cpu ( cpu )
       memset(&per_cpu(core_run_stats, cpu), 0,
              sizeof(struct carrefour_run_stats));

   /* quick test - remove */
   {
       int x, y, z;
       for (x=0; x<8; x++)
           for (y=0; y<8; y++)
               for (z=0; z<8; z++)
                   movelog[x][y][z] = 0;
       memset(movelog_domains, 0, sizeof (movelog_domains));
   }

   options.page_bouncing_fix_4k = carrefour_module_options[PAGE_BOUNCING_FIX_4K].value;
   options.page_bouncing_fix_2M = carrefour_module_options[PAGE_BOUNCING_FIX_2M].value;
   options.async_4k_migrations  = 1;
   /* configure_carrefour_hooks(options); */

   printk("Carrefour hooks options\n\tpage_bouncing_fix_4k = %d\n\tasync_4k_migrations = %d\n\tuse_balance_numa_api = %d\n", options.page_bouncing_fix_4k, options.page_bouncing_fix_2M, options.async_4k_migrations);
 
   return 0;
}

void carrefour_exit_module(void) {
    int cpu;
    
    if ( !loaded )
        return;
    loaded = 0;


    carrefour_ibs_exit();

    rbtree_remove_module();

    printk("Carrefour - profiling time = %lu %03lu %03lu %03lu ns\n",
           (run_stats.time_spent_in_profiling) / 1000000000ul,
           ((run_stats.time_spent_in_profiling) / 1000000) % 1000,
           ((run_stats.time_spent_in_profiling) / 1000ul) % 1000,
           (run_stats.time_spent_in_profiling) % 1000);
    for_each_online_cpu ( cpu )
        printk("              [CPU %2d]     = %lu %03lu %03lu %03lu ns\n",
               cpu, (per_cpu(core_run_stats, cpu).time_spent_in_profiling)
               / 1000000000ul,
               ((per_cpu(core_run_stats, cpu).time_spent_in_profiling)
                / 1000000) % 1000,
               ((per_cpu(core_run_stats, cpu).time_spent_in_profiling)
                / 1000ul) % 1000,
               (per_cpu(core_run_stats, cpu).time_spent_in_profiling)
               % 1000);
    
    printk("            migration time = %lu %03lu %03lu %03lu ns\n",
           (run_stats.time_spent_in_migration) / 1000000000ul,
           ((run_stats.time_spent_in_migration) / 1000000) % 1000,
           ((run_stats.time_spent_in_migration) / 1000ul) % 1000,
           (run_stats.time_spent_in_migration) % 1000);
    for_each_online_cpu ( cpu )
        printk("              [CPU %2d]     = %lu %03lu %03lu %03lu ns\n",
               cpu, (per_cpu(core_run_stats, cpu).time_spent_in_migration)
               / 1000000000ul,
               ((per_cpu(core_run_stats, cpu).time_spent_in_migration)
                / 1000000) % 1000,
               ((per_cpu(core_run_stats, cpu).time_spent_in_migration)
                / 1000ul) % 1000,
               (per_cpu(core_run_stats, cpu).time_spent_in_migration)
               % 1000);
    
    printk("            NMI time       = %lu %03lu %03lu %03lu ns\n",
           (run_stats.time_spent_in_NMI) / 1000000000ul,
           ((run_stats.time_spent_in_NMI) / 1000000) % 1000,
           ((run_stats.time_spent_in_NMI) / 1000ul) % 1000,
           (run_stats.time_spent_in_NMI) % 1000);
    for_each_online_cpu ( cpu )
        printk("              [CPU %2d]     = %lu %03lu %03lu %03lu ns\n",
               cpu, (per_cpu(core_run_stats, cpu).time_spent_in_NMI)
               / 1000000000ul,
               ((per_cpu(core_run_stats, cpu).time_spent_in_NMI)
                / 1000000) % 1000,
               ((per_cpu(core_run_stats, cpu).time_spent_in_NMI)
                / 1000ul) % 1000,
               (per_cpu(core_run_stats, cpu).time_spent_in_NMI) % 1000);

    /* quick test - remove */
    {
    /*     unsigned long node, cnt; */
    /*     int i; */
        unsigned long cnt;
        int src, dest, id;
        
        printk("Carrefour - migration profile\n");

        for (src=0; src<8; src++) {
            printk("    [NODE %d] :      0      1      2      3      4      "
                   "5      6      7\n", src);

            for (id=0; id<8; id++) {
                cnt = 0;
                for (dest=0; dest<8; dest++)
                    cnt += movelog[id][src][dest];
                if (cnt == 0)
                    continue;

                printk("     DOM %2d  :", movelog_domains[id]);
                for (dest=0; dest<8; dest++)
                    printk(" %6lu", movelog[id][src][dest]);
                printk(" => %lu\n", cnt);
            }
        }
        
    /*     for_each_online_cpu ( cpu ) { */
    /*         printk("    [CPU %2d] --     0     1     2     3     4     " */
    /*                "5     6     7\n", cpu); */
    /*         for (i=0; i<8; i++) { */
    /*             cnt = 0; */
    /*             for (node=0; node<8; node++) */
    /*                 cnt += per_cpu(movelog, cpu)[i * 8 + node]; */
    /*             if (cnt == 0) */
    /*                 continue; */
    /*             printk("     dom %2d  --", movelog_offset + i); */
    /*             for (node=0; node<8; node++) */
    /*                 printk(" %5lu", per_cpu(movelog, cpu)[i * 8 + node]); */
    /*             printk("\n"); */
    /*         } */
    /*     } */
    }
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
