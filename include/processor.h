#ifndef __PROCESSOR_H
#define __PROCESSOR_H

/* PPC bit number conversion */
#ifdef __ASSEMBLY__
#define PPC_BIT(bit)		(0x8000000000000000 >> (bit))
#else
#define PPC_BIT(bit)		(0x8000000000000000UL >> (bit))
#endif
#define PPC_BITMASK(bs,be)	((PPC_BIT(bs) - PPC_BIT(be)) | PPC_BIT(bs))
#define PPC_BITLSHIFT(be)	(63 - (be))

/* P7 MSR bits */
#define MSR_SF		PPC_BIT(0)	/* 64-bit mode */
#define MSR_HV		PPC_BIT(3)	/* Hypervisor mode */
#define MSR_VEC		PPC_BIT(38)	/* VMX enable */
#define MSR_VSX		PPC_BIT(40)	/* VSX enable */
#define MSR_EE		PPC_BIT(48)	/* External Int. Enable */
#define MSR_PR		PPC_BIT(49)       	/* Problem state */
#define MSR_FP		PPC_BIT(50)	/* Floating Point Enable */
#define MSR_ME		PPC_BIT(51)	/* Machine Check Enable */
#define MSR_FE0		PPC_BIT(52)	/* FP Exception 0 */
#define MSR_SE		PPC_BIT(53)	/* Step enable */
#define MSR_BE		PPC_BIT(54)	/* Branch trace enable */
#define MSR_FE1		PPC_BIT(55)	/* FP Exception 1 */
#define MSR_IR		PPC_BIT(58)	/* Instructions reloc */
#define MSR_DR		PPC_BIT(59)	/* Data reloc */
#define MSR_PMM		PPC_BIT(61)	/* Perf Monitor */
#define MSR_RI		PPC_BIT(62)	/* Recoverable Interrupt */
#define MSR_LE		PPC_BIT(63)	/* Little Endian */

/* PIR
 *
 * XXX This will break on P8, will do for now
 */
#define SPR_PIR_THREAD_MASK	0x3


/* SPR register definitions */
#define SPR_TBRL	0x10c	/* RO: Timebase low */
#define SPR_TBRU	0x10d	/* RO: Timebase high */
#define SPR_SPRC	0x114	/* RW: Access to uArch SPRs (ex SCOMC) */
#define SPR_SPRD	0x115	/* RW: Access to uArch SPRs (ex SCOMD) */
#define	SPR_SCOMC	0x114	/* RW: SCOM Control - old name of SPRC */
#define	SPR_SCOMD	0x115	/* RW: SCOM Data    - old name of SPRD */
#define SPR_TBWL	0x11c	/* RW: Timebase low */
#define SPR_TBWU	0x11d	/* RW: Timebase high */
#define SPR_TBU40	0x11e	/* RW: Timebase Upper 40 bit */
#define SPR_HSPRG0	0x130	/* RW: Hypervisor scratch 0 */
#define SPR_HSPRG1	0x131	/* RW: Hypervisor scratch 1 */
#define SPR_TFMR	0x13d
#define SPR_HMER	0x150	/* Hypervisor Maintenance Exception */
#define SPR_HMEER	0x151	/* HMER interrupt enable mask */
#define SPR_PIR		0x3ff	/* RO: Processor Identification */

