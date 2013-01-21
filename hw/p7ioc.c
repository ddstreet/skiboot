#include <skiboot.h>
#include <p7ioc.h>
#include <p7ioc-regs.h>
#include <spira.h>
#include <cec.h>
#include <opal.h>
#include <io.h>
#include <interrupts.h>
#include <device_tree.h>
#include <ccan/str/str.h>

/*
 * Determine the base address of LEM registers according to
 * the indicated error source.
 */
static void *p7ioc_LEM_base(struct p7ioc *ioc, uint32_t err_src)
{
	uint32_t index;
	void *base = NULL;

	switch (err_src) {
	case P7IOC_ERR_SRC_RGC:
		base = ioc->regs + P7IOC_RGC_LEM_BASE;
		break;
	case P7IOC_ERR_SRC_BI_UP:
		base = ioc->regs + P7IOC_BI_UP_LEM_BASE;
		break;
	case P7IOC_ERR_SRC_BI_DOWN:
		base = ioc->regs + P7IOC_BI_DOWN_LEM_BASE;
		break;
	case P7IOC_ERR_SRC_CI_P0:
	case P7IOC_ERR_SRC_CI_P1:
	case P7IOC_ERR_SRC_CI_P2:
	case P7IOC_ERR_SRC_CI_P3:
	case P7IOC_ERR_SRC_CI_P4:
	case P7IOC_ERR_SRC_CI_P5:
	case P7IOC_ERR_SRC_CI_P6:
	case P7IOC_ERR_SRC_CI_P7:
		index = err_src - P7IOC_ERR_SRC_CI_P0;
		base = ioc->regs + P7IOC_CI_PORTn_LEM_BASE(index);
		break;
	case P7IOC_ERR_SRC_PHB0:
	case P7IOC_ERR_SRC_PHB1:
	case P7IOC_ERR_SRC_PHB2:
	case P7IOC_ERR_SRC_PHB3:
	case P7IOC_ERR_SRC_PHB4:
	case P7IOC_ERR_SRC_PHB5:
		index = err_src - P7IOC_ERR_SRC_PHB0;
		base = ioc->regs + P7IOC_PHBn_LEM_BASE(index);
		break;
	case P7IOC_ERR_SRC_MISC:
		base = ioc->regs + P7IOC_MISC_LEM_BASE;
		break;
	case P7IOC_ERR_SRC_I2C:
		base = ioc->regs + P7IOC_I2C_LEM_BASE;
		break;
	default:
		prerror("%s: Unknown error source %d\n",
			__func__, err_src);
	}

	return base;
}

static void p7ioc_get_diag_common(struct p7ioc *ioc,
				  void *base,
				  struct OpalIoP7IOCErrorData *data)
{
	/* GEM */
	data->gemXfir    = in_be64(ioc->regs + P7IOC_GEM_XFIR);
	data->gemRfir    = in_be64(ioc->regs + P7IOC_GEM_RFIR);
	data->gemRirqfir = in_be64(ioc->regs + P7IOC_GEM_RIRQFIR);
	data->gemMask    = in_be64(ioc->regs + P7IOC_GEM_MASK);
	data->gemRwof    = in_be64(ioc->regs + P7IOC_GEM_RWOF);

	/* LEM */
	data->lemFir     = in_be64(base + P7IOC_LEM_FIR_OFFSET);
	data->lemErrMask = in_be64(base + P7IOC_LEM_ERR_MASK_OFFSET);
	data->lemAction0 = in_be64(base + P7IOC_LEM_ACTION_0_OFFSET);
	data->lemAction1 = in_be64(base + P7IOC_LEM_ACTION_1_OFFSET);
	data->lemWof     = in_be64(base + P7IOC_LEM_WOF_OFFSET);
}

