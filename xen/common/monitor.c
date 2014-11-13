#include <asm/ibs.h>
#include <asm/pebs.h>
#include <xen/cpumask.h>
#include <xen/lib.h>
#include <xen/percpu.h>

DEFINE_PER_CPU(struct pebs_control, pebs_control);

static void pebs_nmi_handler(struct pebs_record *record, int cpu)
{
    printk("CPU[%d] <= 0x%lx\n", cpu, record->data_linear_address);
}

static int enable_monitoring_pebs(void)
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

static void disable_monitoring_pebs(void)
{
    int cpu;

    for_each_cpu(cpu, &cpu_online_map)
        pebs_control_deinit(&per_cpu(pebs_control, cpu));
}


static void ibs_nmi_handler(struct ibs_record *record, int cpu)
{
    if ( !(record->record_mode & IBS_RECORD_MODE_OP) )
        return;
    if ( !(record->record_mode & IBS_RECORD_MODE_DLA) )
        return;
    printk("CPU[%d] <= 0x%lx\n", cpu, record->data_linear_address);
}

static int enable_monitoring_ibs(void)
{
    int ret;

    ret = ibs_acquire();
    if ( ret )
        return ret;

    ibs_setevent(IBS_EVENT_OP);
    ibs_setrate(0x10000000);
    ibs_sethandler(ibs_nmi_handler);
    ibs_enable();

    return 0;
}

static void disable_monitoring_ibs(void)
{
    ibs_disable();
    ibs_release();
}


int enable_monitoring(void)
{
    if ( ibs_capable() )
        return enable_monitoring_ibs();
    else if ( pebs_capable() )
        return enable_monitoring_pebs();

    printk("Cannot find monitoring facility\n");
    return -1;
}

void disable_monitoring(void)
{
    if ( ibs_capable() )
        disable_monitoring_ibs();
    else if ( pebs_capable() )
        disable_monitoring_pebs();
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