/* Bits in TFMR - control bits */
#define SPR_TFMR_MAX_CYC_BET_STEPS_MASK	PPC_BITMASK(0,7)
#define SPR_TFMR_MAX_CYC_BET_STEPS_LSH	PPC_BITLSHIFT(7)
#define SPR_TFMR_N_CLKS_PER_STEP_MASK	PPC_BITMASK(8,9)
#define SPR_TFMR_N_CLKS_PER_STEP_LSH	PPC_BITLSHIFT(9)
#define SPR_TFMR_MASK_HMI		PPC_BIT(10)
#define SPR_TFMR_SYNC_BIT_SEL_MASK	PPC_BITMASK(11,13)
#define SPR_TFMR_SYNC_BIT_SEL_LSH	PPC_BITLSHIFT(13)
#define SPR_TFMR_TB_ECLIPZ		PPC_BIT(14)
#define SPR_TFMR_LOAD_TOD_MOD		PPC_BIT(16)
#define SPR_TFMR_MOVE_CHIP_TOD_TO_TB	PPC_BIT(18)
#define SPR_TFMR_CLEAR_TB_ERRORS	PPC_BIT(24)
/* Bits in TFMR - thread indep. status bits */
#define SPR_TFMR_HDEC_PARITY_ERROR	PPC_BIT(26)
#define SPR_TFMR_TBST_CORRUPT		PPC_BIT(27)
#define SPR_TFMR_TBST_ENCODED_MASK	PPC_BITMASK(28,31)
#define SPR_TFMR_TBST_ENCODED_LSH	PPC_BITLSHIFT(31)
#define SPR_TFMR_TBST_LAST_MASK		PPC_BITMASK(32,35)
#define SPR_TFMR_TBST_LAST_LSH		PPC_BITLSHIFT(35)
#define SPR_TFMR_TB_ENABLED		PPC_BIT(40)
#define SPR_TFMR_TB_VALID		PPC_BIT(41)
#define SPR_TFMR_TB_SYNC_OCCURED	PPC_BIT(42)
#define SPR_TFMR_TB_MISSING_SYNC	PPC_BIT(43)
#define SPR_TFMR_TB_MISSING_STEP	PPC_BIT(44)
#define SPR_TFMR_TB_RESIDUE_ERR		PPC_BIT(45)
#define SPR_TFMR_FW_CONTROL_ERR		PPC_BIT(46)
#define SPR_TFMR_CHIP_TOD_STATUS_MASK	PPC_BITMASK(47,50)
#define SPR_TFMR_CHIP_TOD_STATUS_LSH	PPC_BITLSHIFT(50)
#define SPR_TFMR_CHIP_TOD_INTERRUPT	PPC_BIT(51)
#define SPR_TFMR_CHIP_TOD_PARITY_ERR	PPC_BIT(56)
/* Bits in TFMR - thread specific. status bits */
#define SPR_TFMR_PURR_PARITY_ERR	PPC_BIT(57)
#define SPR_TFMR_SPURR_PARITY_ERR	PPC_BIT(58)
#define SPR_TFMR_DEC_PARITY_ERR		PPC_BIT(59)
#define SPR_TFMR_TFMR_CORRUPT		PPC_BIT(60)
#define SPR_TFMR_PURR_OVERFLOW		PPC_BIT(61)
#define SPR_TFMR_SPURR_OVERFLOW		PPC_BIT(62)

/* Bits in HMER/HMEER */
#define SPR_HMER_MALFUNCTION_ALERT	PPC_BIT(0)
#define SPR_HMER_PROC_RECV_DONE		PPC_BIT(2)
#define SPR_HMER_PROC_RECV_ERROR_MASKED	PPC_BIT(3)
#define SPR_HMER_TFAC_ERROR		PPC_BIT(4)
#define SPR_HMER_TFMR_PARITY_ERROR	PPC_BIT(5)
#define SPR_HMER_XSCOM_FAIL		PPC_BIT(8)
#define SPR_HMER_XSCOM_DONE		PPC_BIT(9)
#define SPR_HMER_PROC_RECV_AGAIN	PPC_BIT(11)
#define SPR_HMER_WARN_RISE		PPC_BIT(14)
#define SPR_HMER_WARN_FALL		PPC_BIT(15)
#define SPR_HMER_SCOM_FIR_HMI		PPC_BIT(16)
#define SPR_HMER_TRIG_FIR_HMI		PPC_BIT(17)
#define SPR_HMER_HYP_RESOURCE_ERR	PPC_BIT(20)
#define SPR_HMER_XSCOM_STATUS_MASK	PPC_BITMASK(21,23)
#define SPR_HMER_XSCOM_STATUS_LSH	PPC_BITLSHIFT(23)

