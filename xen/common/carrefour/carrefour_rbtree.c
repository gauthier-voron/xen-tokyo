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
#include <xen/percpu.h>

extern unsigned long nr_accesses_node[MAX_NUMNODES];

#define PER_CPU_RBTREE  2  // 0 - One tree, 1 - Per cpu, 2 - Per node

/** rbtree to hold seen pages **/
struct pagetree ** pages;
struct pagetree *pages_global;
struct pagetree *pages_huge_global;

unsigned nr_pagetrees;

/** We preallocate rbtree nodes; this has 2 advantages:
 *  - speed
 *  - no need to free things at the end of profiling, just reset the index to 0 :)
 */

/** RBTREE stats **/
DEFINE_PER_CPU(struct rbtree_stats_t, rbtree_core_stats);

/** init **/
extern unsigned long sampling_rate;

static int logical_time = 0;
int has_been_accessed_recently(struct sdpage * page) {
   return logical_time == page->logical_time;
}

static inline void _rbtree_init(struct pagetree * tree, unsigned long tree_max_size_4k) {
   if(unlikely(!tree)) {
      printk("That's a pretty big bug\n");
      BUG();
   }

   if(tree->initialized) {
      memset(tree->reserve.pages, 0, tree->reserve.index * sizeof(struct sdpage));
   }

   tree->root = RB_ROOT;
   tree->reserve.index = 0;
   tree->warned_overflow = 0;
   tree->initialized = 1;
   tree->reserve.max_pages_to_watch = tree_max_size_4k;

   spin_lock_init(&tree->lock);
}

static inline void _compact_rbtree(struct pagetree * tree, int max_sz) {
   int i, new_index = 0;

   // Reconstructing the tree after compaction
   tree->root = RB_ROOT;
   tree->warned_overflow = 0;
   tree->reserve.max_pages_to_watch = max_sz;

   for(i = 0; i < tree->reserve.index; i++) {
      struct sdpage *page, *new;

      page = &tree->reserve.pages[i];

      if(!has_been_accessed_recently(page) || page->invalid) {
         continue;
      }

      if(i != new_index) {
         //printu("Moving page from index %d to index %d\n", i, new_index);
         tree->reserve.pages[new_index] = *page;
         page = &tree->reserve.pages[new_index];
      }

      new = insert_in_page_rbtree(&tree->root, page, 1);
      if(page != new) {
         printk("BUG !\n");
         break;
      }

      new_index++;
   }

   if(new_index < tree->reserve.index) {
      memset(&(tree->reserve.pages[new_index]), 0, (tree->reserve.index - new_index) * sizeof(struct sdpage));
   }

   tree->reserve.index = new_index;
}

void rbtree_init(void) {
   //static int first_time = 1;
   int i;
   int tree_max_size_4k;

   for_each_online_cpu(i) {
      memset(&per_cpu(rbtree_core_stats, i), 0, sizeof(struct rbtree_stats_t));
   }

   if(carrefour_module_options[ADAPTIVE_SAMPLING].value) {
      if(sampling_rate == carrefour_module_options[IBS_RATE_ACCURATE].value) {
         tree_max_size_4k = MAX_PAGES_TO_WATCH_ACCURATE;
      }
      else {
         tree_max_size_4k = MAX_PAGES_TO_WATCH_CHEAP;
      }
   }
   else {
      tree_max_size_4k = MAX_PAGES_TO_WATCH_ACCURATE;
   }

   for(i = 0; i < nr_pagetrees; i++) {
      _rbtree_init(pages[i], tree_max_size_4k);
   }

   /*if(carrefour_module_options[RESET_HP_TREE].value || first_time) {
      _rbtree_init(&pages_huge);
      first_time = 0;
   }
   else if (carrefour_module_options[ENABLE_TREE_COMPACTION].value) {
      _compact_rbtree(&pages_huge);
   }*/

   logical_time++;
}

