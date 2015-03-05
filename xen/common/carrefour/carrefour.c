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

#include <xen/carrefour/carrefour_alloc.h>
#include <xen/carrefour/carrefour_main.h>

struct carrefour_run_stats     run_stats;
struct carrefour_global_stats  global_stats;
/* struct carrefour_hook_stats_t  hook_stats; */

static void decide_pages_fate(void); 

extern unsigned long sampling_rate;

unsigned enable_replication = 1;
unsigned enable_interleaving = 1;
unsigned enable_migration = 1;

// The first time, to enable replication we need at least 100 orders
// because replicating a page is costly (creation of multiple pgd)
// Set to 1, if replication has already been enable once
// TODO: do it per PID
unsigned min_nr_orders_enable_replication = 500;
unsigned min_nr_samples_for_migration = 2; // Only active if interleaving is disabled. 1 otherwise.

int enable_carrefour;

unsigned long nr_accesses_node[MAX_NUMNODES];
unsigned long interleaving_distrib[MAX_NUMNODES];
unsigned long interleaving_distrib_max = 0;
unsigned long interleaving_proba[MAX_NUMNODES];

void carrefour(void) {
   decide_pages_fate();
}

/****
 * Algorithm plumbery for page migration / replication
 * Store pages in appropriate structs
 ****/


/* Each vcpu has a few struct vcpu_pages-> These structs basically store the pages accessed by a vcpu.
 * We group pages by vcpu in order to do batch migration/replication (otherwise we would have to find the vma each time */
struct vcpu_pages {
   struct vcpu_pages *next;
   int nb_pages;
   int nb_max_pages;
   int domain;
   void **pages;
   int *nodes;
};
/* Two structs : pages to be interleaved/migrated and pages to be replicated */
/* Contains vcpu_pages structs */
static struct pages_container {
   int nb_vcpu;
   struct vcpu_pages *vcpus;
} pages_to_interleave, pages_to_replicate, hgpages_to_interleave;

struct vcpu_pages *insert_vcpu_in_container(struct pages_container *c, int domain) {
   struct vcpu_pages *p = kmalloc(sizeof(*p));
   memset(p, 0, sizeof(*p));
   p->domain = domain;
   p->next = c->vcpus;
   c->vcpus = p;
   c->nb_vcpu++;
   return p;
}

void insert_page_in_container(struct pages_container *c, int domain, void *page, int node) {
   struct vcpu_pages *p = NULL;
   struct vcpu_pages *l = c->vcpus;
   while(l) {
      if(l->domain == domain) {
         p = l;
         break;
      }
      l = l->next;
   }

   if(!p) 
      p = insert_vcpu_in_container(c, domain);
   
   if(p->nb_pages >= p->nb_max_pages) {
      if(p->nb_max_pages) {
         p->nb_max_pages *= 2;
      } else {
         p->nb_max_pages = 256;
      }
      p->pages = krealloc(p->pages, sizeof(*p->pages)*p->nb_max_pages);
      p->nodes = krealloc(p->nodes, sizeof(*p->nodes)*p->nb_max_pages);
   }
   p->pages[p->nb_pages] = page;
   p->nodes[p->nb_pages] = node;
   p->nb_pages++;
}

static void carrefour_free_pages(struct pages_container *c) {
   struct vcpu_pages *p, *tmp;
   for(p = c->vcpus; p;) {
      if(p->pages)
         kfree(p->pages);
      if(p->nodes)
         kfree(p->nodes);
      tmp = p;
      p = p->next;
      kfree(tmp);
   }
   c->vcpus = 0;
   c->nb_vcpu = 0;
}



/***
 * Actual interesting stuff 
 ***/
static inline int interleaving_random_node(int from_node, struct sdpage* this) {
   int dest_node = -1;
   int rand = get_random() % 101; /** Between 0 and 100% **/

   if(! carrefour_module_options[REPLICATION_PER_TID].value) {
      if(rand <= interleaving_proba[from_node]) {
         rand = get_random() % interleaving_distrib_max;
         for(dest_node = 0; interleaving_distrib[dest_node] < rand; dest_node++) {}

         if(dest_node >= num_online_nodes()) {
            printk("[WARNING] Bug ...\n");
            dest_node = -1;
         }
      }
   }
   else {
      // This is a quick and dirty hack for now. 
      // Prevents interleaving a page on a node where there are no threads of this pid.
      // It only works because each application has two nodes on our setup.
      // To be corrected.
      if(rand <= interleaving_proba[from_node]) {
         for(dest_node = 0; dest_node < num_online_nodes(); dest_node++) {
            if(this->nb_accesses[dest_node] != 0 && dest_node != from_node) {
               break;
            }
         }

         if(dest_node >= num_online_nodes()) {
            printk("[WARNING] Bug ...\n");
            dest_node = -1;
         }
      }
   }

   return dest_node;
}

