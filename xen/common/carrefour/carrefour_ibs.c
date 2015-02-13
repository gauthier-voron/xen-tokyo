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

#include <asm/ibs.h>
#include <asm/paging.h>
#include <asm/msr.h>
#include <xen/carrefour/carrefour_main.h>
#include <xen/percpu.h>
#include <xen/sched.h>

#if ! FAKE_IBS
unsigned long min_lin_address = 0;
unsigned long max_lin_address = (unsigned long) (-1);
unsigned long sampling_rate;

struct ibs_stats {
   uint64_t total_interrupts;
   uint64_t total_samples;
   uint64_t total_samples_L3DRAM;
   uint64_t total_sample_overflow;
   uint64_t time_spent_in_NMI;
};
static DEFINE_PER_CPU(struct ibs_stats, stats);

static unsigned long find_gfn_for_vaddr(unsigned long vaddr)
{
    unsigned long gfn = INVALID_GFN;
    uint32_t pfec;

    if ( this_cpu(curr_vcpu) != current )
        goto out;

    local_irq_enable();
    pfec = PFEC_page_present;

    gfn = try_paging_gva_to_gfn(current, vaddr, &pfec);

    local_irq_disable();

 out:
    return gfn;
}

// This function creates a struct ibs_op_sample struct that contains all IBS info
// This structure is passed to rbtree_add_sample which stores pages info in a rbreee.
static void handle_ibs_nmi(const struct ibs_record *record,
			   const struct cpu_user_regs *regs) {
   int cpu = smp_processor_id();
   unsigned long gfn;
   uint64_t time_start,time_stop;

   rdtscll(time_start);

   per_cpu(stats, cpu).total_interrupts++;

   if ( current->domain->guest_type != guest_type_hvm )
       goto end;

   // If the sample does not touch DRAM, stop.
   if((!carrefour_module_options[IBS_CONSIDER_CACHES].value)
      && ((record->northbridge_infos & 7) == 0))
       goto end;
   
   if((record->northbridge_infos & 7) == 3) 
       per_cpu(stats, cpu).total_samples_L3DRAM++;

   gfn = find_gfn_for_vaddr(record->data_linear_address);
   if (gfn == INVALID_GFN)
       goto end;
   ((struct ibs_record *) record)->data_linear_address = gfn << PAGE_SHIFT;

   if(record->data_physical_address == 0)
       goto end;

   if(record->data_linear_address < min_lin_address
      || record->data_linear_address > max_lin_address)
       goto end;

   per_cpu(stats, cpu).total_samples++;

   /* rbtree_add_sample(!guest_mode(regs), record, smp_processor_id(), */
   /* 		     current->vcpu_id, current->domain->domain_id); */
   rbtree_add_sample(0, record, smp_processor_id(),
		     current->vcpu_id, current->domain->domain_id);

end:
   rdtscll(time_stop);
   per_cpu(stats, cpu).time_spent_in_NMI += time_stop - time_start;
}

/**
 * Init and exit
 */
int carrefour_ibs_init(void) {
   int err;

   if (!boot_cpu_has(X86_FEATURE_IBS)) {
      printk(KERN_ERR "ibs: AMD IBS not present in hardware\n");
      return -1;
   }

   if(carrefour_module_options[ADAPTIVE_SAMPLING].value) {
      sampling_rate = carrefour_module_options[IBS_RATE_CHEAP].value;
   }
   else {
      sampling_rate = carrefour_module_options[IBS_RATE_NO_ADAPTIVE].value;
   }

   printk("ibs: AMD IBS detected\n");

   err = ibs_acquire();
   if (err)
      return err;

   err = ibs_sethandler(handle_ibs_nmi);
   if(err)
      return err;

   return 0;
}

void carrefour_ibs_exit(void) {
   ibs_release();
}

void carrefour_ibs_start() {
   int cpu;
   for_each_online_cpu(cpu) {
      memset(&per_cpu(stats, cpu), 0, sizeof(per_cpu(stats, cpu)));
   }

   ibs_setevent(IBS_EVENT_OP);
   ibs_setrate(sampling_rate);
   ibs_enable();
}

int carrefour_ibs_stop() {
   int cpu, total_interrupts = 0, total_samples = 0, total_samples_L3DRAM = 0;
   ibs_disable();

   for_each_online_cpu(cpu) {
	   total_interrupts += per_cpu(stats, cpu).total_interrupts;
	   total_samples += per_cpu(stats, cpu).total_samples;
	   total_samples_L3DRAM += per_cpu(stats, cpu).total_samples_L3DRAM;
	   run_stats.time_spent_in_NMI += per_cpu(stats, cpu).time_spent_in_NMI;
   }
   printk("Sampling: %lx op=%d considering L1&L2=%d\n", sampling_rate, carrefour_module_options[IBS_INSTRUCTION_BASED].value, carrefour_module_options[IBS_CONSIDER_CACHES].value);
   printk("Total interrupts %d Total Samples %d\n", total_interrupts, total_samples);

   return total_samples_L3DRAM;
}

#endif

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
