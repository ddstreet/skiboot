/* (C) Copyright IBM Corp., 2013 and provided pursuant to the Technology
 * Licensing Agreement between Google Inc. and International Business
 * Machines Corporation, IBM License Reference Number AA130103030256 and
 * confidentiality governed by the Parties’ Mutual Nondisclosure Agreement
 * number V032404DR, executed by the parties on November 6, 2007, and
 * Supplement V032404DR-3 dated August 16, 2012 (the “NDA”). */
/*
 * Handle ChipTOD chip & configure core timebases
 */
#include <skiboot.h>
#include <chiptod.h>
#include <xscom.h>
#include <io.h>
#include <cpu.h>
#include <time.h>

//#define DBG(fmt...)	printf("CHIPTOD: " fmt)
#define DBG(fmt...)	do { } while(0)

/* TOD chip XSCOM addresses */
#define TOD_TTYPE_0			0x00040011
#define TOD_TTYPE_1			0x00040012 /* PSS switch */
#define TOD_TTYPE_2			0x00040013 /* Enable step checkers */
#define TOD_TTYPE_3			0x00040014 /* Request TOD */
#define TOD_TTYPE_4			0x00040015 /* Send TOD */
#define TOD_TTYPE_5			0x00040016 /* Invalidate TOD */
#define TOD_CHIPTOD_TO_TB		0x00040017
#define TOD_LOAD_TOD_MOD		0x00040018
#define TOD_CHIPTOD_VALUE		0x00040020
#define TOD_CHIPTOD_LOAD_TB		0x00040021
#define TOD_CHIPTOD_FSM			0x00040024

/* -- TOD PIB Master reg -- */
#define TOD_PIB_MASTER			0x00040027
#define   TOD_PIBM_ADDR_CFG_MCAST	PPC_BIT(25)
#define   TOD_PIBM_ADDR_CFG_SLADDR_MASK	PPC_BITMASK(26,31)
#define   TOD_PIBM_ADDR_CFG_SLADDR_LSH	PPC_BITLSHIFT(31)

/* -- TOD Error interrupt register -- */
#define TOD_ERROR			0x00040030
/* SYNC errors */
#define   TOD_ERR_CRMO_PARITY		PPC_BIT(0)
#define   TOD_ERR_OSC0_PARITY		PPC_BIT(1)
#define   TOD_ERR_OSC1_PARITY		PPC_BIT(2)
#define   TOD_ERR_CRITC_PARITY		PPC_BIT(13)
#define   TOD_ERR_PSS_HAMMING_DISTANCE	PPC_BIT(18)
#define	  TOD_ERR_DELAY_COMPL_PARITY	PPC_BIT(22)
/* CNTR errors */
#define   TOD_ERR_CTCR_PARITY		PPC_BIT(32)
#define   TOD_ERR_TOD_SYNC_CHECK	PPC_BIT(33)
#define   TOD_ERR_TOD_FSM_PARITY	PPC_BIT(34)
#define   TOD_ERR_TOD_REGISTER_PARITY	PPC_BIT(35)
#define   TOD_ERR_OVERFLOW_YR2042	PPC_BIT(36)
#define   TOD_ERR_TOD_WOF_LSTEP_PARITY	PPC_BIT(37)
#define   TOD_ERR_TTYPE0_RECVD		PPC_BIT(38)
#define   TOD_ERR_TTYPE1_RECVD		PPC_BIT(39)
#define   TOD_ERR_TTYPE2_RECVD		PPC_BIT(40)
#define   TOD_ERR_TTYPE3_RECVD		PPC_BIT(41)
#define   TOD_ERR_TTYPE4_RECVD		PPC_BIT(42)
#define   TOD_ERR_TTYPE5_RECVD		PPC_BIT(43)

/* Magic TB value. One step cycle ahead of sync */
#define INIT_TB	0x000000000001ff0

