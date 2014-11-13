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


/*
 * Indicate if the boot cpu can use the PEBS facility.
 * Return 1 if PEBS is usable, 0 otherwise.
 */
int pebs_capable(void);

/*
 * Reserve the PEBS facility resources.
 * This function should be called before any attempt tu use the PEBS facility.
 * Return 0 if the PEBS facility has been reserved.
 */
int pebs_acquire(void);

/*
 * Release the PEBS facility resources.
 * This function should be called after a successfull return of pebs_acquire(),
 * once the PEBS facility is no longer used.
 */
void pebs_release(void);

/*
 * Set the event type to be sampled.
 * The list of events can be found in the Intel IA32 Developer's Manual.
 * Be sure to specify an event defined for the current model of cpu.
 * Return 0 in case of success.
 */
int pebs_setevent(unsigned long event);

/*
 * Set the sample rate for the PEBS control unit.
 * The rate is the count of event triggering the sampled event to ignore before
 * to tag an event to actually trigger the handler.
 * More the rate is low, more the NMI will happen often.
 * Be carefull, a low rate can freeze the machine in infinite NMI.
 * Return 0 in case of success.
 */
int pebs_setrate(unsigned long rate);

/*
 * Set the handler to be called at ech sampling.
 * The handler is called in an NMI context.
 * Return 0 in case of success.
 */
int pebs_sethandler(void (*handler)(struct pebs_record *record, int cpu));


/*
 * Start to sample.
 * Return 0 in case of success.
 */
int pebs_enable(void);

/*
 * Stop to sample.
 */
void pebs_disable(void);


#endif
