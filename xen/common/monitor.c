#include <xen/types.h>
#include <public/xen.h>

#include <asm/ibs.h>
#include <asm/msr.h>
#include <asm/pebs.h>
#include <asm/xenoprof.h>
#include <xen/cpumask.h>
#include <xen/lib.h>
#include <xen/percpu.h>
#include <xen/sched.h>

DEFINE_PER_CPU(struct pebs_control, pebs_control);

static void pebs_nmi_handler(struct pebs_record *record, int cpu)
{
    printk("CPU[%d] <= 0x%lx\n", cpu, record->data_linear_address);
}

static __attribute__((unused)) int enable_monitoring_intel(void)
{
    int cpu;
    struct pebs_control *pbct;

    for_each_cpu(cpu, &cpu_online_map)
    {
        pbct = &per_cpu(pebs_control, cpu);
        pebs_control_init(pbct, cpu);
        pebs_control_setevent(pbct, PEBS_MUOPS | PEBS_MUOPS_ALLLD);
        pebs_control_setrate(pbct, 0x10000);
        pebs_control_sethandler(pbct, pebs_nmi_handler);
        pebs_control_enable(pbct);
    }

    return 0;
}

static __attribute__((unused)) void disable_monitoring_intel(void)
{
    int cpu;

    for_each_cpu(cpu, &cpu_online_map)
        pebs_control_deinit(&per_cpu(pebs_control, cpu));
}


#include "../arch/x86/oprofile/op_counter.h"
#define MSR_AMD64_IBSFETCHCTL           0xc0011030
#define MSR_AMD64_IBSFETCHLINAD         0xc0011031
#define MSR_AMD64_IBSFETCHPHYSAD        0xc0011032
#define MSR_AMD64_IBSOPCTL              0xc0011033
#define MSR_AMD64_IBSOPRIP              0xc0011034
#define MSR_AMD64_IBSOPDATA             0xc0011035
#define MSR_AMD64_IBSOPDATA2            0xc0011036
#define MSR_AMD64_IBSOPDATA3            0xc0011037
#define MSR_AMD64_IBSDCLINAD            0xc0011038
#define MSR_AMD64_IBSDCPHYSAD           0xc0011039
#define MSR_AMD64_IBSCTL                0xc001103a

/* IbsFetchCtl bits/masks */
#define IBS_FETCH_RAND_EN               (1ULL<<57)
#define IBS_FETCH_VAL                   (1ULL<<49)
#define IBS_FETCH_ENABLE                (1ULL<<48)
#define IBS_FETCH_CNT                   0xFFFF0000ULL
#define IBS_FETCH_MAX_CNT               0x0000FFFFULL

/* IbsOpCtl bits */
#define IBS_OP_CNT_CTL                  (1ULL<<19)
#define IBS_OP_VAL                      (1ULL<<18)
#define IBS_OP_ENABLE                   (1ULL<<17)
#define IBS_OP_CNT                      0x7FFFFFF00000000ULL
#define IBS_OP_MAX_CNT                  0x0000FFFFULL

