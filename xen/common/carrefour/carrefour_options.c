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

#include <xen/carrefour/carrefour_main.h>

#define OPTS(n, v) \
   [n] = { \
      .description = #n, \
      .value = v, \
   }

const struct carrefour_module_option_t carrefour_module_options[CARREFOUR_OPTIONS_MAX] = {
   OPTS(ENABLE_REPLICATION,      0),
   OPTS(ENABLE_INTERLEAVING,     1),
   OPTS(ENABLE_MIGRATION,        1),
   OPTS(DETAILED_STATS,          0),

   // Related to IBS
   OPTS(ADAPTIVE_SAMPLING,       1),
   
   //OPTS(IBS_RATE_ACCURATE,       0x1FFF0), //Quatchi -- ASPLOS
   //OPTS(IBS_RATE_CHEAP,          0x3FFE0), //Quatchi -- ASPLOS
   //OPTS(IBS_RATE_NO_ADAPTIVE,    0x1FFF0), //Quatchi -- ASPLOS

   //OPTS(IBS_RATE_ACCURATE,       0x3FFE0), //Quatchi
   //OPTS(IBS_RATE_CHEAP,          0x7FFC0), //Quatchi
   //OPTS(IBS_RATE_NO_ADAPTIVE,    0x1FFF0), //Quatchi

   //OPTS(IBS_RATE_ACCURATE,       0x5FFF0), //Bulldozer
   //OPTS(IBS_RATE_CHEAP,          0xFFFE0), //Bulldozer
   //OPTS(IBS_RATE_NO_ADAPTIVE,    0x8FFF0), //Bulldozer

   OPTS(IBS_RATE_ACCURATE,       0xFFFF0), //Bulldozer
   OPTS(IBS_RATE_CHEAP,          0xFFFE0), //Bulldozer
   OPTS(IBS_RATE_NO_ADAPTIVE,    0xFFFF0), //Bulldozer

   OPTS(IBS_INSTRUCTION_BASED,   0),
   OPTS(IBS_CONSIDER_CACHES,     1),

   OPTS(IBS_ADAPTIVE_MAGIC,      10),

   // Related to migration
   OPTS(PAGE_BOUNCING_FIX_4K,    0),
   OPTS(PAGE_BOUNCING_FIX_2M,    1),

   // Related to replication
   OPTS(REPLICATION_PER_TID,     0),

   // Related to huge pages
   OPTS(MIGRATE_REGULAR_HP,      0),

   OPTS(SPLIT_SHARED_THP,        0),
   OPTS(INTERLEAVE_SHARED_THP,   1),

   OPTS(SPLIT_MISPLACED_THP,     0),
   OPTS(MIGRATE_MISPLACED_THP,   1),

   OPTS(RESET_HP_TREE,           1),
   OPTS(ENABLE_TREE_COMPACTION,  0),

   OPTS(CONSIDER_2M_PAGES,       1),
   OPTS(CONSIDER_4K_PAGES,       1),
};  


void print_module_options (void) {
   int i = 0;

   printk("Carrefour options:\n");
   for(i = 0; i < CARREFOUR_OPTIONS_MAX; i++) {
      printk("\t* %30s => %d\n", carrefour_module_options[i].description, carrefour_module_options[i].value);
   }
}

int validate_module_options (void) {
   int validated = 1;
   //if(carrefour_module_options[SPLIT_SHARED_THP].value && carrefour_module_options[INTERLEAVE_SHARED_THP].value) {
      //printk("You have to choose between SPLIT_SHARED_THP and INTERLEAVE_SHARED_THP\n");
      //validated = 0;
      //}

   //if(carrefour_module_options[SPLIT_MISPLACED_THP].value && carrefour_module_options[MIGRATE_MISPLACED_THP].value) {
      //printk("You have to choose between SPLIT_MISPLACED_THP and MIGRATE_MISPLACED_THP\n");
      //validated = 0;
   //}

   return validated;
}
