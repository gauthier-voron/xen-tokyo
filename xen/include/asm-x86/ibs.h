#ifndef ASM_X86__IBS_H
#define ASM_X86__IBS_H


#include <asm/regs.h>
#include <xen/types.h>


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

#define IBS_FETCH_RAND_EN               (1ULL<<57)
#define IBS_FETCH_VAL                   (1ULL<<49)
#define IBS_FETCH_ENABLE                (1ULL<<48)
#define IBS_FETCH_CNT                   0xFFFF0000ULL
#define IBS_FETCH_MAX_CNT               0x0000FFFFULL

#define IBS_OP_CNT_CTL                  (1ULL<<19)
#define IBS_OP_VAL                      (1ULL<<18)
#define IBS_OP_ENABLE                   (1ULL<<17)
#define IBS_OP_CNT                      0x7FFFFFF00000000ULL
#define IBS_OP_MAX_CNT                  0x0000FFFFULL


#define IBS_EVENT_FETCH          (1UL <<  0)  /* sample fetch events */
#define IBS_EVENT_OP             (1UL <<  1)  /* sample execution events */


#define IBS_RECORD_MODE_FETCH    (1UL <<  0)  /* ibs record is for a fetch */
#define IBS_RECORD_MODE_OP       (1UL <<  1)  /* ibs record is for an op */
#define IBS_RECORD_MODE_ILA      (1UL <<  2)  /* inst_linear_address valid */
#define IBS_RECORD_MODE_IPA      (1UL <<  3)  /* inst_physical_address valid */
#define IBS_RECORD_MODE_DLA      (1UL <<  4)  /* data_linear_address valid */
#define IBS_RECORD_MODE_DPA      (1UL <<  5)  /* data_physical_address valid */
#define IBS_RECORD_MODE_BI       (1UL <<  6)  /* branch_infos valid */
#define IBS_RECORD_MODE_NI       (1UL <<  7)  /* northbridge_infos valid */
#define IBS_RECORD_MODE_CI       (1UL <<  8)  /* cache_infos valid */

#define IBS_RECORD_TLBMISS       (1UL << 55)  /* fetch L1 TLB miss */
#define IBS_RECORD_TLBSIZE_4K    (0UL << 53)  /* fetch L1 TLB miss 4K */
#define IBS_RECORD_TLBSIZE_2M    (1UL << 53)  /* fetch L1 TLB miss 2M */
#define IBS_RECORD_TLBSIZE_1G    (1UL << 54)  /* fetch L1 TLB miss 1G */
#define IBS_RECORD_IPA           (1UL << 52)  /* inst_physical_address valid */
#define IBS_RECORD_CACHEMISS     (1UL << 51)  /* instruction cache miss */
#define IBS_RECORD_LATENCY(i)    (((i) >> 32) & 0xFF) /* fetch latency */

#define IBS_RECORD_ILA           (1UL << 38)  /* inst_linear_address valid */
#define IBS_RECORD_BRNRET        (1UL << 37)  /* op branch retired */
#define IBS_RECORD_BRNMISP       (1UL << 36)  /* op branch mispredicted */
#define IBS_RECORD_BRNTAKEN      (1UL << 35)  /* op branch taken */
#define IBS_RECORD_RET           (1UL << 34)  /* op RET */

#define IBS_RECORD_DCMISSLAT(i)  (((i) >> 32) & 0xFF) /* dcache miss latency */
#define IBS_RECORD_DPA           (1UL << 18)  /* data_physical_address valid */
#define IBS_RECORD_DLA           (1UL << 17)  /* data_linear_address valid */
#define IBS_RECORD_DCLOCK        (1UL << 15)  /* data cache locked op */
#define IBS_RECORD_DCLOCK        (1UL << 15)  /* data cache locked op */
#define IBS_RECORD_UCMEMACC      (1UL << 14)  /* data cache UC mem access */
#define IBS_RECORD_WCMEMACC      (1UL << 13)  /* data cache WC mem access */
#define IBS_RECORD_DCMISACC      (1UL <<  8)  /* data cache misaligned pnlty */
#define IBS_RECORD_DCMISS        (1UL <<  7)  /* data cache miss */
#define IBS_RECORD_L2TLBHIT2M    (1UL <<  6)  /* data L2 TLB hit 2M */
#define IBS_RECORD_L1TLBHIT1G    (1UL <<  5)  /* data L1 TLB hit 1G */
#define IBS_RECORD_L1TLBHIT2M    (1UL <<  4)  /* data L1 TLB hit 2M */
#define IBS_RECORD_DCL2TLBMISS   (1UL <<  3)  /* data L2 TLB miss */
#define IBS_RECORD_DCL1TLBMISS   (1UL <<  2)  /* data L1 TLB miss */
#define IBS_RECORD_STOP          (1UL <<  1)  /* store operation */
#define IBS_RECORD_LDOP          (1UL <<  0)  /* load operation */


/*
 * An IBS sample record.
 * This is the structure which will be given to an IBS handler.
 * It can contain two kind of informations: fetch or operation.
 * What kind of information is specified by the record_mode field.
 */
struct ibs_record
{
    u8  record_mode;                  /* properties about the record */
    u64 inst_linear_address;          /* linear address of the instruction */
    u64 inst_physical_address;        /* physical address of the instruction */
    u64 data_linear_address;          /* linear address of the data */
    u64 data_physical_address;        /* physical address of the data */
    u64 fetch_infos;                  /* informations about the fetch */
    u64 branch_infos;                 /* informations about a branch uop */
    u64 northbridge_infos;            /* informations about a NB uop */
    u64 cache_infos;                  /* informations about data cache */
};


/*
 * The nmi handler called by the do_nmi() function of arch/x86/traps.c
 * Dispatch the NMI interrupt that occured on the current cpu to the
 * appropriate handler.
 * Return 1 if the NMI was an IBS-related NMI, 0 otherwise.
 */
int nmi_ibs(const struct cpu_user_regs *regs);


/*
 * Indicate if the boot cpu can use the IBS facility.
 * Return 1 if IBS is usable, 0 otherwise.
 */
int ibs_capable(void);

/*
 * Reserve the IBS facility resources.
 * This function should be called before any attempt tu use the IBS facility.
 * Return 0 if the IBS facility has been reserved.
 */
int ibs_acquire(void);

/*
 * Release the IBS facility resources.
 * This function should be called after a successfull return of ibs_acquire(),
 * once the IBS facility is no longer used.
 */
void ibs_release(void);


/*
 * Set the event type to be sampled.
 * It can be etiher IBS_EVENT_FETCH or IBS_EVENT_OP, or a bitwise-OR of these
 * two event types.
 * Return 0 in case of success.
 */
int ibs_setevent(unsigned long event);

/*
 * Set the sampling rate.
 * The sampling rate is the count of uops to be fetched/retired before the
 * next uop is sampled.
 * More the rate is low, more the NMI will happen often.
 * Be carefull, a low rate can freeze the machine in infinite NMI.
 * Return 0 in case of success.
 */
int ibs_setrate(unsigned long rate);

/*
 * Set the handler to be called at ech sampling.
 * The handler is called in an NMI context.
 * Return 0 in case of success.
 */
int ibs_sethandler(void (*handler)(const struct ibs_record *record,
				   const struct cpu_user_regs *regs));


/*
 * Start to sample uops.
 * Return 0 in case of success.
 */
int ibs_enable(void);

/*
 * Stop to sample uops.
 */
void ibs_disable(void);


#endif
