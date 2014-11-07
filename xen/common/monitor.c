#include <asm/pebs.h>
#include <xen/cpumask.h>
#include <xen/lib.h>
#include <xen/percpu.h>

DEFINE_PER_CPU(struct pebs_control, pebs_control);

static void pebs_nmi_handler(struct pebs_record *record, int cpu)
{
    printk("CPU[%d] <= 0x%lx\n", cpu, record->data_linear_address);
}

static int enable_monitoring_intel(void)
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

static void disable_monitoring_intel(void)
{
    int cpu;

    for_each_cpu(cpu, &cpu_online_map)
        pebs_control_deinit(&per_cpu(pebs_control, cpu));
}


int enable_monitoring(void)
{
    return enable_monitoring_intel();
}

void disable_monitoring(void)
{
    disable_monitoring_intel();
}