/* Number of iterations for the various timeouts */
#define TIMEOUT_LOOPS		10000000

static enum chiptod_type {
	chiptod_unknown,
	chiptod_p7,
	chiptod_p8
} chiptod_type;

static int32_t chiptod_primary = -1;
static int32_t chiptod_secondary = -1;

/* The base TFMR value is the same for the whole machine
 * for now as far as I can tell
 */
static uint64_t base_tfmr;

static bool __chiptod_init(u32 master_cpu)
{
	struct dt_node *np;

	dt_for_each_compatible(dt_root, np, "ibm,power-chiptod") {
		uint32_t chip;

		/* Old DT has chip-id in chiptod node, newer only in the
		 * parent xscom bridge
		 */
		chip = dt_get_chip_id(np);

		if (dt_has_node_property(np, "primary", NULL)) {
		    chiptod_primary = chip;
		    if (dt_node_is_compatible(np,"ibm,power7-chiptod"))
			    chiptod_type = chiptod_p7;
		    if (dt_node_is_compatible(np,"ibm,power8-chiptod"))
			    chiptod_type = chiptod_p8;
		}

		if (dt_has_node_property(np, "secondary", NULL))
		    chiptod_secondary = chip;

	}

	/*
	 * If ChipTOD isn't found in the device-tree, we fallback
	 * based on the master CPU passed by OPAL boot since the
	 * FSP strips off the ChipTOD info from the HDAT when booting
	 * in OPAL mode :-(
	 */
	if (chiptod_primary < 0) {
		struct cpu_thread *t = find_cpu_by_pir(master_cpu);
		printf("CHIPTOD: Cannot find a primary TOD in device-tree\n");
		printf("CHIPTOD: Falling back to Master CPU: %d\n", master_cpu);
		if (!t) {
			prerror("CHIPTOD: NOT FOUND !\n");
			return false;
		}
		chiptod_primary = t->chip_id;
		switch(proc_gen) {
		case proc_gen_p7:
			chiptod_type = chiptod_p7;
			return true;
		case proc_gen_p8:
			chiptod_type = chiptod_p8;
			return true;
		default:
			break;
		}	
		prerror("CHIPTOD: Unknown fallback CPU type !\n");
		return false;
	}
	if (chiptod_type == chiptod_unknown) {
		prerror("CHIPTOD: Unknown TOD type !\n");
		return false;
	}

	return true;
}

static void chiptod_setup_base_tfmr(void)
{
	base_tfmr = SPR_TFMR_TB_ECLIPZ;

	/* XXX Those values need to be derived from the core freq
	 *     I'm working on getting the right formula -- BenH
	 */
	base_tfmr = SETFIELD(SPR_TFMR_MAX_CYC_BET_STEPS, base_tfmr, 0x4b);
	base_tfmr = SETFIELD(SPR_TFMR_N_CLKS_PER_STEP, base_tfmr, 0);
	base_tfmr = SETFIELD(SPR_TFMR_SYNC_BIT_SEL, base_tfmr, 4);
}

static bool chiptod_mod_tb(void)
{
	uint64_t tfmr = base_tfmr;
	uint64_t timeout = 0;

	/* Switch timebase to "Not Set" state */
	mtspr(SPR_TFMR, tfmr | SPR_TFMR_LOAD_TOD_MOD);
	do {
		if (++timeout >= (TIMEOUT_LOOPS*2)) {
			prerror("CHIPTOD: TB \"Not Set\" timeout\n");
			return false;
		}
		tfmr = mfspr(SPR_TFMR);
		if (tfmr & SPR_TFMR_TFMR_CORRUPT) {
			prerror("CHIPTOD: TB \"Not Set\" TFMR corrupt\n");
			return false;
		}
		if (GETFIELD(SPR_TFMR_TBST_ENCODED, tfmr) == 9) {
			prerror("CHIPTOD: TB \"Not Set\" TOD in error state\n");
			return false;
		}
	} while(tfmr & SPR_TFMR_LOAD_TOD_MOD);

	return true;
}