static int64_t p7ioc_get_diag_data(struct io_hub *hub,
				   void *diag_buffer,
				   uint64_t diag_buffer_len)
{
	struct p7ioc *ioc = iohub_to_p7ioc(hub);
	struct OpalIoP7IOCErrorData *data = diag_buffer;
	void *base;

	/* Make sure we have enough buffer */
	if (diag_buffer_len < sizeof(struct OpalIoP7IOCErrorData))
		return OPAL_PARAMETER;

	/* We need do nothing if there're no pending errors */
	if (!p7ioc_err_pending(ioc))
		return OPAL_CLOSED;

	/*
	 * We needn't collect diag-data for CI Port{2, ..., 7}
	 * and PHB{0, ..., 5} since their errors (except GXE)
	 * have been cached to the specific PHB.
	 */
	base = p7ioc_LEM_base(ioc, ioc->err.err_src);
	if (!base) {
		ioc->err.err_src   = P7IOC_ERR_SRC_NONE;
		ioc->err.err_class = P7IOC_ERR_CLASS_NONE;
		ioc->err.err_bit   = 0;
		p7ioc_set_err_pending(ioc, false);
		return OPAL_INTERNAL_ERROR;
	}

	switch (ioc->err.err_src) {
	case P7IOC_ERR_SRC_RGC:
		data->type = OPAL_P7IOC_DIAG_TYPE_RGC;
		p7ioc_get_diag_common(ioc, base, data);

		data->rgc.rgcStatus	= in_be64(ioc->regs + 0x3E1C10);
		data->rgc.rgcLdcp	= in_be64(ioc->regs + 0x3E1C18);

		break;
	case P7IOC_ERR_SRC_BI_UP:
		data->type = OPAL_P7IOC_DIAG_TYPE_BI;
		data->bi.biDownbound = 0;
		p7ioc_get_diag_common(ioc, base, data);

		data->bi.biLdcp0	= in_be64(ioc->regs + 0x3C0100);
		data->bi.biLdcp1	= in_be64(ioc->regs + 0x3C0108);
		data->bi.biLdcp2	= in_be64(ioc->regs + 0x3C0110);
		data->bi.biFenceStatus	= in_be64(ioc->regs + 0x3C0130);

		break;
	case P7IOC_ERR_SRC_BI_DOWN:
		data->type = OPAL_P7IOC_DIAG_TYPE_BI;
		data->bi.biDownbound = 1;
		p7ioc_get_diag_common(ioc, base, data);

		data->bi.biLdcp0	= in_be64(ioc->regs + 0x3C0118);
		data->bi.biLdcp1	= in_be64(ioc->regs + 0x3C0120);
		data->bi.biLdcp2	= in_be64(ioc->regs + 0x3C0128);
		data->bi.biFenceStatus	= in_be64(ioc->regs + 0x3C0130);

		break;
	case P7IOC_ERR_SRC_CI_P0:
	case P7IOC_ERR_SRC_CI_P1:
		data->type = OPAL_P7IOC_DIAG_TYPE_CI;
		data->ci.ciPort = ioc->err.err_src - P7IOC_ERR_SRC_CI_P0;
		p7ioc_get_diag_common(ioc, base, data);

		data->ci.ciPortStatus	= in_be64(base + 0x008);
		data->ci.ciPortLdcp	= in_be64(base + 0x010);
		break;
	case P7IOC_ERR_SRC_MISC:
		data->type = OPAL_P7IOC_DIAG_TYPE_MISC;
		p7ioc_get_diag_common(ioc, base, data);
		break;
	case P7IOC_ERR_SRC_I2C:
		data->type = OPAL_P7IOC_DIAG_TYPE_I2C;
		p7ioc_get_diag_common(ioc, base, data);
		break;
	default:
		ioc->err.err_src   = P7IOC_ERR_SRC_NONE;
		ioc->err.err_class = P7IOC_ERR_CLASS_NONE;
		ioc->err.err_bit   = 0;
		p7ioc_set_err_pending(ioc, false);
		return OPAL_CLOSED;
	}

	/* For errors of MAL class, we need mask it */
	if (ioc->err.err_class == P7IOC_ERR_CLASS_MAL)
		out_be64(base + P7IOC_LEM_ERR_MASK_OR_OFFSET,
			 PPC_BIT(63 - ioc->err.err_bit));
	ioc->err.err_src   = P7IOC_ERR_SRC_NONE;
	ioc->err.err_class = P7IOC_ERR_CLASS_NONE;
	ioc->err.err_bit   = 0;
	p7ioc_set_err_pending(ioc, false);

	return OPAL_SUCCESS;
}

