#include <asm/ibs.h>
#include <asm/pebs.h>
#include <xen/cpumask.h>
#include <xen/lib.h>
#include <xen/percpu.h>

/* static void pebs_nmi_handler(struct pebs_record *record, int cpu) */
/* { */
/*     printk("CPU[%d] <= 0x%lx\n", cpu, record->data_linear_address); */
/* } */

static int enable_monitoring_pebs(void)
{
    /* int ret; */

    /* ret = pebs_acquire(); */
    /* if ( ret ) */
    /*     return ret; */

    /* pebs_setevent(PEBS_MUOPS | PEBS_MUOPS_ALLLD); */
    /* pebs_setrate(0x10000); */
    /* pebs_sethandler(pebs_nmi_handler); */
    /* pebs_enable(); */
    printk("PEBS useless in virtualization context !\n");
    return -1;
}

static void disable_monitoring_pebs(void)
{
    /* pebs_disable(); */
    /* pebs_release(); */
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