static bool chiptod_interrupt_check(void)
{
	uint64_t tfmr = mfspr(SPR_TFMR);
	uint64_t timeout = 0;

	do {
		if (++timeout >= TIMEOUT_LOOPS) {
			prerror("CHIPTOD: Interrupt check fail\n");
			return false;
		}
		tfmr = mfspr(SPR_TFMR);
		if (tfmr & SPR_TFMR_TFMR_CORRUPT) {
			prerror("CHIPTOD: Interrupt check TFMR corrupt !\n");
			return false;
		}
	} while(tfmr & SPR_TFMR_CHIP_TOD_INTERRUPT);

	return true;
}

static bool chiptod_poll_running(void)
{
	uint64_t timeout = 0;
	uint64_t tval;

	/* Chip TOD running check */
	do {
		if (++timeout >= TIMEOUT_LOOPS) {
			prerror("CHIPTOD: Running check fail timeout\n");
			return false;
		}
		if (xscom_readme(TOD_CHIPTOD_FSM, &tval) != 0) {
			prerror("CHIPTOD: XSCOM error polling run\n");
			return false;
		}
	} while(!(tval & 0x0800000000000000UL));

	return true;
}

static bool chiptod_to_tb(void)
{
	uint64_t tval, tfmr, tvbits;
	uint64_t timeout = 0;

	/* Tell the ChipTOD about our fabric address
	 *
	 * The pib_master value is calculated from the CPU core ID, given in
	 * the PIR. Because we have different core/thread arrangements in the
	 * PIR between p7 and p8, we need to do the calculation differently.
	 *
	 * p7: 0b00001 || 3-bit core id
	 * p8: 0b0001 || 4-bit core id
	 */

	if (xscom_readme(TOD_PIB_MASTER, &tval) != 0) {
		prerror("CHIPTOD: XSCOM error reading PIB_MASTER\n");
		return false;
	}
	if (chiptod_type == chiptod_p8) {
		tvbits = (this_cpu()->pir >> 3) & 0xf;
		tvbits |= 0x10;
	} else {
		tvbits = (this_cpu()->pir >> 2) & 0x7;
		tvbits |= 0x08;
	}
	tval &= ~TOD_PIBM_ADDR_CFG_MCAST;
	tval = SETFIELD(TOD_PIBM_ADDR_CFG_SLADDR, tval, tvbits);
	if (xscom_writeme(TOD_PIB_MASTER, tval) != 0) {
		prerror("CHIPTOD: XSCOM error writing PIB_MASTER\n");
		return false;
	}

	/* Make us ready to get the TB from the chipTOD */
	mtspr(SPR_TFMR, base_tfmr | SPR_TFMR_MOVE_CHIP_TOD_TO_TB);

	/* Tell the ChipTOD to send it */
	if (xscom_writeme(TOD_CHIPTOD_TO_TB, (1ULL << 63)) != 0) {
		prerror("CHIPTOD: XSCOM error writing CHIPTOD_TO_TB\n");
		return false;
	}

	/* Wait for it to complete */
	timeout = 0;
	do {
		if (++timeout >= TIMEOUT_LOOPS) {
			prerror("CHIPTOD: Chip to TB timeout\n");
			return false;
		}
		tfmr = mfspr(SPR_TFMR);
		if (tfmr & SPR_TFMR_TFMR_CORRUPT) {
			prerror("CHIPTOD: MoveToTB: corrupt TFMR !\n");
			return false;
		}
	} while(tfmr & SPR_TFMR_MOVE_CHIP_TOD_TO_TB);

	return true;
}