static void p7ioc_add_nodes(struct io_hub *hub)
{
	struct p7ioc *ioc = iohub_to_p7ioc(hub);
	char name[sizeof("io-hub@") + STR_MAX_CHARS(ioc->regs)];
	static const char p7ioc_compat[] =
		"ibm,p7ioc\0ibm,ioda-hub";
	unsigned int i;
	u64 reg[2];

	reg[0] = cleanup_addr((uint64_t)ioc->regs);
	reg[1] = 0x2000000;

	sprintf(name, "io-hub@%llx", reg[0]);
	dt_begin_node(name);
	dt_property("compatible", p7ioc_compat, sizeof(p7ioc_compat));
	dt_property("reg", reg, sizeof(reg));
	dt_property_cell("#address-cells", 2);
	dt_property_cell("#size-cells", 2);
	dt_property_cells("ibm,opal-hubid", 2, 0, hub->hub_id);
	dt_property_cell("interrupt-parent", get_ics_phandle());
	/* XXX Fixme: how many RGC interrupts ? */
	dt_property_cell("interrupts", ioc->rgc_buid << 4);
	dt_property_cell("interrupt-base", ioc->rgc_buid << 4);
	/* XXX What about ibm,opal-mmio-real ? */
	dt_property("ranges", NULL, 0);
	for (i = 0; i < P7IOC_NUM_PHBS; i++)
		p7ioc_phb_add_nodes(&ioc->phbs[i]);
	dt_end_node();
}

static const struct io_hub_ops p7ioc_hub_ops = {
	.set_tce_mem	= NULL, /* No set_tce_mem for p7ioc, we use FMTC */
	.get_diag_data	= p7ioc_get_diag_data,
	.add_nodes	= p7ioc_add_nodes,
	.reset		= p7ioc_reset,
};

static int64_t p7ioc_rgc_get_xive(void *data, uint32_t isn,
				  uint16_t *server, uint8_t *prio)
{
	struct p7ioc *ioc = data;
	uint32_t irq = (isn & 0xf);
	uint32_t fbuid = IRQ_FBUID(isn);
	uint64_t xive;

	if (fbuid != ioc->rgc_buid)
		return OPAL_PARAMETER;

	xive = ioc->xive_cache[irq];
	*server = GETFIELD(IODA_XIVT_SERVER, xive);
	*prio = GETFIELD(IODA_XIVT_PRIORITY, xive);

	return OPAL_SUCCESS;
 }

static int64_t p7ioc_rgc_set_xive(void *data, uint32_t isn,
				  uint16_t server, uint8_t prio)
{
	struct p7ioc *ioc = data;
	uint32_t irq = (isn & 0xf);
	uint32_t fbuid = IRQ_FBUID(isn);
	uint64_t xive;
	uint64_t m_server, m_prio;

	if (fbuid != ioc->rgc_buid)
		return OPAL_PARAMETER;

	xive = SETFIELD(IODA_XIVT_SERVER, 0ull, server);
	xive = SETFIELD(IODA_XIVT_PRIORITY, xive, prio);
	ioc->xive_cache[irq] = xive;

	/* Now we mangle the server and priority */
	if (prio == 0xff) {
		m_server = 0;
		m_prio = 0xff;
	} else {
		m_server = server >> 3;
		m_prio = (prio >> 3) | ((server & 7) << 5);
	}

	/* Update the XIVE. Don't care HRT entry on P7IOC */
	out_be64(ioc->regs + 0x3e1820, (0x0002000000000000 | irq));
	xive = in_be64(ioc->regs + 0x3e1830);
	xive = SETFIELD(IODA_XIVT_SERVER, xive, m_server);
	xive = SETFIELD(IODA_XIVT_PRIORITY, xive, m_prio);
	out_be64(ioc->regs + 0x3e1830, xive);

	return OPAL_SUCCESS;
}