static void decide_page_fate(struct sdpage *this, int alread_treated) {
   int nb_dies = 0, dest_node = -1;
   int i;

   int should_be_interleaved = 0;
   int should_be_migrated = 0;
   int should_be_replicated = 0;

   unsigned long total_nb_accesses = 0;
   int from_node;

   struct pages_container * interleave_container;

   for(i = 0; i < num_online_nodes(); i++) {
      if(this->nb_accesses[i] != 0) {
         nb_dies++;
         dest_node = i;
      }
      total_nb_accesses += this->nb_accesses[i];
   }

   if(alread_treated) {
      run_stats.nr_of_samples_after_order   += total_nb_accesses;
      run_stats.nr_of_process_pages_touched ++;
   }

   from_node = phys2node((unsigned long) this->page_phys);

   /*****
    * THIS IS THE REAL CORE AND IMPORTANT LOGIC
    * - duplicate pages accessed from multiple nodes in RO
    * - interleave pages accessed from multiple nodes in RW
    * - move pages accessed from 1 node
    ****/
   /* should_be_replicated = enable_replication && (nb_dies > 1) && (this->nb_writes == 0) && is_user_addr(this->page_lin) && !this->huge; */
   /* should_be_replicated &= is_allowed_to_replicate(this->domain); */
   should_be_replicated = 0;

   should_be_interleaved = enable_interleaving && (nb_dies > 1);

   should_be_migrated = nb_dies == 1;
   should_be_migrated = should_be_migrated && ((enable_migration && total_nb_accesses >= min_nr_samples_for_migration) || enable_interleaving) && from_node != dest_node;

   if(this->huge) {
      interleave_container = &hgpages_to_interleave;
      //printk("huge: %p -- %d %d %d %d %lu\n", this->page_lin, should_be_migrated, should_be_interleaved, should_be_replicated, nb_dies, total_nb_accesses);
   }
   else {
      interleave_container = &pages_to_interleave;
   }

   /** The priority is to replicate if possible **/
   if(should_be_replicated) {
      //printk("Choosing replication for page %p:%d (total_nb_accesses = %lu, nb_dies = %d)\n", this->page_lin, this->tgid, total_nb_accesses, nb_dies);
      insert_page_in_container(&pages_to_replicate, this->domain, this->page_lin, 0);
      run_stats.nr_requested_replications++;
   }
   else if (should_be_interleaved) {
      dest_node = interleaving_random_node(from_node, this);

      if(dest_node != -1 && (dest_node != from_node)) { 
         insert_page_in_container(interleave_container, this->domain, this->page_lin, dest_node);
         run_stats.nb_interleave_orders++;
         //printk("Will interleave page on node %d\n", dest_node);

         run_stats.migr_from_to_node[from_node][dest_node]++;
      }
   }
   else if (should_be_migrated) { 
      //printk("Page %p will be migrated from node %d to node %d\n", this->page_lin, from_node, dest_node);
      insert_page_in_container(interleave_container, this->domain, this->page_lin, dest_node);
      run_stats.nb_migration_orders++;
      run_stats.migr_from_to_node[from_node][dest_node]++;
   }
}