static bool chiptod_check_tb_running(void)
{
	/* We used to wait for two SYNC pulses in TFMR but that
	 * doesn't seem to occur in sim, so instead we use a
	 * method similar to what pHyp does which is to check for
	 * TFMR SPR_TFMR_TB_VALID and not SPR_TFMR_TFMR_CORRUPT
	 */
#if 0
	uint64_t tfmr, timeout;
	unsigned int i;

	for (i = 0; i < 2; i++) {
		tfmr = mfspr(SPR_TFMR);
		tfmr &= ~SPR_TFMR_TB_SYNC_OCCURED;
		mtspr(SPR_TFMR, tfmr);
		timeout = 0;
		do {
			if (++timeout >= TIMEOUT_LOOPS) {
				prerror("CHIPTOD: No sync pulses\n");
				return false;
			}
			tfmr = mfspr(SPR_TFMR);
		} while(!(tfmr & SPR_TFMR_TB_SYNC_OCCURED));
	}
#else
	uint64_t tfmr = mfspr(SPR_TFMR);

	return (tfmr & SPR_TFMR_TB_VALID) &&
		!(tfmr & SPR_TFMR_TFMR_CORRUPT);
#endif
	return true;
}

static void chiptod_reset_tb_errors(void)
{
	uint64_t tfmr;
	unsigned long timeout = 0;

	/* Ask for automatic clear of errors */
	tfmr = base_tfmr | SPR_TFMR_CLEAR_TB_ERRORS;

	/* Additionally pHyp sets these (write-1-to-clear ?) */
	tfmr |= SPR_TFMR_TB_MISSING_SYNC;
	tfmr |= SPR_TFMR_TB_MISSING_STEP;
	tfmr |= SPR_TFMR_TB_RESIDUE_ERR;
	mtspr(SPR_TFMR, tfmr);

	/* We have to write "Clear TB Errors" again */
	tfmr = base_tfmr | SPR_TFMR_CLEAR_TB_ERRORS;
	mtspr(SPR_TFMR, tfmr);

	do {
		if (++timeout >= TIMEOUT_LOOPS) {
			/* Don't actually do anything on error for
			 * now ... not much we can do, panic maybe ?
			 */
			prerror("CHIPTOD: TB error reset timeout !\n");
			return;
		}
		tfmr = mfspr(SPR_TFMR);
		if (tfmr & SPR_TFMR_TFMR_CORRUPT) {
			prerror("CHIPTOD: TB error reset: corrupt TFMR !\n");
			return;
		}
	} while(tfmr & SPR_TFMR_CLEAR_TB_ERRORS);
}

static void chiptod_cleanup_thread_tfmr(void)
{
	uint64_t tfmr = base_tfmr;

	tfmr |= SPR_TFMR_PURR_PARITY_ERR;
	tfmr |= SPR_TFMR_SPURR_PARITY_ERR;
	tfmr |= SPR_TFMR_DEC_PARITY_ERR;
	tfmr |= SPR_TFMR_TFMR_CORRUPT;
	tfmr |= SPR_TFMR_PURR_OVERFLOW;
	tfmr |= SPR_TFMR_SPURR_OVERFLOW;
	mtspr(SPR_TFMR, tfmr);
}

static void chiptod_reset_tod_errors(void)
{
	uint64_t terr;

	/*
	 * At boot, we clear the errors that the firmware is
	 * supposed to handle. List provided by the pHyp folks.
	 */
	
	terr = TOD_ERR_CRITC_PARITY;
	terr |= TOD_ERR_PSS_HAMMING_DISTANCE;
	terr |= TOD_ERR_DELAY_COMPL_PARITY;
	terr |= TOD_ERR_CTCR_PARITY;
	terr |= TOD_ERR_TOD_SYNC_CHECK;
	terr |= TOD_ERR_TOD_FSM_PARITY;
	terr |= TOD_ERR_TOD_REGISTER_PARITY;

	if (xscom_writeme(TOD_ERROR, terr) != 0) {
		prerror("CHIPTOD: XSCOM error writing TOD_ERROR !\n");
		/* Not much we can do here ... abort ? */
	}
}