void rbtree_get_merged_stats(struct rbtree_stats_t * stats_to_fill, struct carrefour_run_stats * c_stats) {
   int cpu;
   memset(stats_to_fill, 0, sizeof(struct rbtree_stats_t));

   for_each_online_cpu(cpu) {
      struct rbtree_stats_t* stat = &per_cpu(rbtree_core_stats, cpu);
      stats_to_fill->nr_ld_samples += stat->nr_ld_samples;
      stats_to_fill->nr_st_samples += stat->nr_st_samples;
      stats_to_fill->total_samples_in_tree += stat->total_samples_in_tree;
      stats_to_fill->total_samples_missed += stat->total_samples_missed;
      stats_to_fill->nr_pages_in_tree += stat->nr_pages_in_tree;
   }

   /* c_stats->avg_nr_samples_per_page = stats_to_fill->nr_pages_in_tree ? stats_to_fill->total_samples_in_tree / stats_to_fill->nr_pages_in_tree : 0; */
}

/** rbtree insert; for some reason the kernel does not provide an implem... */
struct sdpage * insert_in_page_rbtree(struct rb_root *root, struct sdpage *data, int add) {
   struct rb_node **new = &(root->rb_node), *parent = NULL;

   /* Figure out where to put new node */
   while (*new) {
      struct sdpage *this = container_of(*new, struct sdpage, node);
      parent = *new;

      if (data->page_phys > this->page_phys)
         new = &((*new)->rb_left);
      else if (data->page_phys < this->page_phys)
         new = &((*new)->rb_right);
      else {
         return this;
      }
   }

   /* Add new node and rebalance tree. */
   if(add) {
      rb_link_node(&data->node, parent, new);
      rb_insert_color(&data->node, root);
      return data;
   } else {
      return NULL;
   }
}


/** Called on all IBS samples. Put a page in the rbtree **/
extern unsigned long nr_of_samples_after_order;
static inline int _fetch_page(struct pagetree * tree, unsigned long lin_addr, unsigned long phys_addr, int domain, int vcpu, struct sdpage **page) {
   struct sdpage * tmp;
   int new = 0;

   /*** Insert in rbtree->***/
   tmp = &tree->reserve.pages[tree->reserve.index];
   tmp->page_lin = (void *) (lin_addr);
   tmp->page_phys = (void *) (phys_addr);
   tmp->domain = domain;

   *page = insert_in_page_rbtree(&tree->root, tmp, (tree->reserve.index < tree->reserve.max_pages_to_watch - 1));
   if(*page == tmp && tree->reserve.index < tree->reserve.max_pages_to_watch - 1) {
      tree->reserve.index++;
      new = 1;
   }

   if(*page == NULL && !tree->warned_overflow) {
      printk("OVERFLOW on %s (max size is %d)!\n", tree->name, tree->reserve.max_pages_to_watch);
      tree->warned_overflow = 1;
   }

   if(*page && ((unsigned long ) (*page)->page_lin) != lin_addr) {
      // It can happen because migration is asynchronous
      // Mark the page invalid
      (*page)->invalid = 1;
   }

   if(*page) {
      (*page)->logical_time = logical_time;

      if((*page)->last_vcpu > 0 && (*page)->last_vcpu != vcpu) {
         (*page)->accessed_by_multiple_threads = 1;
      }
      (*page)->last_vcpu = vcpu;
   }

   return new;
}