void decide_pages_fate(void) {
   int i,j;
   struct vcpu_pages *p;
   int nr_regular_huge_pages, nr_thp, nr_misplaced_thp, nr_shared_thp, nr_thp_split, nr_thp_split_failed, nr_thp_migrate, nr_thp_migrate_failed;
   unsigned long total_accesses = 0;

   struct rbtree_stats_t rbtree_stats;
   struct pagetree *pages_huge, *pages;

   rbtree_get_merged_stats(&rbtree_stats, &run_stats);
   get_rbtree(&pages, &pages_huge);

   nr_regular_huge_pages = nr_thp = nr_misplaced_thp = nr_shared_thp = nr_thp_split = nr_thp_split_failed = nr_thp_migrate = nr_thp_migrate_failed = 0;

   /* Compute the imbalance and set up the right probabilities for interleaving */
   if(carrefour_module_options[ENABLE_INTERLEAVING].value) {
      for(i = 0; i < num_online_nodes(); i++) { 
         //printk("Load on node %d : %lu memory accesses\n", i, nr_accesses_node[i]);
         total_accesses += nr_accesses_node[i];
      }

      for(i = 0; enable_interleaving && i < num_online_nodes(); i++) {
         if(total_accesses) {
            interleaving_proba[i] = (nr_accesses_node[i]*100 / total_accesses);
            interleaving_distrib[i] = interleaving_distrib_max + (100 - interleaving_proba[i]);
            interleaving_distrib_max = interleaving_distrib[i];
            //printk("Interleaving distrib node %d : %lu\n", i, interleaving_distrib[i]);
         }
         else {
            interleaving_proba[i] = 100;
            interleaving_distrib[i] = interleaving_distrib_max + 100;
            interleaving_distrib_max = interleaving_distrib[i];

            printk("Warning: total_accesses = 0\n");
         }
      }
   }

   for(i = 0; carrefour_module_options[CONSIDER_2M_PAGES].value && (i < pages_huge->reserve.index); i++) {
      struct sdpage * this = &pages_huge->reserve.pages[i];
      int i;
      int nb_nodes = 0;
      int total_nb_accesses = 0;
      int from_node = phys2node((unsigned long) this->page_phys);
      int accessed_by_node = -1;

      if(this->huge == -1) {
         //printk("Page 0x%lx has been marked as huge by IBS\n", (unsigned long) this->page_lin);
         this->huge = 0;
         continue;
      }

      if(! has_been_accessed_recently(this) || this->split || this->invalid) {
         continue;
      }

      if(carrefour_module_options[PAGE_BOUNCING_FIX_2M].value && this->migrated) {
         continue;
      }

      if(!this->huge) {
         /** We first check if the page is valid and if it is a huge page **/
	 int ret = 0;
	 /* printk("int ret = is_huge_addr_sloppy(this->domain, (unsigned long) this->page_lin);\n"); */
         
         this->invalid = 0;
         this->huge = 0;

         if(unlikely(ret == -1)) {
            this->invalid = 1;
         }
         else if (ret == 1) {
            this->huge = 1;
         }

         if(this->invalid || ! this->huge) {
            continue;
         }
      }
      else {
         //printk("Page 0x%lx has been marked as huge by IBS\n", (unsigned long) this->page_lin);
      }

      /** If that's a regular huge page **/
      /*if(this->huge == 1) {
         nr_regular_huge_pages++;

         if(carrefour_module_options[MIGRATE_REGULAR_HP].value) {
            decide_page_fate(this, alread_treated);
         }
         continue;
      }*/

      /** If that's a THP **/
      nr_thp++;

      /** We first compute by how many nodes this page has been accessed **/
      for(i = 0; i < num_online_nodes(); i++) {
         if(this->nb_accesses[i] != 0) {
            nb_nodes++;
            accessed_by_node = i;
         }
         total_nb_accesses += this->nb_accesses[i];
      }

      if(nb_nodes > 1 && this->accessed_by_multiple_threads && enable_interleaving) {
         u64 percent_access_to_page = total_accesses?(total_nb_accesses * 100 / total_accesses):0;
         int is_hot = (percent_access_to_page > 6LL);

         nr_shared_thp++;

         if(is_hot && carrefour_module_options[SPLIT_SHARED_THP].value) {
	    int err = -1;
	    printk("int err = find_and_split_thp(this->domain, (unsigned long) this->page_lin);\n");
         
            if(err) {
               printk("(%d) Split has failed (return value is %d)!\n", __LINE__, err);
               nr_thp_split_failed ++;
            }
            else {
               // interleave the subpages (they now exist!)
               int kp, num_nodes_kp = num_online_nodes();
               for(kp = 0; kp < 512; kp++) {
                  insert_page_in_container(&pages_to_interleave, this->domain, this->page_lin+(4096L*kp), kp%num_nodes_kp);
               }
               this->split = 1;
               this->huge = 0;
               nr_thp_split ++;
            }
         }
         else if(enable_interleaving && carrefour_module_options[INTERLEAVE_SHARED_THP].value) {
            int dest_node = interleaving_random_node(from_node, this);
            if(dest_node != -1 && (dest_node != from_node)) {
	       int err = -1;
	       printk("int err = find_and_migrate_thp(this->domain, (unsigned long) this->page_lin, dest_node);\n");
               switch(err) {
                  case 0:
                     nr_thp_migrate++;
                     this->migrated = 1;
                     break;
                  /* case -EBOUNCINGFIX: */
                  /*    this->migrated = 1; */
                  /*    break; */
                  default:
                     printk("(%d) Migrate has failed (return value is %d)!\n", __LINE__, err);
                     nr_thp_migrate_failed ++;
               }
            }
         }
      }

      if(nb_nodes == 1  && accessed_by_node != from_node) {
         if(unlikely(carrefour_module_options[SPLIT_MISPLACED_THP].value)) {
	    int err = -1;
	    printk("int err = find_and_split_thp(this->domain, (unsigned long) this->page_lin);\n");

            if(err) {
               printk("(%d) Split has failed (return value is %d)!\n", __LINE__, err);
               nr_thp_split_failed ++;
            }
            else {
               this->split = 1;
               this->huge = 0;
               nr_thp_split ++;
            }
         }
         else if ((enable_interleaving || enable_migration) && likely(carrefour_module_options[MIGRATE_MISPLACED_THP].value)) {
	    int err = -1;
	    printk("int err = find_and_migrate_thp(this->domain, (unsigned long) this->page_lin, accessed_by_node);\n");
            switch(err) {
               case 0:
                  nr_thp_migrate++;
                  this->migrated = 1;
                  break;
               /* case -EBOUNCINGFIX: */
               /*    this->migrated = 1; */
               /*    break; */
               default:
                  printk("(%d) Migrate has failed (return value is %d)!\n", __LINE__, err);
                  nr_thp_migrate_failed ++;
            }
         }

         nr_misplaced_thp++;
      }
   }
   printu("%d regular huge pages, %d THP -- %d THP shared, %d misplaced -- %d THP split succeded, %d failed -- %d THP migrate succeded, %d failed\n", 
         nr_regular_huge_pages, nr_thp,
         nr_shared_thp, nr_misplaced_thp,
         nr_thp_split, nr_thp_split_failed,
         nr_thp_migrate, nr_thp_migrate_failed
         );

   for(i = 0; i < pages->reserve.index; i++) {
      struct sdpage tmp, *tmp2;
      int alread_treated = 0;
      struct sdpage * this = &pages->reserve.pages[i];

      if(carrefour_module_options[DETAILED_STATS].value) {
         /* int huge; */
	 this->invalid = 1;
         printk("this->invalid = page_status_for_carrefour(this->domain, (unsigned long) this->page_lin, &alread_treated, &huge);\n");

         if(this->invalid) {
            continue;
         }
      }

      tmp.page_phys = (void *) (((unsigned long) this->page_phys) & HGPAGE_MASK);

      tmp2 = insert_in_page_rbtree(&pages_huge->root, &tmp, 0);
      if(!tmp2) {
         if(unlikely(!pages_huge->warned_overflow))
            printk("[BUG, 0x%lx] Cannot find the associated huge page (0x%lx)\n", (unsigned long) this->page_phys, (unsigned long) tmp.page_phys);
         continue;
      }

      if(this->huge == -1) {
         this->huge = 0;
      }

      if((carrefour_module_options[CONSIDER_4K_PAGES].value && !tmp2->huge) || (carrefour_module_options[CONSIDER_2M_PAGES].value && !tmp2->huge && tmp2->split)) {
         //splitted and huge pages already treated before
         decide_page_fate(this, alread_treated);
      }
   }

   /* Migrate or interleave */
   for(p = pages_to_interleave.vcpus; p; p = p->next) {
      int err;

      //printk("Moving %d pages of pid %d!\n", p->nb_pages, p->tgid);
      err = s_migrate_pages(p->domain, p->nb_pages, p->pages, p->nodes, 0);

      if(err) {
         switch(err) {
            /* case -ENOMEM: */
            /*    printk("!No memory left to perform page migration\n"); */
            /*    break; */
            /* case -ESRCH: */
            /*    printk("!Cannot migrate tasks of pid %d because it does not exist!\n", p->tgid); */
            /*    break; */
            /* case -EINVAL: */
            /*    printk("!Cannot migrate tasks of pid %d: no mm?!\n", p->tgid); */
            /*    break; */
            default:
               printk("!Migration returned error %d\n", err);
               break;
         }
      }
   }

   for(p = hgpages_to_interleave.vcpus; p; p = p->next) {
      printu("Moving %d huge pages of pid %d!\n", p->nb_pages, p->domain);
      printk("s_migrate_hugepages(p->domain, p->nb_pages, p->pages, p->nodes);\n");
   }

   /* hook_stats = get_carrefour_hook_stats(); */

   /* if(hook_stats.nr_4k_migrations + hook_stats.nr_2M_migrations) { */
   /*    struct carrefour_options_t options = get_carrefour_hooks_conf(); */

   /*    run_stats.time_spent_in_migration = hook_stats.time_spent_in_migration_4k + hook_stats.time_spent_in_migration_2M; */
   /*    //run_stats.time_spent_in_migration = hook_stats.time_spent_in_migration_2M; */

   /*    if(options.async_4k_migrations) { */
   /*       // Because it's asynchronous, t can be done in parallel on multiple core */
   /*       // We might under-estimate it */
   /*       run_stats.time_spent_in_migration /= num_online_cpus(); */
   /*    }    */
   /* }    */
   /* else { */
      run_stats.time_spent_in_migration = 0;
   /* }    */

   if(run_stats.nb_migration_orders + run_stats.nb_interleave_orders) {
      for(i = 0; i < num_online_nodes(); i++) {
         printu("Moving pages from node %d: ", i);
         for(j = 0; j < num_online_nodes(); j++) {
            printu("%d\t", run_stats.migr_from_to_node[i][j]);
         }
         printu("\n");
      }
   }

   /* replicate */
   if(run_stats.nr_requested_replications >= min_nr_orders_enable_replication) {
      //printk("Nr requested replication = %u\n", nr_requested_replications); 

      min_nr_orders_enable_replication = 1;
      for(p = pages_to_replicate.vcpus; p; p = p->next) {
         int err;
         int i;

         /** TODO:
          * try to "merge" individual pages in groups of contiguous pages to reduce the number of calls
          * thus the number of VMA creation
          * 
          * We should really be careful with tgids because a tgid might have disappeared or worse been reallocated !
          **/
         for(i = 0; i < p->nb_pages; i++) {
            //printk("Replicating page 0x%lx (user = %d)\n", (unsigned long) p->pages[i], is_user_addr(p->pages[i]));
	    printk("err = replicate_madvise(p->tgid, (unsigned long) p->pages[i], PAGE_SIZE, MADV_REPLICATE);\n");
	    err = -1;
            if(err) {
               printk("!Cannot replicate page %p\n", NULL);
            }
            else {
               run_stats.nb_replication_orders++;
            }
         }
      }
   }

   run_stats.total_nr_orders = run_stats.nb_migration_orders + run_stats.nb_interleave_orders + run_stats.nb_replication_orders;

   global_stats.total_nr_orders += run_stats.total_nr_orders;
   global_stats.cumulative_nb_migration_orders   += run_stats.nb_migration_orders;
   global_stats.cumulative_nb_replication_orders += run_stats.nb_replication_orders;
   global_stats.cumulative_nb_interleave_orders  += run_stats.nb_interleave_orders; 


   /* Clean the mess */
   carrefour_free_pages(&pages_to_interleave);
   carrefour_free_pages(&hgpages_to_interleave);
   carrefour_free_pages(&pages_to_replicate);

   printu("Carrefour - %d migration %d interleaving %d replication orders\n",
         run_stats.nb_migration_orders, run_stats.nb_interleave_orders, run_stats.nb_replication_orders
         );

   printu("%lu pages, %lu samples, avg = %lu\n", 
         rbtree_stats.nr_pages_in_tree, rbtree_stats.total_samples_in_tree,
         (unsigned long) run_stats.avg_nr_samples_per_page);

   if(carrefour_module_options[DETAILED_STATS].value) {
      printu("NPPT = %lu -- NSAAO = %lu -- TNO = %lu -- TNSIT = %lu -- TNSM = %lu\n", 
            run_stats.nr_of_process_pages_touched, run_stats.nr_of_samples_after_order, global_stats.total_nr_orders, rbtree_stats.total_samples_in_tree, rbtree_stats.total_samples_missed);
   }
}


void carrefour_init(void) {
   memset(&run_stats, 0, sizeof(struct carrefour_run_stats));

   memset(&pages_to_interleave, 0, sizeof(pages_to_interleave));
   memset(&pages_to_replicate, 0, sizeof(pages_to_replicate));

   memset(&hgpages_to_interleave, 0, sizeof(hgpages_to_interleave));

   memset(&nr_accesses_node, 0, sizeof(unsigned long) * num_online_nodes());
   memset(&interleaving_distrib, 0, sizeof(unsigned long) * num_online_nodes());
   memset(&interleaving_proba, 0, sizeof(unsigned long) * num_online_nodes());
   interleaving_distrib_max = 0;
}

void carrefour_clean(void) {
}