/*
 * The function is used to figure out the error class and error
 * bit according to LEM WOF.
 *
 * The bits of WOF register have been classified according to
 * the error severity. Of course, we should process those errors
 * with higher priority. For example, there have 2 errors (GXE, INF)
 * pending, we should process GXE, and INF is meaningless in face
 * of GXE.
 */
static bool p7ioc_err_bit(struct p7ioc *ioc, uint64_t wof)
{
	uint64_t val, severity[P7IOC_ERR_CLASS_LAST];
        int32_t class, bit, err_bit = -1;

	/* Clear severity array */
	memset(severity, 0, sizeof(uint64_t) * P7IOC_ERR_CLASS_LAST);

	/*
	 * The severity array has fixed values. However, it depends
	 * on the damage settings for individual components. We're
	 * using fix values based on the assuption that damage settings
	 * are fixed for now. If we change it some day, we also need
	 * change the severity array accordingly. Anyway, it's something
	 * to improve in future so that we can figure out the severity
	 * array from hardware registers.
	 */
	switch (ioc->err.err_src) {
	case P7IOC_ERR_SRC_EI:
		/* EI won't create interrupt yet */
		break;
	case P7IOC_ERR_SRC_RGC:
		severity[P7IOC_ERR_CLASS_GXE] = 0xF00086E0F4FCFFFF;
		severity[P7IOC_ERR_CLASS_RGA] = 0x0000010000000000;
		severity[P7IOC_ERR_CLASS_INF] = 0x0FFF781F0B030000;
		break;
	case P7IOC_ERR_SRC_BI_UP:
		severity[P7IOC_ERR_CLASS_GXE] = 0xF7FFFFFF7FFFFFFF;
		severity[P7IOC_ERR_CLASS_INF] = 0x0800000080000000;
		break;
	case P7IOC_ERR_SRC_BI_DOWN:
		severity[P7IOC_ERR_CLASS_GXE] = 0xDFFFF7F35F8000BF;
		severity[P7IOC_ERR_CLASS_INF] = 0x2000080CA07FFF40;
		break;
	case P7IOC_ERR_SRC_CI_P0:
		severity[P7IOC_ERR_CLASS_GXE] = 0xF5FF000000000000;
		severity[P7IOC_ERR_CLASS_INF] = 0x0200FFFFFFFFFFFF;
		severity[P7IOC_ERR_CLASS_MAL] = 0x0800000000000000;
		break;
	case P7IOC_ERR_SRC_CI_P1:
		severity[P7IOC_ERR_CLASS_GXE] = 0xFFFF000000000000;
		severity[P7IOC_ERR_CLASS_INF] = 0x0000FFFFFFFFFFFF;
		break;
	case P7IOC_ERR_SRC_CI_P2:
	case P7IOC_ERR_SRC_CI_P3:
	case P7IOC_ERR_SRC_CI_P4:
	case P7IOC_ERR_SRC_CI_P5:
	case P7IOC_ERR_SRC_CI_P6:
	case P7IOC_ERR_SRC_CI_P7:
		severity[P7IOC_ERR_CLASS_GXE] = 0x5B0B000000000000;
		severity[P7IOC_ERR_CLASS_PHB] = 0xA4F4000000000000;
		severity[P7IOC_ERR_CLASS_INF] = 0x0000FFFFFFFFFFFF;
		break;
	case P7IOC_ERR_SRC_MISC:
		severity[P7IOC_ERR_CLASS_GXE] = 0x0000000310000000;
		severity[P7IOC_ERR_CLASS_PLL] = 0x0000000001C00000;
		severity[P7IOC_ERR_CLASS_INF] = 0x555FFFF0EE3FFFFF;
		severity[P7IOC_ERR_CLASS_MAL] = 0xAAA0000C00000000;
		break;
	case P7IOC_ERR_SRC_I2C:
		severity[P7IOC_ERR_CLASS_GXE] = 0x1100000000000000;
		severity[P7IOC_ERR_CLASS_INF] = 0xEEFFFFFFFFFFFFFF;
		break;
	case P7IOC_ERR_SRC_PHB0:
	case P7IOC_ERR_SRC_PHB1:
	case P7IOC_ERR_SRC_PHB2:
	case P7IOC_ERR_SRC_PHB3:
	case P7IOC_ERR_SRC_PHB4:
	case P7IOC_ERR_SRC_PHB5:
		severity[P7IOC_ERR_CLASS_PHB] = 0xADB650CB808DD051;
		severity[P7IOC_ERR_CLASS_ER]  = 0x0000A0147F50092C;
		severity[P7IOC_ERR_CLASS_INF] = 0x52490F2000222682;
		break;
	}

        /*
         * The error class (ERR_CLASS) has been defined based on
         * their severity. The priority of those errors out of same
         * class should be defined based on the position of corresponding
         * bit in LEM (Local Error Macro) register.
         */
	for (class = P7IOC_ERR_CLASS_NONE + 1;
	     err_bit < 0 && class < P7IOC_ERR_CLASS_LAST;
	     class++) {
		val = wof & severity[class];
		if (!val) continue;

		for (bit = 0; bit < 64; bit++) {
			if (val & PPC_BIT(bit)) {
				err_bit = 63 - bit;
				break;
			}
		}
	}

	/* If we don't find the error bit, we needn't go on. */
	if (err_bit < 0)
		return false;

	ioc->err.err_class = class - 1;
	ioc->err.err_bit   = err_bit;
	return true;
}

