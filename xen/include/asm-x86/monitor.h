#ifndef ASM_X86__MONITOR_H
#define ASM_X86__MONITOR_H

void monitor_intel(void);


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


/* NMI pebs sample handler type */
/* Handle a PEBS sample on the given cpu */
typedef void (*pebs_handler_t)(struct pebs_record *record, int cpu);

struct pmc;      /* opaque structure containing PMC informations */

struct pebs_control
{
    int               enabled;               /* currently sampling */
    struct pmc       *pmc;                   /* PMC used for the PEBS */
    pebs_handler_t    handler;               /* callback for nmi interrupt */
};


/*
 * Initialize a PEBS control unit for the given cpus.
 * The total amount of PEBS control unit is hardware limited so this function
 * can fail if there is no more free resources on the specified cpus.
 * Return 0 in case of success.
 */
int pebs_control_init(struct pebs_control *this);

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
int pebs_control_sethandler(struct pebs_control *this, pebs_handler_t new,
                            pebs_handler_t *old);


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


void test_setup(void);
void test_teardown(void);

#endif /* ASM_X86__MONITOR_H */