static inline void _update_page_stat (struct sdpage * page, int node, int tid_index, const struct ibs_record * ibs_op) {
   if(page) {
      if(node >= num_online_nodes() || node < 0) {
         printk("Strange node %d\n", node);
      } else {
         __sync_fetch_and_add(&(page->nb_accesses[node]), 1);
      }

      if(ibs_op->cache_infos & IBS_RECORD_STOP) {
         __sync_fetch_and_add(&(page->nb_writes), 1);
      }
#ifdef OPTERON
      /* We cannot distinguish 4K from 2M pages on Bulldozer using this field because TLBs are unified */
      if(ibs_op->cache_infos & IBS_RECORD_L1TLBHIT2M
	 || ibs_op->cache_infos & IBS_RECORD_L2TLBHIT2M) {
         // We know for sure that's a 2M page
         page->huge = 1; // We don't need an atomic op here (not sure)
      }
   
      if(!page->huge && (!ibs_op->cache_infos & IBS_RECORD_DCL2TLBMISS
			 || !ibs_op->cache_infos & IBS_RECORD_DCL1TLBMISS)) {
         // We know for sure that's a 4k page
         page->huge = -1; // We don't need an atomic op here (not sure)
      }
#endif
   }
}


void rbtree_add_sample(int is_hypervisor, const struct ibs_record *ibs_op, int cpu, int vcpu, int domain) {
   struct sdpage *s_page;
   struct rbtree_stats_t* core_stats;

   int is_softirq = 0;  //WE NEED TO FIND A WAY TO KNOW THAT WE ARE IN IRQ.
                        //I have a kernel hack that does that but can it be done without modifying the kernel?
   //int is_softirq = get_is_in_soft_irq(cpu);
   int tid_index = 0;

   int node = cpu_to_node(cpu);
   int new = 0;

   struct pagetree *tree;

#if PER_CPU_RBTREE == 0 || PER_CPU_RBTREE == 2
   unsigned long flags;
#endif

   /*** Do not consider pages acccessed by the kernel or in IRQ context ***/
   if(is_hypervisor || is_softirq)
      return;
   if(domain == 0 || domain >= DOMID_FIRST_RESERVED)
      return;

#if PER_CPU_RBTREE == 0
   tree = pages[0];

   spin_lock_irqsave(&tree->lock, flags);
   new = _fetch_page(tree, (ibs_op->data_linear_address & SDPAGE_MASK), (ibs_op->data_physical_address & SDPAGE_MASK), domain, vcpu, &s_page);
   spin_unlock_irqrestore(&tree->lock, flags);

#elif PER_CPU_RBTREE == 1
   tree = pages[cpu];
   new = _fetch_page(tree, (ibs_op->data_linear_address & SDPAGE_MASK), (ibs_op->data_physical_address & SDPAGE_MASK), domain, vcpu, &s_page);

#else
   tree = pages[node];

   spin_lock_irqsave(&tree->lock, flags);
   new = _fetch_page(tree, (ibs_op->data_linear_address & SDPAGE_MASK), (ibs_op->data_physical_address & SDPAGE_MASK), domain, vcpu, &s_page);
   spin_unlock_irqrestore(&tree->lock, flags);
#endif

   core_stats = &this_cpu(rbtree_core_stats);
   if(s_page) {
      _update_page_stat(s_page, node, tid_index, ibs_op);
      if(new) {
         core_stats->nr_pages_in_tree++;
      }
   }
   else {
      core_stats->total_samples_missed++;
   }

   if(ibs_op->cache_infos & IBS_RECORD_STOP) {
      core_stats->nr_st_samples ++;
   }
   if(ibs_op->cache_infos & IBS_RECORD_LDOP) {
      core_stats->nr_ld_samples ++;
   }

   return;
}


int cmp_nb_accesses (const void * a, const void * b) {
   struct sdpage * page_a = (struct sdpage *) a;
   struct sdpage * page_b = (struct sdpage *) b;

   int nb_acc_a = 0;
   int nb_acc_b = 0;

   int i;
   for (i = 0; i < num_online_nodes(); i++) {
      nb_acc_a += page_a->nb_accesses[i];
      nb_acc_b += page_b->nb_accesses[i];
   }

   // We are doing a reverse sort
   return ( nb_acc_b - nb_acc_a );
}