/*
 * Check LEM to determine the detailed error information.
 * The function is expected to be called while OS calls
 * to OPAL API opal_pci_next_error(). Eventually, the errors
 * from CI Port{2, ..., 7} or PHB{0, ..., 5} would be cached
 * to the specific PHB, the left errors would be cached to
 * the IOC.
 */
bool p7ioc_check_LEM(struct p7ioc *ioc,
		     uint16_t *pci_error_type,
		     uint16_t *severity)
{
	void *base;
	uint64_t fir, wof, mask;
	struct p7ioc_phb *p;
	int32_t index;
	bool ret;

	/* Make sure we have error pending on IOC */
	if (!p7ioc_err_pending(ioc))
		return false;

	/*
	 * The IOC probably has been put to fatal error
	 * state (GXE) because of failure on reading on
	 * GEM FIR.
	 */
	if (ioc->err.err_src == P7IOC_ERR_SRC_NONE &&
	    ioc->err.err_class != P7IOC_ERR_CLASS_NONE)
		goto err;

	/*
	 * Get the base address of LEM registers according
	 * to the error source. If we failed to get that,
	 * the error pending flag would be cleared.
	 */
	base = p7ioc_LEM_base(ioc, ioc->err.err_src);
	if (!base) {
		p7ioc_set_err_pending(ioc, false);
		ioc->err.err_src   = P7IOC_ERR_SRC_NONE;
		ioc->err.err_class = P7IOC_ERR_CLASS_NONE;
		return false;
	}

	/* IOC would be broken upon broken FIR */
	fir = in_be64(base + P7IOC_LEM_FIR_OFFSET);
	if (fir == 0xffffffffffffffff) {
		ioc->err.err_src   = P7IOC_ERR_SRC_NONE;
		ioc->err.err_class = P7IOC_ERR_CLASS_GXE;
		goto err;
	}

	/* Read on ERR_MASK and WOF. However, we needn't do for PHBn */
	wof = in_be64(base + P7IOC_LEM_WOF_OFFSET);
	if (ioc->err.err_src >= P7IOC_ERR_SRC_PHB0 &&
	    ioc->err.err_src <= P7IOC_ERR_SRC_PHB5) {
		mask = 0x0ull;
	} else {
		mask = in_be64(base + P7IOC_LEM_ERR_MASK_OFFSET);
		in_be64(base + P7IOC_LEM_ACTION_0_OFFSET);
		in_be64(base + P7IOC_LEM_ACTION_1_OFFSET);
	}

        /*
         * We need process those unmasked error first. If we're
         * failing to get the error bit, we needn't proceed.
         */
	if (wof & ~mask)
		wof &= ~mask;
	if (!wof) {
		ioc->err.err_src   = P7IOC_ERR_SRC_NONE;
		ioc->err.err_class = P7IOC_ERR_CLASS_NONE;
		p7ioc_set_err_pending(ioc, false);
		return false;
        }

	if (!p7ioc_err_bit(ioc, wof)) {
		ioc->err.err_src   = P7IOC_ERR_SRC_NONE;
		ioc->err.err_class = P7IOC_ERR_CLASS_NONE;
		p7ioc_set_err_pending(ioc, false);
		return false;
	}

err:
	/*
	 * We run into here because of valid error. Those errors
	 * from CI Port{2, ..., 7} and PHB{0, ..., 5} will be cached
	 * to the specific PHB. However, we will cache the global
	 * errors (e.g. GXE) to IOC directly. For the left errors,
	 * they will be cached to IOC.
	 */
	if (((ioc->err.err_src >= P7IOC_ERR_SRC_CI_P2  &&
	      ioc->err.err_src <= P7IOC_ERR_SRC_CI_P7) ||
	     (ioc->err.err_src >= P7IOC_ERR_SRC_PHB0   &&
	      ioc->err.err_src <= P7IOC_ERR_SRC_PHB5)) &&
	     ioc->err.err_class != P7IOC_ERR_CLASS_GXE) {
		index = (ioc->err.err_src >= P7IOC_ERR_SRC_PHB0 &&
			 ioc->err.err_src <= P7IOC_ERR_SRC_PHB5) ?
			(ioc->err.err_src - P7IOC_ERR_SRC_PHB0) :
			(ioc->err.err_src - P7IOC_ERR_SRC_CI_P2);
		p = &ioc->phbs[index];

		p->err.err_src   = ioc->err.err_src;
		p->err.err_class = ioc->err.err_class;
		p->err.err_bit   = ioc->err.err_bit;
		p7ioc_phb_set_err_pending(p, true);

		ioc->err.err_src   = P7IOC_ERR_SRC_NONE;
		ioc->err.err_class = P7IOC_ERR_CLASS_NONE;
		ioc->err.err_bit   = 0;
		p7ioc_set_err_pending(ioc, false);

		return false;
	}

	/*
	 * Map the internal error class to that OS can recognize.
	 * Errors from PHB or the associated CI port would be
	 * GXE, PHB-fatal, ER, or INF. For the case, GXE will be
	 * cached to IOC and the left classes will be cached to
	 * the specific PHB.
	 */
	switch (ioc->err.err_class) {
	case P7IOC_ERR_CLASS_GXE:
	case P7IOC_ERR_CLASS_PLL:
	case P7IOC_ERR_CLASS_RGA:
		*pci_error_type = OPAL_EEH_IOC_ERROR;
		*severity = OPAL_EEH_SEV_IOC_DEAD;
		ret = true;
		break;
	case P7IOC_ERR_CLASS_INF:
	case P7IOC_ERR_CLASS_MAL:
		*pci_error_type = OPAL_EEH_IOC_ERROR;
		*severity = OPAL_EEH_SEV_INF;
		ret = false;
		break;
	default:
		ioc->err.err_src   = P7IOC_ERR_SRC_NONE;
		ioc->err.err_class = P7IOC_ERR_CLASS_NONE;
		ioc->err.err_bit   = 0;
		p7ioc_set_err_pending(ioc, false);
		ret = false;
	}

	return ret;
}

