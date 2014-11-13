#ifndef ASM_X86__PEBS_H
#define ASM_X86__PEBS_H


#include <xen/types.h>


/*
 * Some events and their umasks which can be sampled with PEBS.
 * For more informations, please refers to the Intel IA32 Developer's Manual.
 */
#define PEBS_INST                (0x00c1)
#define  PEBS_INST_PDIST         (0x0100)
#define PEBS_UOPS                (0x00c2)
#define  PEBS_UOPS_ALL           (0x0100)
#define  PEBS_UOPS_SLOT          (0x0200)
#define PEBS_BRINST              (0x00c4)
#define  PEBS_BRINST_COND        (0x0100)
#define  PEBS_BRINST_NEARCL      (0x0200)
#define  PEBS_BRINST_ALL         (0x0400)
#define  PEBS_BRINST_NEARR       (0x0800)
#define  PEBS_BRINST_NEART       (0x2000)
#define PEBS_BRMISP              (0x00c5)
#define  PEBS_BRMISP_COND        (0x0100)
#define  PEBS_BRMISP_NEARCL      (0x0200)
#define  PEBS_BRMISP_ALL         (0x0400)
#define  PEBS_BRMISP_NOTTK       (0x1000)
#define  PEBS_BRMISP_TAKEN       (0x2000)
#define PEBS_MUOPS               (0x00d0)
#define  PEBS_MUOPS_TLBMSLD      (0x1100)
#define  PEBS_MUOPS_TLBMSST      (0x1200)
#define  PEBS_MUOPS_LCKLD        (0x2100)
#define  PEBS_MUOPS_SPLLD        (0x4100)
#define  PEBS_MUOPS_SPLST        (0x4200)
#define  PEBS_MUOPS_ALLLD        (0x8100)
#define  PEBS_MUOPS_ALLST        (0x8200)
#define PEBS_MLUOPS              (0x00d1)
#define  PEBS_MLUOPS_LIHIT       (0x0100)
#define  PEBS_MLUOPS_L2HIT       (0x0200)
#define  PEBS_MLUOPS_L3HIT       (0x0300)
#define  PEBS_MLUOPS_HITLFB      (0x4000)


/*
 * The record for a PEBS event.
 * The cpu registers ip, ax, bx etc... contains the cpu state for the
 * instruction following the sampled one.
 * The other fields are standing for the sampled instruction.
 */
struct pebs_record
{
    /* fields available for all 64 bits architectures */
    u64 flags, ip;
    u64 ax, bx, cx, dx;
    u64 si, di, bp, sp;
    u64 r8, r9, r10, r11;
    u64 r12, r13, r14, r15;

    /* fields available since Nehalem architecture */
    u64 ia32_perf_global_status;
    u64 data_linear_address;
    u64 data_source_encoding;
    u64 latency_value;

    /* fields available since Haswell architecture */
    u64 eventing_ip;
    u64 tx_abort_information;
} __attribute__((packed));

/*
 * Handle an NMI, checking if the reason is PEBS and then, dispatching to
 * appropriate handlers.
 */
int nmi_pebs(int cpu);


/* NMI pebs sample handler type */
/* Handle a PEBS sample on the given cpu */
typedef void (*pebs_handler_t)(struct pebs_record *record, int cpu);

/*
 * A pebs controller object.
 * This should be used as an opaque interface to control PEBS mechanisms.
 */
struct pebs_control
{
    int                   cpu;                 /* on what cpu the PEBS run */
    int                   enabled;             /* is the PEBS running */
    struct debug_store   *debug_store;         /* cpu ds area content */
    struct pebs_record   *pebs_records;        /* records array location */
};


#define pebs_capable()   (0)

/*
 * Initialize a PEBS control unit for the given cpus.
 * The total amount of PEBS control unit is hardware limited so this function
 * can fail if there is no more free resources on the specified cpus.
 * Return 0 in case of success.
 */
int pebs_control_init(struct pebs_control *this, int cpu);

/*
 * Finalize a PEBS control unit.
 * Free the hardware resources of the given control unit on the according cpus.
 * Return 0 in case of success.
 */
int pebs_control_deinit(struct pebs_control *this);


/*
 * Set the event the PEBS control unit samples.
 * The list of events can be found in the Intel IA32 Developer's Manual.
 * Be sure to specify an event defined for the current model of cpu.
 * Return 0 in case of success.
 */
int pebs_control_setevent(struct pebs_control *this, unsigned long event);

/*
 * Set the sample rate for the PEBS control unit.
 * The rate is the count of event triggering the sampled event to ignore before
 * to tag an event to actually trigger the handler.
 * More the rate is small, more often an interrupt will be triggered.
 * Return 0 in case of success.
 */
int pebs_control_setrate(struct pebs_control *this, unsigned long rate);

/*
 * Set the handler to call when the interrupt is triggered.
 * If the old parameter is not NULL, it is filled with the previous handler.
 * You can specify the new parameter as NULL so no handler will be called.
 * Return 0 in case of success.
 */
int pebs_control_sethandler(struct pebs_control *this, pebs_handler_t new);


/*
 * Enable the PEBS control unit so it start the sampling.
 * Return 0 in case of success.
 */
int pebs_control_enable(struct pebs_control *this);

/*
 * Disable the PEBS control unit so it does not sample anymore.
 * Return 0 in case of success.
 */
int pebs_control_disable(struct pebs_control *this);


#endif