static void chiptod_sync_master(void *data)
{
	bool *result = data;

	printf("CHIPTOD: Master sync on CPU PIR 0x%04x...\n", this_cpu()->pir);

	/* Apply base tfmr */
	mtspr(SPR_TFMR, base_tfmr);

	/* From recipe provided by pHyp folks, reset various errors
	 * before attempting the sync
	 */
	chiptod_reset_tb_errors();

	/* Cleanup thread tfmr bits */
	chiptod_cleanup_thread_tfmr();

	/* Reset errors in the chiptod itself */
	chiptod_reset_tod_errors();

	/* Switch timebase to "Not Set" state */
	if (!chiptod_mod_tb())
		goto error;
	DBG("SYNC MASTER Step 2 TFMR=0x%016lx\n", mfspr(SPR_TFMR));

	/* Chip TOD step checkers enable */
	if (xscom_writeme(TOD_TTYPE_2, (1UL << 63)) != 0) {
		prerror("CHIPTOD: XSCOM error enabling steppers\n");
		goto error;
	}

	DBG("SYNC MASTER Step 3 TFMR=0x%016lx\n", mfspr(SPR_TFMR));

	/* Chip TOD interrupt check */
	if (!chiptod_interrupt_check())
		goto error;	
	DBG("SYNC MASTER Step 4 TFMR=0x%016lx\n", mfspr(SPR_TFMR));

	/* Switch local chiptod to "Not Set" state */
	if (xscom_writeme(TOD_LOAD_TOD_MOD, (1UL << 63)) != 0) {
		prerror("CHIPTOD: XSCOM error sending LOAD_TOD_MOD\n");
		goto error;
	}

	/* Switch all remote chiptod to "Not Set" state */
	if (xscom_writeme(TOD_TTYPE_5, (1UL << 63)) != 0) {
		prerror("CHIPTOD: XSCOM error sending TTYPE_5\n");
		goto error;
	}

	/* Chip TOD load initial value */
	if (xscom_writeme(TOD_CHIPTOD_LOAD_TB, INIT_TB) != 0) {
		prerror("CHIPTOD: XSCOM error setting init TB\n");
		goto error;
	}

	DBG("SYNC MASTER Step 5 TFMR=0x%016lx\n", mfspr(SPR_TFMR));

	if (!chiptod_poll_running())
		goto error;
	DBG("SYNC MASTER Step 6 TFMR=0x%016lx\n", mfspr(SPR_TFMR));

	/* Move chiptod value to core TB */
	if (!chiptod_to_tb())
		goto error;
	DBG("SYNC MASTER Step 7 TFMR=0x%016lx\n", mfspr(SPR_TFMR));

	/* Send local chip TOD to all chips TOD */
	if (xscom_writeme(TOD_TTYPE_4, (1ULL << 63)) != 0) {
		prerror("CHIPTOD: XSCOM error sending TTYPE_4\n");
		goto error;
	}

	/* Check if TB is running */
	if (!chiptod_check_tb_running())
		goto error;

	DBG("Master sync completed, TB=%lx\n", mfspr(SPR_TBRL));

	/*
	 * A little delay to make sure the remote chips get up to
	 * speed before we start syncing them.
	 *
	 * We have to do it here because we know our TB is running
	 * while the boot thread TB might not yet.
	 */
	time_wait_ms(1);

	*result = true;
	return;
 error:
	prerror("CHIPTOD: Master sync failed! TFMR=0x%16lx\n", mfspr(SPR_TFMR));
	*result = false;
}