/*
 * Check GEM to see if there has any problematic components.
 * The function is expected to be called in RGC interrupt
 * handler. Also, it's notable that failure on reading on
 * XFIR will cause GXE directly.
 */
static bool p7ioc_check_GEM(struct p7ioc *ioc)
{
	uint64_t xfir, rwof;

	/*
	 * Recov_5: Read GEM Xfir
	 * Recov_6: go to GXE recovery?
	 */
	xfir = in_be64(ioc->regs + P7IOC_GEM_XFIR);
	if (xfir == 0xffffffffffffffff) {
		ioc->err.err_src   = P7IOC_ERR_SRC_NONE;
		ioc->err.err_class = P7IOC_ERR_CLASS_GXE;
		p7ioc_set_err_pending(ioc, true);
		return true;
	}

	/*
	 * Recov_7: Read GEM Rfir
	 * Recov_8: Read GEM RIRQfir
	 * Recov_9: Read GEM RWOF
	 * Recov_10: Read Fence Shadow
	 * Recov_11: Read Fence Shadow WOF
	 */
        in_be64(ioc->regs + P7IOC_GEM_RFIR);
        in_be64(ioc->regs + P7IOC_GEM_RIRQFIR);
	rwof = in_be64(ioc->regs + P7IOC_GEM_RWOF);
	in_be64(ioc->regs + P7IOC_CHIP_FENCE_SHADOW);
	in_be64(ioc->regs + P7IOC_CHIP_FENCE_WOF);

	/*
	 * Check GEM RWOF to see which component has been
	 * put into problematic state.
	 */
	ioc->err.err_src = P7IOC_ERR_SRC_NONE;
	if	(rwof & PPC_BIT(1))  ioc->err.err_src = P7IOC_ERR_SRC_RGC;
	else if (rwof & PPC_BIT(2))  ioc->err.err_src = P7IOC_ERR_SRC_BI_UP;
	else if (rwof & PPC_BIT(3))  ioc->err.err_src = P7IOC_ERR_SRC_BI_DOWN;
	else if (rwof & PPC_BIT(4))  ioc->err.err_src = P7IOC_ERR_SRC_CI_P0;
	else if (rwof & PPC_BIT(5))  ioc->err.err_src = P7IOC_ERR_SRC_CI_P1;
	else if (rwof & PPC_BIT(6))  ioc->err.err_src = P7IOC_ERR_SRC_CI_P2;
	else if (rwof & PPC_BIT(7))  ioc->err.err_src = P7IOC_ERR_SRC_CI_P3;
	else if (rwof & PPC_BIT(8))  ioc->err.err_src = P7IOC_ERR_SRC_CI_P4;
	else if (rwof & PPC_BIT(9))  ioc->err.err_src = P7IOC_ERR_SRC_CI_P5;
	else if (rwof & PPC_BIT(10)) ioc->err.err_src = P7IOC_ERR_SRC_CI_P6;
	else if (rwof & PPC_BIT(11)) ioc->err.err_src = P7IOC_ERR_SRC_CI_P7;
	else if (rwof & PPC_BIT(16)) ioc->err.err_src = P7IOC_ERR_SRC_PHB0;
	else if (rwof & PPC_BIT(17)) ioc->err.err_src = P7IOC_ERR_SRC_PHB1;
	else if (rwof & PPC_BIT(18)) ioc->err.err_src = P7IOC_ERR_SRC_PHB2;
	else if (rwof & PPC_BIT(19)) ioc->err.err_src = P7IOC_ERR_SRC_PHB3;
	else if (rwof & PPC_BIT(20)) ioc->err.err_src = P7IOC_ERR_SRC_PHB4;
	else if (rwof & PPC_BIT(21)) ioc->err.err_src = P7IOC_ERR_SRC_PHB5;
	else if (rwof & PPC_BIT(24)) ioc->err.err_src = P7IOC_ERR_SRC_MISC;
	else if (rwof & PPC_BIT(25)) ioc->err.err_src = P7IOC_ERR_SRC_I2C;

	/*
	 * If we detect any problematic components, the OS is
	 * expected to poll that for more details through OPAL
	 * interface.
	 */
        if (ioc->err.err_src != P7IOC_ERR_SRC_NONE) {
		p7ioc_set_err_pending(ioc, true);
		return true;
	}

	return false;
}

