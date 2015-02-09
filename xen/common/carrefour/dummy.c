#include <xen/carrefour/carrefour_main.h>


int enable_carrefour;

unsigned enable_replication;
unsigned enable_interleaving;
unsigned enable_migration;

unsigned long sampling_rate;
unsigned long nr_accesses_node[MAX_NUMNODES];


unsigned long min_lin_address;
unsigned long max_lin_address;

struct carrefour_global_stats global_stats;


struct carrefour_run_stats run_stats;


const struct carrefour_module_option_t carrefour_module_options[CARREFOUR_OPTIONS_MAX];
