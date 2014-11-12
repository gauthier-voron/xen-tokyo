#ifndef ASM_X86__IBS_H
#define ASM_X86__IBS_H


#include <xen/cpumask.h>
#include <xen/types.h>


#define IBS_RECORD_MODE_FETCH    0
#define IBS_RECORD_MODE_OP       1


struct ibs_record
{
    u8  record_mode;
    u64 fetch_linear_address;
    u64 fetch_physical_address;
    u64 op_linear_address;
    u64 op_branch_infos;
    u64 op_northbridge_infos;
    u64 op_cache_infos;
    u64 op_data_linear_address;
    u64 op_data_physical_address;
};

struct ibs_control
{
    int       enabled;
    cpumask_t cpumask;
};


int nmi_ibs(int cpu);


int ibs_initialize(struct ibs_control *this, cpumask_t *cpumask);

void ibs_finalize(struct ibs_control *this);


int ibs_setevent(struct ibs_control *this, unsigned long event);

int ibs_setrate(struct ibs_control *this, unsigned long rate);

int ibs_sethandler(struct ibs_control *this,
                   void (*handler)(int cpu, struct ibs_record *record));


int ibs_enable(struct ibs_control *this);

void ibs_disable(struct ibs_control *this);


#endif