#ifdef __ASSEMBLY__

/* Thread priority control opcodes */
#define smt_low		or 1,1,1
#define smt_medium	or 2,2,2
#define smt_high	or 3,3,3
#define smt_medium_high	or 5,5,5
#define smt_medium_low	or 6,6,6
#define smt_extra_high	or 7,7,7
#define smt_very_low	or 31,31,31

#else /* __ASSEMBLY__ */

#include <compiler.h>
#include <stdint.h>

/*
 * PPC bitmask field manipulation
 */

/* Extract field fname from val */
#define PPC_GETFIELD(fname, val)			\
	(((val) & fname##_MASK) >> fname##_LSH)

/* Set field fname of oval to fval
 * NOTE: oval isn't modified, the combined result is returned
 */
#define PPC_SETFIELD(fname, oval, fval)			\
	(((oval) & ~fname##_MASK) | \
	 ((((typeof(oval))(fval)) << fname##_LSH) & fname##_MASK))


/*
 * SMT priority
 */

static inline void smt_low(void)	{ asm volatile("or 1,1,1");	}
static inline void smt_medium(void) 	{ asm volatile("or 2,2,2");	}
static inline void smt_high(void)	{ asm volatile("or 3,3,3");	}
static inline void smt_medium_high(void){ asm volatile("or 5,5,5");	}
static inline void smt_medium_low(void)	{ asm volatile("or 6,6,6");	}
static inline void smt_extra_high(void)	{ asm volatile("or 7,7,7");	}
static inline void smt_very_low(void)	{ asm volatile("or 31,31,31");	}

/*
 * SPR access functions
 */

static inline unsigned long mfmsr(void)
{
	unsigned long val;
	
	asm volatile("mfmsr %0" : "=r"(val) : : "memory");
	return val;
}

static inline void mtmsr(unsigned long val)
{
	asm volatile("mtmsr %0" : : "r"(val) : "memory");
}

static inline void mtmsrd(unsigned long val, int l)
{
	asm volatile("mtmsrd %0,%1" : : "r"(val), "i"(l) : "memory");
}

static inline unsigned long mfspr(unsigned int spr)
{
	unsigned long val;

	asm volatile("mfspr %0,%1" : "=r"(val) : "i"(spr) : "memory");
	return val;
}

static inline void mtspr(unsigned int spr, unsigned long val)
{
	asm volatile("mtspr %0,%1" : : "i"(spr), "r"(val) : "memory");
}

/*
 * Barriers
 */

static inline void eieio(void)
{
	asm volatile("eieio" : : : "memory");
}

static inline void sync(void)
{
	asm volatile("sync" : : : "memory");
}

static inline void lwsync(void)
{
	asm volatile("lwsync" : : : "memory");
}

/*
 * Byteswap load/stores
 */

static inline uint16_t ld_le16(const uint16_t *addr)
{
	uint16_t val;
	asm volatile("lhbrx %0,0,%1" : "=r"(val) : "r"(addr), "m"(*addr));
	return val;
}

static inline uint32_t ld_le32(const uint32_t *addr)
{
	uint32_t val;
	asm volatile("lwbrx %0,0,%1" : "=r"(val) : "r"(addr), "m"(*addr));
	return val;
}

static inline void st_le16(uint16_t *addr, uint16_t val)
{
	asm volatile("sthbrx %0,0,%1" : : "r"(val), "r"(addr), "m"(*addr));
}

static inline void st_le32(uint32_t *addr, uint32_t val)
{
	asm volatile("stwbrx %0,0,%1" : : "r"(val), "r"(addr), "m"(*addr));
}

#endif /* __ASSEMBLY__ */

#endif /* __PROCESSOR_H */