int nmi_ibs(int cpu)
{
    u64 val;
    struct ibs_record record;
    
    rdmsr_safe(MSR_AMD64_IBSFETCHCTL, val);
    if ( val & IBS_FETCH_ENABLE && val & IBS_FETCH_VAL )
    {
        record.record_mode = IBS_RECORD_MODE_FETCH;
        rdmsr_safe(MSR_AMD64_IBSFETCHLINAD, record.fetch_linear_address);
        rdmsr_safe(MSR_AMD64_IBSFETCHPHYSAD, record.fetch_physical_address);

        printk("[%d] fetch enabled on %d\n", cpu, current->domain->domain_id);
        printk("[%d] linear address = 0x%lx\n", cpu,
               record.fetch_linear_address);
        printk("[%d] physical address = 0x%lx\n", cpu,
               record.fetch_physical_address);

        val &= ~(IBS_FETCH_VAL | IBS_FETCH_CNT);
        wrmsr_safe(MSR_AMD64_IBSFETCHCTL, val);
        
        return 1;
    }

    rdmsr_safe(MSR_AMD64_IBSOPCTL, val);
    if ( val & IBS_OP_ENABLE && val & IBS_OP_VAL )
    {
        record.record_mode = IBS_RECORD_MODE_OP;
        rdmsr_safe(MSR_AMD64_IBSOPRIP, record.op_linear_address);
        rdmsr_safe(MSR_AMD64_IBSOPDATA, record.op_branch_infos);
        rdmsr_safe(MSR_AMD64_IBSOPDATA2, record.op_northbridge_infos);
        rdmsr_safe(MSR_AMD64_IBSOPDATA3, record.op_cache_infos);
        rdmsr_safe(MSR_AMD64_IBSDCLINAD, record.op_data_linear_address);
        rdmsr_safe(MSR_AMD64_IBSDCPHYSAD, record.op_data_physical_address);
        
        printk("[%d] op enabled on %d\n", cpu, current->domain->domain_id);
        printk("[%d] op linear address = 0x%lx\n", cpu,
               record.op_linear_address);
        printk("[%d] op branch infos = 0x%lx\n", cpu,
               record.op_branch_infos);
        printk("[%d] op northbridge infos = 0x%lx\n", cpu,
               record.op_northbridge_infos);
        printk("[%d] op cache infos = 0x%lx\n", cpu,
               record.op_cache_infos);
        printk("[%d] op data linear address = 0x%lx\n", cpu,
               record.op_data_linear_address);
        printk("[%d] op data physical address = 0x%lx\n", cpu,
               record.op_data_physical_address);

        val &= ~(IBS_OP_VAL | IBS_OP_CNT);
        wrmsr_safe(MSR_AMD64_IBSOPCTL, val);
        
        return 1;
    }

    return 0;
    
    /* if (ibs_config.fetch_enabled) { */
    /*     	rdmsrl(MSR_AMD64_IBSFETCHCTL, ctl); */
    /*     	if (ctl & IBS_FETCH_VAL) { */
    /*     		rdmsrl(MSR_AMD64_IBSFETCHLINAD, val); */
    /*     		xenoprof_log_event(v, regs, IBS_FETCH_CODE, mode, 0); */
    /*     		xenoprof_log_event(v, regs, val, mode, 0); */

    /*     		ibs_log_event(val, regs, mode); */
    /*     		ibs_log_event(ctl, regs, mode); */

    /*     		rdmsrl(MSR_AMD64_IBSFETCHPHYSAD, val); */
    /*     		ibs_log_event(val, regs, mode); */
		
    /*     		/\* reenable the IRQ *\/ */
    /*     		ctl &= ~(IBS_FETCH_VAL | IBS_FETCH_CNT); */
    /*     		ctl |= IBS_FETCH_ENABLE; */
    /*     		wrmsrl(MSR_AMD64_IBSFETCHCTL, ctl); */
    /*     	} */
    /*     } */

    /*     if (ibs_config.op_enabled) { */
    /*     	rdmsrl(MSR_AMD64_IBSOPCTL, ctl); */
    /*     	if (ctl & IBS_OP_VAL) { */

    /*     		rdmsrl(MSR_AMD64_IBSOPRIP, val); */
    /*     		xenoprof_log_event(v, regs, IBS_OP_CODE, mode, 0); */
    /*     		xenoprof_log_event(v, regs, val, mode, 0); */
			
    /*     		ibs_log_event(val, regs, mode); */

    /*     		rdmsrl(MSR_AMD64_IBSOPDATA, val); */
    /*     		ibs_log_event(val, regs, mode); */
    /*     		rdmsrl(MSR_AMD64_IBSOPDATA2, val); */
    /*     		ibs_log_event(val, regs, mode); */
    /*     		rdmsrl(MSR_AMD64_IBSOPDATA3, val); */
    /*     		ibs_log_event(val, regs, mode); */
    /*     		rdmsrl(MSR_AMD64_IBSDCLINAD, val); */
    /*     		ibs_log_event(val, regs, mode); */
    /*     		rdmsrl(MSR_AMD64_IBSDCPHYSAD, val); */
    /*     		ibs_log_event(val, regs, mode); */

    /*     		/\* reenable the IRQ *\/ */
    /*     		ctl = op_amd_randomize_ibs_op(ibs_op_ctl); */
    /*     		wrmsrl(MSR_AMD64_IBSOPCTL, ctl); */
    /*     	} */
    /*     } */
}

static int enable_monitoring_amd(void)
{
    int ret;

    ret = xenoprof_arch_reserve_counters();
    printk("reserve counters = %d\n", ret);

    ibs_config.op_enabled = 1;
    ibs_config.fetch_enabled = 0;
    ibs_config.max_cnt_fetch = 0;
    ibs_config.max_cnt_op = 0x10000000;
    ibs_config.rand_en = 0;
    ibs_config.dispatched_ops = 0;

    ret = xenoprof_arch_start();
    printk("start = %d\n", ret);
    
    return ret;
}

static void disable_monitoring_amd(void)
{
    xenoprof_arch_stop();
    xenoprof_arch_release_counters();
}


int enable_monitoring(void)
{
    return enable_monitoring_amd();
}

void disable_monitoring(void)
{
    disable_monitoring_amd();
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