static inline void _merge_trees(struct pagetree * src, struct pagetree * dest, struct pagetree * dest_huge) {
   int i;
   for(i = 0; i < src->reserve.index; i++) {
      struct sdpage *page, *new;
      struct sdpage *new_huge;
      int node;

      page = &src->reserve.pages[i];

      _fetch_page(dest, (unsigned long) page->page_lin, (unsigned long) page->page_phys, page->domain, page->last_vcpu, &new);
      _fetch_page(dest_huge, ((unsigned long) page->page_lin) & HGPAGE_MASK, ((unsigned long) page->page_phys) & HGPAGE_MASK, page->domain, page->last_vcpu, &new_huge);

      if(new) {
         if(page->accessed_by_multiple_threads) {
            new->accessed_by_multiple_threads = 1;
         }

         for_each_online_node(node) {
            new->nb_accesses[node] += page->nb_accesses[node];
            new->nb_writes += page->nb_writes;
         }
      }

      if(new_huge) {
         if(page->accessed_by_multiple_threads) {
            new_huge->accessed_by_multiple_threads = 1;
         }

         if(page->huge) {
            new_huge->huge = page->huge;
         }

         for_each_online_node(node) {
            new_huge->nb_accesses[node] += page->nb_accesses[node];
            new_huge->nb_writes += page->nb_writes;
         }
      }
   }
}

void get_rbtree(struct pagetree ** tree, struct pagetree ** tree_huge) {
   int i;

   //TODO: Support compaction and history
   _rbtree_init(pages_huge_global, MAX_PAGES_TO_WATCH_ACCURATE);
   _rbtree_init(pages_global, MAX_PAGES_TO_WATCH_ACCURATE);

   for(i = 0; i < nr_pagetrees; i++) {
      _merge_trees(pages[i], pages_global, pages_huge_global);
   }

   *tree = pages_global;
   *tree_huge = pages_huge_global;
}

void rbtree_load_module(void) {
   int i;

#if PER_CPU_RBTREE == 0
   nr_pagetrees = 1;
#elif PER_CPU_RBTREE == 1
   nr_pagetrees = num_online_cpus();
#else
   nr_pagetrees = num_online_nodes();
#endif

   pages = kmalloc(nr_pagetrees * sizeof(struct pagetree*));
   for(i = 0; i < nr_pagetrees ; i++) {
      struct pagetree * tree = vzalloc_node(sizeof(struct pagetree), cpu_to_node(i));
      if(!tree) {
         printk("Per cpu rbtree allocation has failed !\n");
         BUG();
      }

      pages[i] = tree;
      printu("Allocated %lu bytes for core %d\n", sizeof(*tree), i);
   }

   pages_global = kzalloc(sizeof(*pages_global));
   pages_huge_global = kzalloc(sizeof(*pages_huge_global));
   printu("Allocated %lu bytes for global structures\n",
	  sizeof(*pages_global) + sizeof(*pages_huge_global));
}

void rbtree_remove_module(void) {
   int i;
   for(i = 0; i < nr_pagetrees; i++) {
      vfree(pages[i]);
   }
   kfree(pages);
   kfree(pages_global);
   kfree(pages_huge_global);
}

void rbtree_clean(void) {
   //printk("Before cleaning: %d 4k pages and %d 2M pages in trees\n", pages.reserve.index, pages_huge.reserve.index);
   //sort(&pages_huge.reserve.pages, pages_huge.reserve.index, sizeof(struct sdpage), cmp_nb_accesses, NULL);

   // TODO:take the lock
   //rbtree_print(&pages_huge);
}


void rbtree_print(struct pagetree * tree) {
   int i;
   
   for(i = 0; i < tree->reserve.index; i++) {
      struct sdpage * this  = &(tree->reserve.pages[i]);
      int j;
      int total = 0;

      printk("Page: %16lx %16lx [ ", (long unsigned)this->page_phys, (long unsigned)this->page_lin);
      for(j = 0; j < num_online_nodes(); j++) {
         printk("%10d ", this->nb_accesses[j]);
         total += this->nb_accesses[j];
      }

      printk("], ( %10d ), [ %6d ]\n", total, this->domain);
   }
}