static void p7ioc_rgc_interrupt(void *data, uint32_t isn __unused)
{
	struct p7ioc *ioc = data;

	/* We will notify OS while getting error from GEM */
	if (p7ioc_check_GEM(ioc))
		opal_update_pending_evt(OPAL_EVENT_PCI_ERROR,
					OPAL_EVENT_PCI_ERROR);
}

static const struct irq_source_ops p7ioc_rgc_irq_ops = {
	.get_xive = p7ioc_rgc_get_xive,
	.set_xive = p7ioc_rgc_set_xive,
	.interrupt = p7ioc_rgc_interrupt,
};

struct io_hub *p7ioc_create_hub(const struct cechub_io_hub *hub, uint32_t id)
{
	struct p7ioc *ioc;
	unsigned int i;

	ioc = zalloc(sizeof(struct p7ioc));
	if (!ioc)
		return NULL;
	ioc->hub.hub_id = id;
	ioc->hub.ops = &p7ioc_hub_ops;
	
	printf("P7IOC: Assigned OPAL Hub ID %d\n", ioc->hub.hub_id);
	printf("P7IOC: Chip: %d GX bus: %d Base BUID: 0x%x EC Level: 0x%x\n",
	       hub->proc_chip_id, hub->gx_index, hub->buid_ext, hub->ec_level);