static void chiptod_sync_slave(void *data)
{
	bool *result = data;

	/* Only get primaries, not threads */
	if (this_cpu()->is_secondary) {
		/* On secondaries we just cleanup the TFMR */
		chiptod_cleanup_thread_tfmr();
		*result = true;
		return;
	}

	printf("CHIPTOD: Slave sync on CPU PIR 0x%04x...\n", this_cpu()->pir);

	/* Apply base tfmr */
	mtspr(SPR_TFMR, base_tfmr);

	/* From recipe provided by pHyp folks, reset various errors
	 * before attempting the sync
	 */
	chiptod_reset_tb_errors();

	/* Cleanup thread tfmr bits */
	chiptod_cleanup_thread_tfmr();

	/* Switch timebase to "Not Set" state */
	if (!chiptod_mod_tb())
		goto error;
	DBG("SYNC SLAVE Step 2 TFMR=0x%016lx\n", mfspr(SPR_TFMR));

	/* Chip TOD running check */
	if (!chiptod_poll_running())
		goto error;
	DBG("SYNC SLAVE Step 3 TFMR=0x%016lx\n", mfspr(SPR_TFMR));

	/* Chip TOD interrupt check */
	if (!chiptod_interrupt_check())
		goto error;
	DBG("SYNC SLAVE Step 4 TFMR=0x%016lx\n", mfspr(SPR_TFMR));

	/* Move chiptod value to core TB */
	if (!chiptod_to_tb())
		goto error;
	DBG("SYNC SLAVE Step 5 TFMR=0x%016lx\n", mfspr(SPR_TFMR));

	/* Check if TB is running */
	if (!chiptod_check_tb_running())
		goto error;

	DBG("Slave sync completed, TB=%lx\n", mfspr(SPR_TBRL));

	*result = true;
	return;
 error:
	prerror("CHIPTOD: Slave sync failed ! TFMR=0x%16lx\n", mfspr(SPR_TFMR));
	*result = false;
}

static void chiptod_print_tb(void *data __unused)
{
	printf("CHIPTOD: PIR 0x%04x TB=%lx\n",
	       this_cpu()->pir, mfspr(SPR_TBRL));
}

void chiptod_init(u32 master_cpu)
{
	struct cpu_thread *cpu0, *cpu;
	bool sres;

	op_display(OP_LOG, OP_MOD_CHIPTOD, 0);

	if (!__chiptod_init(master_cpu)) {
		prerror("CHIPTOD: Failed ChipTOD init !\n");
		op_display(OP_FATAL, OP_MOD_CHIPTOD, 0);
		abort();
	}

	op_display(OP_LOG, OP_MOD_CHIPTOD, 1);

	/* Pick somebody on the primary */
	cpu0 = find_cpu_by_chip_id(chiptod_primary);

	/* Calculate the base TFMR value used for everybody */
	chiptod_setup_base_tfmr();

	printf("CHIPTOD: Base TFMR=0x%016llx\n", base_tfmr);

	/* Schedule master sync */
	sres = false;
	cpu_wait_job(cpu_queue_job(cpu0, chiptod_sync_master, &sres), true);
	if (!sres) {
		op_display(OP_FATAL, OP_MOD_CHIPTOD, 2);
		abort();
	}

	op_display(OP_LOG, OP_MOD_CHIPTOD, 2);

	/* Schedule slave sync */
	for_each_available_cpu(cpu) {
		/* Skip master */
		if (cpu == cpu0)
			continue;

		/* Queue job */
		sres = false;
		cpu_wait_job(cpu_queue_job(cpu, chiptod_sync_slave, &sres),
			     true);
		if (!sres) {
			op_display(OP_WARN, OP_MOD_CHIPTOD, 3|(cpu->pir << 8));

			/* Disable threads */
			cpu_disable_all_threads(cpu);
		}
		op_display(OP_LOG, OP_MOD_CHIPTOD, 3|(cpu->pir << 8));
	}

	/* Display TBs */
	for_each_available_cpu(cpu) {
		/* Only do primaries, not threads */
		if (cpu->is_secondary)
			continue;
		cpu_wait_job(cpu_queue_job(cpu, chiptod_print_tb, NULL), true);
	}

	op_display(OP_LOG, OP_MOD_CHIPTOD, 4);
}
