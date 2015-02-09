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

#ifndef __CARREFOUR_OPTIONS__
#define __CARREFOUR_OPTIONS__

enum carrefour_available_options {
   // General purpose
   ENABLE_REPLICATION = 0,
   ENABLE_MIGRATION,
   ENABLE_INTERLEAVING,
   DETAILED_STATS,

   // Related to IBS
   ADAPTIVE_SAMPLING, 
   IBS_RATE_ACCURATE,
   IBS_RATE_CHEAP,
   IBS_RATE_NO_ADAPTIVE,
   IBS_INSTRUCTION_BASED,
   IBS_CONSIDER_CACHES,
   IBS_ADAPTIVE_MAGIC,

   // Related to migration
   PAGE_BOUNCING_FIX_4K,
   PAGE_BOUNCING_FIX_2M,

   // Related to replication
   REPLICATION_PER_TID,

   // Related to huge pages
   MIGRATE_REGULAR_HP,

   SPLIT_SHARED_THP,
   INTERLEAVE_SHARED_THP,

   SPLIT_MISPLACED_THP,
   MIGRATE_MISPLACED_THP,

   RESET_HP_TREE,
   ENABLE_TREE_COMPACTION,

   CONSIDER_2M_PAGES,
   CONSIDER_4K_PAGES,

   // Dummy value
   CARREFOUR_OPTIONS_MAX,
};

struct carrefour_module_option_t {
   char *   description;
   int      value;
};

struct carrefour_options_t {
   int page_bouncing_fix_4k;
   int page_bouncing_fix_2M;
   int async_4k_migrations;
};

extern const struct carrefour_module_option_t carrefour_module_options[CARREFOUR_OPTIONS_MAX];

void print_module_options (void);
int validate_module_options (void);

#endif