	/* GX BAR assignment: see p7ioc.h */
	printf("P7IOC: GX BAR 0 = 0x%016llx\n", hub->gx_ctrl_bar0);
	printf("P7IOC: GX BAR 1 = 0x%016llx\n", hub->gx_ctrl_bar1);
	printf("P7IOC: GX BAR 2 = 0x%016llx\n", hub->gx_ctrl_bar2);
	printf("P7IOC: GX BAR 3 = 0x%016llx\n", hub->gx_ctrl_bar3);
	printf("P7IOC: GX BAR 4 = 0x%016llx\n", hub->gx_ctrl_bar4);

	/* We only know about memory map 1 */
	if (hub->mem_map_vers != 1) {
		prerror("P7IOC: Unknown memory map %d\n", hub->mem_map_vers);
		/* We try to continue anyway ... */
	}

	ioc->regs = (void *)hub->gx_ctrl_bar1;

	/* Should we read the GX BAR sizes via SCOM instead ? */
	ioc->mmio1_win_start = hub->gx_ctrl_bar1;
	ioc->mmio1_win_size = MWIN1_SIZE;
	ioc->mmio2_win_start = hub->gx_ctrl_bar2;
	ioc->mmio2_win_size = MWIN2_SIZE;

	ioc->buid_base = hub->buid_ext << 9;
	ioc->rgc_buid = ioc->buid_base + RGC_BUID_OFFSET;

	/* Clear the RGC XIVE cache */
	for (i = 0; i < 16; i++)
		ioc->xive_cache[i] = SETFIELD(IODA_XIVT_PRIORITY, 0ull, 0xff);

	/*
	 * Register RGC interrupts
	 *
	 * For now I assume only 0 is... to verify with Greg or HW guys,
	 * we support all 16
	 */
	register_irq_source(&p7ioc_rgc_irq_ops, ioc, ioc->rgc_buid << 4, 1);

	/* Setup PHB structures (no HW access yet).
	 *
	 * XXX FIXME: We assume all PHBs are active.
	 */
	for (i = 0; i < P7IOC_NUM_PHBS; i++)
		p7ioc_phb_setup(ioc, i, true);
	
	/* Now, we do the bulk of the inits */
	p7ioc_inits(ioc);

	printf("P7IOC: Initialization complete\n");

	return &ioc->hub;
}
