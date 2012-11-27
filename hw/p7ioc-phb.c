#include <skiboot.h>
#include <p7ioc.h>
#include <p7ioc-regs.h>
#include <io.h>
#include <time.h>
#include <pci.h>
#include <pci-cfg.h>
#include <interrupts.h>
#include <device_tree.h>
#include <opal.h>
#include <ccan/str/str.h>

static void p7ioc_phb_trace(struct p7ioc_phb *p, FILE *s, const char *fmt, ...)
{
	/* Use a temp stack buffer to print all at once to avoid
	 * mixups of a trace entry on SMP
	 */
	char tbuf[128 + 10];
	va_list args;
	char *b = tbuf;

	b += sprintf(b, "PHB%d: ", p->phb.opal_id);
	va_start(args, fmt);
	vsnprintf(b, 128, fmt, args);
	va_end(args);
	fputs(tbuf, s);
}
#define PHBDBG(p, fmt...)	p7ioc_phb_trace(p, stdout, fmt)
#define PHBERR(p, fmt...)	p7ioc_phb_trace(p, stderr, fmt)

/* Helper to select an IODA table entry */
static inline void p7ioc_phb_ioda_sel(struct p7ioc_phb *p, uint32_t table,
				      uint32_t addr, bool autoinc)
{
	out_be64(p->regs + PHB_IODA_ADDR,
		 (autoinc ? PHB_IODA_AD_AUTOINC : 0)	|
		 SETFIELD(PHB_IODA_AD_TSEL, 0ul, table)	|
		 SETFIELD(PHB_IODA_AD_TADR, 0ul, addr));
}

/* Helper to set the state machine timeout */
static inline uint64_t p7ioc_set_sm_timeout(struct p7ioc_phb *p, uint64_t dur)
{
	uint64_t target, now = mftb();

	target = now + dur;
	if (target == 0)
		target++;
	p->delay_tgt_tb = target;

	return dur;
}

/*
 * Lock callbacks. Allows the OPAL API handlers to lock the
 * PHB around calls such as config space, EEH, etc...
 */
static void p7ioc_phb_lock(struct phb *phb)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);

	lock(&p->lock);
}

static  void p7ioc_phb_unlock(struct phb *phb)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);

	unlock(&p->lock);
}

/*
 * Configuration space access
 *
 * The PHB lock is assumed to be already held
 */
static int64_t p7ioc_pcicfg_check(struct p7ioc_phb *p, uint32_t bdfn,
				  uint32_t offset, uint32_t size)
{
	uint32_t sm = size - 1;

	if (offset > 0xfff || bdfn > 0xffff)
		return OPAL_PARAMETER;
	if (offset & sm)
		return OPAL_PARAMETER;

	/* The root bus only has a device at 0 and we get into an
	 * error state if we try to probe beyond that, so let's
	 * avoid that and just return an error to Linux
	 */
	if ((bdfn >> 8) == 0 && (bdfn & 0xff))
		return OPAL_HARDWARE;

	/* Check PHB state */
	if (p->state == P7IOC_PHB_STATE_BROKEN)
		return OPAL_HARDWARE;

	return OPAL_SUCCESS;
}

static int64_t p7ioc_pcicfg_read8(struct phb *phb, uint32_t bdfn,
				  uint32_t offset, uint8_t *data)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t addr;
	int64_t rc;

	/* Initialize data in case of error */
	*data = 0xff;

	rc = p7ioc_pcicfg_check(p, bdfn, offset, 1);
	if (rc)
		return rc;

	addr = PHB_CA_ENABLE | ((uint64_t)bdfn << PHB_CA_FUNC_LSH);
	addr = SETFIELD(PHB_CA_REG, addr, offset);
	out_be64(p->regs + PHB_CONFIG_ADDRESS, addr);
	*data = in_8(p->regs + PHB_CONFIG_DATA + (offset & 3));

	return OPAL_SUCCESS;
}

static int64_t p7ioc_pcicfg_read16(struct phb *phb, uint32_t bdfn,
				   uint32_t offset, uint16_t *data)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t addr;
	int64_t rc;

	/* Initialize data in case of error */
	*data = 0xffff;

	rc = p7ioc_pcicfg_check(p, bdfn, offset, 2);
	if (rc)
		return rc;

	addr = PHB_CA_ENABLE | ((uint64_t)bdfn << PHB_CA_FUNC_LSH);
	addr = SETFIELD(PHB_CA_REG, addr, offset);
	out_be64(p->regs + PHB_CONFIG_ADDRESS, addr);
	*data = in_le16(p->regs + PHB_CONFIG_DATA + (offset & 3));

	return OPAL_SUCCESS;
}

static int64_t p7ioc_pcicfg_read32(struct phb *phb, uint32_t bdfn,
				   uint32_t offset, uint32_t *data)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t addr;
	int64_t rc;

	/* Initialize data in case of error */
	*data = 0xffffffff;

	rc = p7ioc_pcicfg_check(p, bdfn, offset, 4);
	if (rc)
		return rc;

	addr = PHB_CA_ENABLE | ((uint64_t)bdfn << PHB_CA_FUNC_LSH);
	addr = SETFIELD(PHB_CA_REG, addr, offset);
	out_be64(p->regs + PHB_CONFIG_ADDRESS, addr);
	*data = in_le32(p->regs + PHB_CONFIG_DATA);

	return OPAL_SUCCESS;
}

static int64_t p7ioc_pcicfg_write8(struct phb *phb, uint32_t bdfn,
				   uint32_t offset, uint8_t data)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t addr;
	int64_t rc;

	rc = p7ioc_pcicfg_check(p, bdfn, offset, 1);
	if (rc)
		return rc;

	addr = PHB_CA_ENABLE | ((uint64_t)bdfn << PHB_CA_FUNC_LSH);
	addr = SETFIELD(PHB_CA_REG, addr, offset);
	out_be64(p->regs + PHB_CONFIG_ADDRESS, addr);
	out_8(p->regs + PHB_CONFIG_DATA + (offset & 3), data);

	return OPAL_SUCCESS;
}

static int64_t p7ioc_pcicfg_write16(struct phb *phb, uint32_t bdfn,
				    uint32_t offset, uint16_t data)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t addr;
	int64_t rc;

	rc = p7ioc_pcicfg_check(p, bdfn, offset, 2);
	if (rc)
		return rc;

	addr = PHB_CA_ENABLE | ((uint64_t)bdfn << PHB_CA_FUNC_LSH);
	addr = SETFIELD(PHB_CA_REG, addr, offset);
	out_be64(p->regs + PHB_CONFIG_ADDRESS, addr);
	out_le16(p->regs + PHB_CONFIG_DATA + (offset & 3), data);

	return OPAL_SUCCESS;
}

static int64_t p7ioc_pcicfg_write32(struct phb *phb, uint32_t bdfn,
				    uint32_t offset, uint32_t data)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t addr;
	int64_t rc;

	rc = p7ioc_pcicfg_check(p, bdfn, offset, 1);
	if (rc)
		return rc;

	addr = PHB_CA_ENABLE | ((uint64_t)bdfn << PHB_CA_FUNC_LSH);
	addr = SETFIELD(PHB_CA_REG, addr, offset);
	out_be64(p->regs + PHB_CONFIG_ADDRESS, addr);
	out_le32(p->regs + PHB_CONFIG_DATA, data);

	return OPAL_SUCCESS;
}

static int64_t p7ioc_presence_detect(struct phb *phb)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t reg = in_be64(p->regs + PHB_PCIE_SLOTCTL2);

	/* XXX Test for PHB in error state ? */

	if (reg & PHB_PCIE_SLOTCTL2_PRSTN_STAT)
		return OPAL_SHPC_DEV_PRESENT;

	return OPAL_SHPC_DEV_NOT_PRESENT;
}

static int64_t p7ioc_link_state(struct phb *phb)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t reg = in_be64(p->regs + PHB_PCIE_DLP_TRAIN_CTL);
	uint16_t lstat;
	int64_t rc;

	/* XXX Test for PHB in error state ? */

	/* Link is up, let's find the actual speed */
	if (!(reg & PHB_PCIE_DLP_TC_DL_LINKACT))
		return OPAL_SHPC_LINK_DOWN;

	rc = p7ioc_pcicfg_read16(&p->phb, 0, p->ecap + PCICAP_EXP_LSTAT,
				 &lstat);
	if (rc < 0) {
		/* Shouldn't happen */
		PHBERR(p, "Failed to read link status\n");
		return OPAL_HARDWARE;
	}
	if (!(lstat & PCICAP_EXP_LSTAT_DLLL_ACT))
		return OPAL_SHPC_LINK_DOWN;

	return GETFIELD(PCICAP_EXP_LSTAT_WIDTH, lstat);
}

static int64_t p7ioc_power_state(struct phb *phb)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t reg = in_be64(p->regs + PHB_PCIE_SLOTCTL2);

	/* XXX Test for PHB in error state ? */

	if (reg & PHB_PCIE_SLOTCTL2_PWR_EN_STAT)
		return OPAL_SHPC_POWER_ON;

	return OPAL_SHPC_POWER_OFF;
}

/* p7ioc_sm_slot_power_off - Slot power off state machine
 */
static int64_t p7ioc_sm_slot_power_off(struct p7ioc_phb *p)
{
	switch(p->state) {
	default:
		break;
	}

	/* Unknown state, hardware error ? */
	return OPAL_HARDWARE;
}

static int64_t p7ioc_slot_power_off(struct phb *phb)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);

	if (p->state != P7IOC_PHB_STATE_FUNCTIONAL)
		return OPAL_BUSY;

	/* run state machine */
	return p7ioc_sm_slot_power_off(p);
}

static int64_t p7ioc_sm_slot_power_on(struct p7ioc_phb *p)
{
	uint64_t reg;
	uint32_t reg32;
	uint16_t brctl;

	switch(p->state) {
	case P7IOC_PHB_STATE_FUNCTIONAL:
		/* Check presence */
		reg = in_be64(p->regs + PHB_PCIE_SLOTCTL2);
		if (!(reg & PHB_PCIE_SLOTCTL2_PRSTN_STAT)) {
			PHBDBG(p, "Slot power on: no device\n");
			return OPAL_CLOSED;
		}

		/* Adjust UTL interrupt settings to disable various
		 * errors that would interfere with the process
		 */
		out_be64(p->regs + UTL_PCIE_PORT_IRQ_EN, 0x7e00000000000000);

		/* If the power is not on, turn it on now */
		if (!(reg & PHB_PCIE_SLOTCTL2_PWR_EN_STAT)) {
			/*
			 * The hotplug override register will not properly
			 * initiate the poweron sequence unless bit 0
			 * transitions from 0 to 1. Since it can already be
			 * set to 1 as a result of a previous power-on
			 * operation (even if the slot power is now off)
			 * we need to first clear it, then set it to 1 or
			 * nothing will happen
			 */
			reg = in_be64(p->regs + PHB_HOTPLUG_OVERRIDE);
			reg &= ~(0x8c00000000000000ul);
			out_be64(p->regs + PHB_HOTPLUG_OVERRIDE, reg);
			reg |= 0x8400000000000000ul;
			out_be64(p->regs + PHB_HOTPLUG_OVERRIDE, reg);
			p->state = P7IOC_PHB_STATE_SPUP_STABILIZE_DELAY;
			PHBDBG(p, "Slot power on: powering on...\n");
			return p7ioc_set_sm_timeout(p, secs_to_tb(2));
		}
		/* Power is already on */
	power_ok:
		/* Ensure hot reset is deasserted */
		p7ioc_pcicfg_read16(&p->phb, 0, PCI_CFG_BRCTL, &brctl);
		brctl &= ~PCI_CFG_BRCTL_SECONDARY_RESET;
		p7ioc_pcicfg_write16(&p->phb, 0, PCI_CFG_BRCTL, brctl);
		p->retries = 40;
		p->state = P7IOC_PHB_STATE_SPUP_WAIT_LINK;
		PHBDBG(p, "Slot power on: waiting for link\n");
		/* Fall through */
	case P7IOC_PHB_STATE_SPUP_WAIT_LINK:
		reg = in_be64(p->regs + PHB_PCIE_DLP_TRAIN_CTL);
		/* Link is up ? Complete */

		/* XXX TODO: Check link width problem and if present
		 * go straight to the host reset code path.
		 */
		if (reg & PHB_PCIE_DLP_TC_DL_LINKACT) {
			/* Restore UTL interrupts */
			out_be64(p->regs + UTL_PCIE_PORT_IRQ_EN,
				 0xfe65000000000000);
			p->state = P7IOC_PHB_STATE_FUNCTIONAL;
			PHBDBG(p, "Slot power on: up !\n");
			return OPAL_SUCCESS;
		}
		/* Retries */
		p->retries--;
		if (p->retries == 0) {
			/* XXX Improve logging */
			PHBERR(p,"Slot power on: Timeout waiting for link\n");
			goto error;
		}
		/* Check time elapsed */
		if ((p->retries % 20) != 0)
			return p7ioc_set_sm_timeout(p, msecs_to_tb(10));

		/* >200ms, time to try a hot reset after clearing the
		 * link status bit (doco says to do so)
		 */
		out_be64(p->regs + UTL_PCIE_PORT_STATUS, 0x0080000000000000);

		/* Mask receiver error status in AER */
		p7ioc_pcicfg_read32(&p->phb, 0,
				    p->aercap + PCIECAP_AER_CE_MASK, &reg32);
		reg32 |= PCIECAP_AER_CE_RECVR_ERR;
		p7ioc_pcicfg_write32(&p->phb, 0,
				     p->aercap + PCIECAP_AER_CE_MASK, reg32);

		/* Turn on host reset */
		p7ioc_pcicfg_read16(&p->phb, 0, PCI_CFG_BRCTL, &brctl);
		brctl |= PCI_CFG_BRCTL_SECONDARY_RESET;
		p7ioc_pcicfg_write16(&p->phb, 0, PCI_CFG_BRCTL, brctl);
		p->state = P7IOC_PHB_STATE_SPUP_HOT_RESET_DELAY;
		PHBDBG(p, "Slot power on: soft reset...\n");
		return p7ioc_set_sm_timeout(p, secs_to_tb(1));
	case P7IOC_PHB_STATE_SPUP_HOT_RESET_DELAY:
		/* Turn off host reset */
		p7ioc_pcicfg_read16(&p->phb, 0, PCI_CFG_BRCTL, &brctl);
		brctl &= ~PCI_CFG_BRCTL_SECONDARY_RESET;
		p7ioc_pcicfg_write16(&p->phb, 0, PCI_CFG_BRCTL, brctl);
		/* Clear spurrious errors */
		out_be64(p->regs + UTL_PCIE_PORT_STATUS, 0x00e0000000000000);
		p7ioc_pcicfg_write32(&p->phb, 0,
				     p->aercap + PCIECAP_AER_CE_STATUS,
				     PCIECAP_AER_CE_RECVR_ERR);
		/* Unmask receiver error status in AER */
		p7ioc_pcicfg_read32(&p->phb, 0,
				    p->aercap + PCIECAP_AER_CE_MASK, &reg32);
		reg32 &= ~PCIECAP_AER_CE_RECVR_ERR;
		p7ioc_pcicfg_write32(&p->phb, 0,
				     p->aercap + PCIECAP_AER_CE_MASK, reg32);
		/* Go back to waiting for link */
		p->state = P7IOC_PHB_STATE_SPUP_WAIT_LINK;
		PHBDBG(p, "Slot power on: waiting for link (2)\n");
		return p7ioc_set_sm_timeout(p, msecs_to_tb(10));

	case P7IOC_PHB_STATE_SPUP_STABILIZE_DELAY:
		/* Come here after the 2s delay after power up */
		p->retries = 1000;
		p->state = P7IOC_PHB_STATE_SPUP_SLOT_STATUS;
		PHBDBG(p, "Slot power on: waiting for power\n");
		/* Fall through */
	case P7IOC_PHB_STATE_SPUP_SLOT_STATUS:
		reg = in_be64(p->regs + PHB_PCIE_SLOTCTL2);

		/* Doc says to check LED status, but we ignore that, there
		 * no point really and it's easier that way
		 */
		if (reg & PHB_PCIE_SLOTCTL2_PWR_EN_STAT)
			goto power_ok;
		if (p->retries-- == 0) {
			/* XXX Improve error logging */
			PHBERR(p, "Timeout powering up slot\n");
			goto error;
		}
		return p7ioc_set_sm_timeout(p, msecs_to_tb(10));
	default:
		break;
	}

	/* Unknown state, hardware error ? */
 error:
	p->state = P7IOC_PHB_STATE_FUNCTIONAL;
	return OPAL_HARDWARE;
}

static int64_t p7ioc_slot_power_on(struct phb *phb)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);

	if (p->state != P7IOC_PHB_STATE_FUNCTIONAL)
		return OPAL_BUSY;

	/* run state machine */
	return p7ioc_sm_slot_power_on(p);
}

static int64_t p7ioc_sm_hot_reset(struct p7ioc_phb *p)
{
	switch(p->state) {
	default:
		break;
	}

	/* Unknown state, hardware error ? */
	return OPAL_HARDWARE;
}

static int64_t p7ioc_hot_reset(struct phb *phb)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);

	if (p->state != P7IOC_PHB_STATE_FUNCTIONAL)
		return OPAL_BUSY;

	/* run state machine */
	return p7ioc_sm_hot_reset(p);
}

static int64_t p7ioc_poll(struct phb *phb)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t now = mftb();

	if (p->state == P7IOC_PHB_STATE_FUNCTIONAL)
		return OPAL_SUCCESS;

	/* Check timer */
	if (p->delay_tgt_tb &&
	    tb_compare(now, p->delay_tgt_tb) == TB_ABEFOREB)
		return p->delay_tgt_tb - now;

	/* Expired (or not armed), clear it */
	p->delay_tgt_tb = 0;

	/* Dispatch to the right state machine */
	switch(p->state) {
	case P7IOC_PHB_STATE_SPUP_STABILIZE_DELAY:
	case P7IOC_PHB_STATE_SPUP_SLOT_STATUS:
	case P7IOC_PHB_STATE_SPUP_WAIT_LINK:
	case P7IOC_PHB_STATE_SPUP_HOT_RESET_DELAY:
		return p7ioc_sm_slot_power_on(p);
	case P7IOC_PHB_STATE_SPDOWN_STABILIZE_DELAY:
	case P7IOC_PHB_STATE_SPDOWN_SLOT_STATUS:
		return p7ioc_sm_slot_power_off(p);
	case P7IOC_PHB_STATE_HRESET_DELAY:
		return p7ioc_sm_hot_reset(p);
	default:
		break;
	}
	/* Unknown state, could be a HW error */
	return OPAL_HARDWARE;
}

static void p7ioc_eeh_read_phb_status(struct p7ioc_phb *p,
				      struct OpalIoP7IOCPhbErrorData *stat)
{
	uint16_t tmp16;
	unsigned int i;

	memset(stat, 0, sizeof(struct OpalIoP7IOCPhbErrorData));

	/*
	 * We read some registers using config space through AIB.
	 *
	 * Get to other registers using ASB when possible to get to them
	 * through a fence if one is present.
	 *
	 * Note that the OpalIoP7IOCPhbErrorData has oddities, such as the
	 * bridge control being 32-bit and the UTL registers being 32-bit
	 * (which they really are, but they use the top 32-bit of a 64-bit
	 * register so we need to be a bit careful).
	 */

	/* Grab RC bridge control, make it 32-bit */
	p7ioc_pcicfg_read16(&p->phb, 0, PCI_CFG_BRCTL, &tmp16);
	stat->brdgCtl = tmp16;

	/* Grab UTL status registers */
	stat->portStatusReg = hi32(in_be64(p->regs_asb
					   + UTL_PCIE_PORT_STATUS));
	stat->rootCmplxStatus = hi32(in_be64(p->regs_asb
					   + UTL_RC_STATUS));
	stat->busAgentStatus = hi32(in_be64(p->regs_asb
					   + UTL_SYS_BUS_AGENT_STATUS));

	/*
	 * Grab various RC PCIe capability registers. All device, slot
	 * and link status are 16-bit, so we grab the pair control+status
	 * for each of them
	 */
	p7ioc_pcicfg_read32(&p->phb, 0, p->ecap + PCICAP_EXP_DEVCTL,
			    &stat->deviceStatus);
	p7ioc_pcicfg_read32(&p->phb, 0, p->ecap + PCICAP_EXP_SLOTCTL,
			    &stat->slotStatus);
	p7ioc_pcicfg_read32(&p->phb, 0, p->ecap + PCICAP_EXP_LCTL,
			    &stat->linkStatus);

	/*
	 * I assume those are the standard config space header, cmd & status
	 * together makes 32-bit. Secondary status is 16-bit so I'll clear
	 * the top on that one
	 */
	p7ioc_pcicfg_read32(&p->phb, 0, PCI_CFG_CMD, &stat->devCmdStatus);
	p7ioc_pcicfg_read16(&p->phb, 0, PCI_CFG_SECONDARY_STATUS, &tmp16);
	stat->devSecStatus = tmp16;

	/* Grab a bunch of AER regs */
	p7ioc_pcicfg_read32(&p->phb, 0, p->aercap + PCIECAP_AER_RERR_STA,
			    &stat->rootErrorStatus);
	p7ioc_pcicfg_read32(&p->phb, 0, p->aercap + PCIECAP_AER_UE_STATUS,
			    &stat->uncorrErrorStatus);
	p7ioc_pcicfg_read32(&p->phb, 0, p->aercap + PCIECAP_AER_CE_STATUS,
			    &stat->corrErrorStatus);
	p7ioc_pcicfg_read32(&p->phb, 0, p->aercap + PCIECAP_AER_HDR_LOG0,
			    &stat->tlpHdr1);
	p7ioc_pcicfg_read32(&p->phb, 0, p->aercap + PCIECAP_AER_HDR_LOG1,
			    &stat->tlpHdr2);
	p7ioc_pcicfg_read32(&p->phb, 0, p->aercap + PCIECAP_AER_HDR_LOG2,
			    &stat->tlpHdr3);
	p7ioc_pcicfg_read32(&p->phb, 0, p->aercap + PCIECAP_AER_HDR_LOG3,
			    &stat->tlpHdr4);
	p7ioc_pcicfg_read32(&p->phb, 0, p->aercap + PCIECAP_AER_SRCID,
			    &stat->sourceId);

	/*
	 * No idea what that that is supposed to be, opal.h says
	 * "Record data about the call to allocate a buffer."
	 *
	 * Let's leave them alone for now...
	 *
	 * uint64_t errorClass;
	 * uint64_t correlator;
	*/

	/* P7IOC MMIO Error Regs */
	stat->p7iocPlssr = in_be64(p->regs_asb + PHB_CPU_LOADSTORE_STATUS);
	stat->p7iocPlssr = in_be64(p->regs_asb + PHB_DMA_CHAN_STATUS);
	stat->lemFir = in_be64(p->regs_asb + PHB_LEM_FIR_ACCUM);
	stat->lemErrorMask = in_be64(p->regs_asb + PHB_LEM_ERROR_MASK);
	stat->lemWOF = in_be64(p->regs_asb + PHB_LEM_WOF);
	stat->phbErrorStatus = in_be64(p->regs_asb + PHB_ERR_STATUS);
	stat->phbFirstErrorStatus = in_be64(p->regs_asb + PHB_ERR1_STATUS);
	stat->phbErrorLog0 = in_be64(p->regs_asb + PHB_ERR_LOG_0);
	stat->phbErrorLog1 = in_be64(p->regs_asb + PHB_ERR_LOG_1);
	stat->mmioErrorStatus = in_be64(p->regs_asb + PHB_OUT_ERR_STATUS);
	stat->mmioFirstErrorStatus = in_be64(p->regs_asb + PHB_OUT_ERR1_STATUS);
	stat->mmioErrorLog0 = in_be64(p->regs_asb + PHB_OUT_ERR_LOG_0);
	stat->mmioErrorLog1 = in_be64(p->regs_asb + PHB_OUT_ERR_LOG_1);
	stat->dma0ErrorStatus = in_be64(p->regs_asb + PHB_INA_ERR_STATUS);
	stat->dma0FirstErrorStatus = in_be64(p->regs_asb + PHB_INA_ERR1_STATUS);
	stat->dma0ErrorLog0 = in_be64(p->regs_asb + PHB_INA_ERR_LOG_0);
	stat->dma0ErrorLog1 = in_be64(p->regs_asb + PHB_INA_ERR_LOG_1);
	stat->dma1ErrorStatus = in_be64(p->regs_asb + PHB_INB_ERR_STATUS);
	stat->dma1FirstErrorStatus = in_be64(p->regs_asb + PHB_INB_ERR1_STATUS);
	stat->dma1ErrorLog0 = in_be64(p->regs_asb + PHB_INB_ERR_LOG_0);
	stat->dma1ErrorLog1 = in_be64(p->regs_asb + PHB_INB_ERR_LOG_1);

	/* Grab PESTA & B content */
	p7ioc_phb_ioda_sel(p, IODA_TBL_PESTA, 0, true);
	for (i = 0; i < OPAL_P7IOC_NUM_PEST_REGS; i++)
		stat->pestA[i] = in_be64(p->regs + PHB_IODA_DATA0);
	p7ioc_phb_ioda_sel(p, IODA_TBL_PESTB, 0, true);
	for (i = 0; i < OPAL_P7IOC_NUM_PEST_REGS; i++)
		stat->pestB[i] = in_be64(p->regs + PHB_IODA_DATA0);
}

static bool p7ioc_phb_fenced(struct p7ioc_phb *p)
{
	struct p7ioc *ioc = p->ioc;
	uint64_t fence, fbits;

	fbits = 0x0003000000000000 >> (p->index * 4);
	fence = in_be64(ioc->regs + P7IOC_CHIP_FENCE_SHADOW);

	return (fence & fbits) != 0;
}

static int64_t p7ioc_eeh_freeze_status(struct phb *phb, uint64_t pe_number,
				       uint8_t *freeze_state,
				       uint16_t *pci_error_type,
				       uint16_t *severity,
				       uint64_t *phb_status)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t peev_bit = PPC_BIT(pe_number & 0x3f);
	uint64_t peev, pesta, pestb;

	/* Defaults: not frozen */
	*freeze_state = OPAL_EEH_STOPPED_NOT_FROZEN;
	*pci_error_type = OPAL_EEH_PHB_NO_ERROR;

	/* Check dead */
	if (p->state == P7IOC_PHB_STATE_BROKEN) {
		*freeze_state = OPAL_EEH_STOPPED_MMIO_DMA_FREEZE;
		*pci_error_type = OPAL_EEH_PHB_FATAL;
		if (severity)
			*severity = OPAL_EEH_SEV_PHB_DEAD;
		goto bail;
	}

	/* Check fence */
	if (p7ioc_phb_fenced(p)) {
		/* Should be OPAL_EEH_STOPPED_TEMP_UNAVAIL ? */
		*freeze_state = OPAL_EEH_STOPPED_MMIO_DMA_FREEZE;
		*pci_error_type = OPAL_EEH_PHB_FATAL;
		if (severity)
			*severity = OPAL_EEH_SEV_PHB_FENCED;
		p->state = P7IOC_PHB_STATE_FENCED;
		goto bail;
	}

	/* Check the PEEV */
	p7ioc_phb_ioda_sel(p, IODA_TBL_PEEV, 0, true);
	peev = in_be64(p->regs + PHB_IODA_DATA0);
	if (pe_number > 63)
		peev = in_be64(p->regs + PHB_IODA_DATA0);
	if (!(peev & peev_bit))
		return OPAL_SUCCESS;

	/* Indicate that we have an ER pending */
	p->er_pending = true;
	if (severity)
		*severity = OPAL_EEH_SEV_DEV_ER;

	/* Read the PESTA & PESTB */
	p7ioc_phb_ioda_sel(p, IODA_TBL_PESTA, pe_number, false);
	pesta = in_be64(p->regs + PHB_IODA_DATA0);
	p7ioc_phb_ioda_sel(p, IODA_TBL_PESTB, pe_number, false);
	pestb = in_be64(p->regs + PHB_IODA_DATA0);

	/* Convert them */
	if (pesta & IODA_PESTA_MMIO_FROZEN)
		*freeze_state |= OPAL_EEH_STOPPED_MMIO_FREEZE;
	if (pestb & IODA_PESTB_DMA_STOPPED)
		*freeze_state |= OPAL_EEH_STOPPED_DMA_FREEZE;

	/* XXX Handle more causes */
	if (pesta & IODA_PESTA_MMIO_CAUSE)
		*pci_error_type = OPAL_EEH_PCI_MMIO_ERROR;
	else
		*pci_error_type = OPAL_EEH_PCI_DMA_ERROR;

 bail:
	if (phb_status)
		p7ioc_eeh_read_phb_status(p, (struct OpalIoP7IOCPhbErrorData *)
					  phb_status);
	return OPAL_SUCCESS;
}

static int64_t p7ioc_eeh_next_error(struct phb *phb, uint64_t *first_frozen_pe,
				    uint16_t *pci_error_type, uint16_t *severity)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t peev0, peev1;
	unsigned int i;

	*first_frozen_pe = (uint64_t)-1;

	/* Check dead */
	if (p->state == P7IOC_PHB_STATE_BROKEN) {
		*pci_error_type = OPAL_EEH_PHB_FATAL;
		*severity = OPAL_EEH_SEV_PHB_DEAD;
		return OPAL_SUCCESS;
	}

	/* Check fence */
	if (p7ioc_phb_fenced(p)) {
		/* Should be OPAL_EEH_STOPPED_TEMP_UNAVAIL ? */
		*pci_error_type = OPAL_EEH_PHB_FATAL;
		*severity = OPAL_EEH_SEV_PHB_FENCED;
		p->state = P7IOC_PHB_STATE_FENCED;
		return OPAL_SUCCESS;
	}

	/* Check ERs */
	p7ioc_phb_ioda_sel(p, IODA_TBL_PEEV, 0, true);
	peev0 = in_be64(p->regs + PHB_IODA_DATA0);
	peev1 = in_be64(p->regs + PHB_IODA_DATA0);
	p->er_pending = (peev0 || peev1);
	if (p->er_pending) {
		*pci_error_type = OPAL_EEH_PCI_ANY_ER;
		*severity = OPAL_EEH_SEV_DEV_ER;
		/* XXX use cntlz */
		for (i = 0 ; i < 64; i++) {
			if (PPC_BIT(i) & peev1) {
				*first_frozen_pe = i + 64;
				break;
			}
		}
		for (i = 0 ; i < 64; i++) {
			if (PPC_BIT(i) & peev0) {
				*first_frozen_pe = i;
				break;
			}
		}
		return OPAL_SUCCESS;
	}

	/* XXX Add INF */

	*pci_error_type = OPAL_EEH_PHB_NO_ERROR;
	*severity = OPAL_EEH_SEV_NO_ERROR;
	return OPAL_SUCCESS;
}

static int64_t p7ioc_eeh_freeze_clear(struct phb *phb, uint64_t pe_number,
				      uint64_t eeh_action_token)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t peev0, peev1;

	/* XXX Now this is a heavy hammer, coming roughly from the P7IOC doc
	 * and my old "pseudopal" code. It will need to be refined. In general
	 * error handling will have to be reviewed and probably done properly	
	 * "from scratch" based on the description in the p7IOC spec.
	 *
	 * XXX Additionally, when handling interrupts, we might want to consider
	 * masking while processing and/or ack'ing interrupt bits etc...
	 */
	u64 err, lem;
	u32 val;

	/* Summary. If nothing, move to clearing the PESTs which can
	 * contain a freeze state from a previous error or simply set
	 * explicitely by the user
	 */
	err = in_be64(p->regs + PHB_ETU_ERR_SUMMARY);
	if (err == 0)
		goto clear_pest;

	/* Rec 1,2 */
	lem = in_be64(p->regs + PHB_LEM_FIR_ACCUM);
	/* XXX Check bit 60. If set, check AER 104 (malformed packet)
	 * and if set, go to PHB fatal
	 */

	/* Rec 3,4,5 AER registers (could use cfg space accessors) */
	out_be64(p->regs + PHB_CONFIG_ADDRESS, 0x8000001c00000000ull);
	out_be32(p->regs + PHB_CONFIG_DATA, 0x10000000);

	/* Rec 6,7,8 XXX DOC whacks payload & req size ... we don't */
	out_be64(p->regs + PHB_CONFIG_ADDRESS, 0x8000005000000000ull);
	val = in_be32(p->regs + PHB_CONFIG_DATA);
	out_be32(p->regs + PHB_CONFIG_DATA, (val & 0xe0700000) | 0x0f000f00);

	/* Rec 9,10,11 */
	out_be64(p->regs + PHB_CONFIG_ADDRESS, 0x8000010400000000ull);
	out_be32(p->regs + PHB_CONFIG_DATA, 0xffffffff);

	/* Rec 12,13,14 */
	out_be64(p->regs + PHB_CONFIG_ADDRESS, 0x8000011000000000ull);
	out_be32(p->regs + PHB_CONFIG_DATA, 0xffffffff);

	/* Rec 23,24,25 */
	out_be64(p->regs + PHB_CONFIG_ADDRESS, 0x8000013000000000ull);
	out_be32(p->regs + PHB_CONFIG_DATA, 0xffffffff);

	/* Rec 26,27,28 */
	out_be64(p->regs + PHB_CONFIG_ADDRESS, 0x8000004000000000ull);
	out_be32(p->regs + PHB_CONFIG_DATA, 0x470100f8);

	/* Rec 29..34 UTL registers */
	err = in_be64(p->regs + UTL_SYS_BUS_AGENT_STATUS);
	out_be64(p->regs + UTL_SYS_BUS_AGENT_STATUS, err);
	err = in_be64(p->regs + UTL_PCIE_PORT_STATUS);
	out_be64(p->regs + UTL_PCIE_PORT_STATUS, err);
	err = in_be64(p->regs + UTL_RC_STATUS);
	out_be64(p->regs + UTL_RC_STATUS, err);

	/* PHB error traps registers */
	err = in_be64(p->regs + PHB_ERR_STATUS);
	out_be64(p->regs + PHB_ERR_STATUS, err);
	out_be64(p->regs + PHB_ERR1_STATUS, 0);
	out_be64(p->regs + PHB_ERR_LOG_0, 0);
	out_be64(p->regs + PHB_ERR_LOG_1, 0);

	err = in_be64(p->regs + PHB_OUT_ERR_STATUS);
	out_be64(p->regs + PHB_OUT_ERR_STATUS, err);
	out_be64(p->regs + PHB_OUT_ERR1_STATUS, 0);
	out_be64(p->regs + PHB_OUT_ERR_LOG_0, 0);
	out_be64(p->regs + PHB_OUT_ERR_LOG_1, 0);

	err = in_be64(p->regs + PHB_INA_ERR_STATUS);
	out_be64(p->regs + PHB_INA_ERR_STATUS, err);
	out_be64(p->regs + PHB_INA_ERR1_STATUS, 0);
	out_be64(p->regs + PHB_INA_ERR_LOG_0, 0);
	out_be64(p->regs + PHB_INA_ERR_LOG_1, 0);

	err = in_be64(p->regs + PHB_INB_ERR_STATUS);
	out_be64(p->regs + PHB_INB_ERR_STATUS, err);
	out_be64(p->regs + PHB_INB_ERR1_STATUS, 0);
	out_be64(p->regs + PHB_INB_ERR_LOG_0, 0);
	out_be64(p->regs + PHB_INB_ERR_LOG_1, 0);

	/* Rec 67, 68 LEM */
	out_be64(p->regs + PHB_LEM_FIR_AND_MASK, ~lem);
	out_be64(p->regs + PHB_LEM_WOF, 0);

 clear_pest:
	/* XXX We just clear the whole PESTA for MMIO clear and PESTB
	 * for DMA clear. We might want to only clear the frozen bit
	 * as to not clobber the rest of the state. However, we expect
	 * the state to have been harvested before the clear operations
	 * so this might not be an issue
	 */
	if (eeh_action_token & OPAL_EEH_ACTION_CLEAR_FREEZE_MMIO) {
		p7ioc_phb_ioda_sel(p, IODA_TBL_PESTA, pe_number, false);
		out_be64(p->regs + PHB_IODA_DATA0, 0);
	}
	if (eeh_action_token & OPAL_EEH_ACTION_CLEAR_FREEZE_DMA) {
		p7ioc_phb_ioda_sel(p, IODA_TBL_PESTB, pe_number, false);
		out_be64(p->regs + PHB_IODA_DATA0, 0);
	}

	/* Update ER pending indication */
	p7ioc_phb_ioda_sel(p, IODA_TBL_PEEV, 0, true);
	peev0 = in_be64(p->regs + PHB_IODA_DATA0);
	peev1 = in_be64(p->regs + PHB_IODA_DATA0);
	p->er_pending = (peev0 || peev1);

	return OPAL_SUCCESS;
}

static int64_t p7ioc_get_diag_data(struct phb *phb, void *diag_buffer,
				   uint64_t diag_buffer_len)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);

	if (diag_buffer_len < sizeof(struct OpalIoP7IOCPhbErrorData))
		return OPAL_PARAMETER;

	p7ioc_eeh_read_phb_status(p, (struct OpalIoP7IOCPhbErrorData *)
				  diag_buffer);

	return OPAL_SUCCESS;
}

static int64_t p7ioc_phb_mmio_enable(struct phb *phb __unused,
				     uint16_t window_type __unused,
				     uint16_t window_num __unused,
				     uint16_t enable __unused)
{
	/* XXX We don't support that function yet, M32 is enabled by
	 * default and that's it for now. Linux doesn't use it yet
	 */
	return OPAL_UNSUPPORTED;
}

static int64_t p7ioc_set_phb_mem_window(struct phb *phb __unused,
					uint16_t window_type __unused,
					uint16_t window_num __unused,
					uint64_t starting_real_addr  __unused,
					uint64_t starting_pci_addr  __unused,
					uint16_t segment_size __unused)
{
	/* XXX We don't support that function yet, M32 is pre-configured
	 * by default and that's it for now. Linux doesn't use it yet
	 */
	return OPAL_UNSUPPORTED;
}

static int64_t p7ioc_map_pe_mmio_window(struct phb *phb, uint16_t pe_number,
					uint16_t window_type,
					uint16_t window_num,
					uint16_t segment_num)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t tbl, index;
	uint64_t *cache;

	if (pe_number > 127)
		return OPAL_PARAMETER;

	switch(window_type) {
	case OPAL_IO_WINDOW_TYPE:
		if (window_num != 0 || segment_num > 127)
			return OPAL_PARAMETER;
		tbl = IODA_TBL_IODT;
		index = segment_num;
		cache = &p->iod_cache[index];
		break;
	case OPAL_M32_WINDOW_TYPE:
		if (window_num != 0 || segment_num > 127)
			return OPAL_PARAMETER;
		tbl = IODA_TBL_M32DT;
		index = segment_num;
		cache = &p->m32d_cache[index];
		break;
	case OPAL_M64_WINDOW_TYPE:
		if (window_num > 15 || segment_num > 7)
			return OPAL_PARAMETER;

		tbl = IODA_TBL_M64DT;
		index = window_num << 3 | segment_num;
		cache = &p->m64d_cache[index];
		break;
	default:
		return OPAL_PARAMETER;
	}

	p7ioc_phb_ioda_sel(p, tbl, index, false);
	out_be64(p->regs + PHB_IODA_DATA0,
		 SETFIELD(IODA_XXDT_PE, 0ull, pe_number));

	/* Update cache */
	*cache = SETFIELD(IODA_XXDT_PE, 0ull, pe_number);

	return OPAL_SUCCESS;
}


static int64_t p7ioc_set_pe(struct phb *phb, uint64_t pe_number,
			    uint64_t bdfn, uint8_t bus_compare,
			    uint8_t dev_compare, uint8_t func_compare,
			    uint8_t pe_action)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t pelt;
	uint64_t *cache = &p->peltm_cache[pe_number];

	if (pe_number > 127 || bdfn > 0xffff)
		return OPAL_PARAMETER;
	if (pe_action != OPAL_MAP_PE && pe_action != OPAL_UNMAP_PE)
		return OPAL_PARAMETER;
	if (bus_compare > 7)
		return OPAL_PARAMETER;

	if (pe_action == OPAL_MAP_PE) {
		pelt  = SETFIELD(IODA_PELTM_BUS, 0ul, bdfn >> 8);
		pelt |= SETFIELD(IODA_PELTM_DEV, 0ul, (bdfn >> 3) & 0x1f);
		pelt |= SETFIELD(IODA_PELTM_FUNC, 0ul, bdfn & 0x7);
		pelt |= SETFIELD(IODA_PELTM_BUS_VALID, 0ul, bus_compare);
		if (dev_compare)
			pelt |= IODA_PELTM_DEV_VALID;
		if (func_compare)
			pelt |= IODA_PELTM_FUNC_VALID;
	} else
		pelt = 0;

	p7ioc_phb_ioda_sel(p, IODA_TBL_PELTM, pe_number, false);
	out_be64(p->regs + PHB_IODA_DATA0, pelt);

	/* Update cache */
	*cache = pelt;

	return OPAL_SUCCESS;
}


static int64_t p7ioc_set_peltv(struct phb *phb, uint32_t parent_pe,
			       uint32_t child_pe, uint8_t state)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint32_t reg;
	uint64_t mask, peltv;
	uint64_t *cache;
	if (parent_pe > 127 || child_pe > 127)
		return OPAL_PARAMETER;

	cache = (child_pe >> 6) ? &p->peltv_hi_cache[parent_pe] :
		&p->peltv_lo_cache[parent_pe];
	reg = (child_pe >> 6) ? PHB_IODA_DATA1 : PHB_IODA_DATA0;
	child_pe &= 0x2f;
	mask = 1ull << (63 - child_pe);

	p7ioc_phb_ioda_sel(p, IODA_TBL_PELTV, parent_pe, false);
	peltv = in_be64(p->regs + reg);
	if (state)
		peltv |= mask;
	else
		peltv &= ~mask;
	out_be64(p->regs + reg, peltv);

	/* Update cache */
	*cache = peltv;

	return OPAL_SUCCESS;
}

static int64_t p7ioc_map_pe_dma_window(struct phb *phb, uint16_t pe_number,
				       uint16_t window_id, uint16_t tce_levels,
				       uint64_t tce_table_addr,
				       uint64_t tce_table_size,
				       uint64_t tce_page_size)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t tvt0, tvt1, t, pelt;
	uint64_t dma_window_size;
	uint64_t *cache_lo, *cache_hi;

	if (pe_number > 127 || window_id > 255 || tce_levels != 1)
		return OPAL_PARAMETER;
	cache_lo = &p->tve_lo_cache[window_id];
        cache_hi = &p->tve_hi_cache[window_id];

	/* Encode table size */
	dma_window_size = tce_page_size * (tce_table_size >> 3);
	t = ilog2(dma_window_size);
	if (t < 27)
		return OPAL_PARAMETER;
	tvt0 = SETFIELD(IODA_TVT0_TCE_TABLE_SIZE, 0, (t - 26));

	/* Encode TCE page size */
	switch(tce_page_size) {
	case 0x1000:		/* 4K */
		tvt1 = SETFIELD(IODA_TVT1_IO_PSIZE, 0ul, 1ul);
		break;
	case 0x10000:		/* 64K */
		tvt1 = SETFIELD(IODA_TVT1_IO_PSIZE, 0ul, 5ul);
		break;
	case 0x1000000:		/* 16M */
		tvt1 = SETFIELD(IODA_TVT1_IO_PSIZE, 0ul, 13ul);
		break;
	case 0x400000000:	/* 16G */
		tvt1 = SETFIELD(IODA_TVT1_IO_PSIZE, 0ul, 23ul);
		break;
	default:
		return OPAL_PARAMETER;
	}

	/* XXX Hub number ... leave 0 for now */

	/* Shift in the address. The table address is "off by 4 bits"
	 * but since the field is itself shifted by 16, we basically
	 * need to write the address >> 12, which basically boils down
	 * to writing a 4k page address
	 */
	tvt0 = SETFIELD(IODA_TVT0_TABLE_ADDR, tvt0, tce_table_addr >> 12);

	/* Read the PE filter info from the PELT-M */
	p7ioc_phb_ioda_sel(p, IODA_TBL_PELTM, pe_number, false);
	pelt = in_be64(p->regs + PHB_IODA_DATA0);

	/* Copy in filter bits from PELT */
	tvt0 = SETFIELD(IODA_TVT0_BUS_VALID, tvt0,
			GETFIELD(IODA_PELTM_BUS_VALID, pelt));
	tvt0 = SETFIELD(IODA_TVT0_BUS_NUM, tvt0,
			GETFIELD(IODA_PELTM_BUS, pelt));
	tvt1 = SETFIELD(IODA_TVT1_DEV_NUM, tvt1,
			GETFIELD(IODA_PELTM_DEV, pelt));
	tvt1 = SETFIELD(IODA_TVT1_FUNC_NUM, tvt1,
			GETFIELD(IODA_PELTM_FUNC, pelt));
	if (pelt & IODA_PELTM_DEV_VALID)
		tvt1 |= IODA_TVT1_DEV_VALID;
	if (pelt & IODA_PELTM_FUNC_VALID)
		tvt1 |= IODA_TVT1_FUNC_VALID;
	tvt1 = SETFIELD(IODA_TVT1_PE_NUM, tvt1, pe_number);

	/* Write the TVE */
	p7ioc_phb_ioda_sel(p, IODA_TBL_TVT, window_id, false);
	out_be64(p->regs + PHB_IODA_DATA1, tvt1);
	out_be64(p->regs + PHB_IODA_DATA0, tvt0);

	/* Update cache */
	*cache_lo = tvt0;
	*cache_hi = tvt1;

	return OPAL_SUCCESS;
}

static int64_t p7ioc_map_pe_dma_window_real(struct phb *phb __unused,
					    uint16_t pe_number __unused,
					    uint16_t dma_window_num __unused,
					    uint64_t pci_start_addr __unused,
					    uint64_t pci_mem_size __unused)
{
	/* XXX Not yet implemented (not yet used by Linux) */
	return OPAL_UNSUPPORTED;
}

static int64_t p7ioc_set_mve(struct phb *phb, uint32_t mve_number,
			     uint32_t pe_number)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t pelt, mve = 0;
	uint64_t *cache = &p->mve_cache[mve_number];

	if (pe_number > 127 || mve_number > 255)
		return OPAL_PARAMETER;

	/* Read the PE filter info from the PELT-M */
	p7ioc_phb_ioda_sel(p, IODA_TBL_PELTM, pe_number, false);
	pelt = in_be64(p->regs + PHB_IODA_DATA0);

	mve = SETFIELD(IODA_MVT_BUS_VALID, mve,
		       GETFIELD(IODA_PELTM_BUS_VALID, pelt));
	mve = SETFIELD(IODA_MVT_BUS_NUM, mve,
		       GETFIELD(IODA_PELTM_BUS, pelt));
	mve = SETFIELD(IODA_MVT_DEV_NUM, mve,
		       GETFIELD(IODA_PELTM_DEV, pelt));
	mve = SETFIELD(IODA_MVT_FUNC_NUM, mve,
		       GETFIELD(IODA_PELTM_FUNC, pelt));
	if (pelt & IODA_PELTM_DEV_VALID)
		mve |= IODA_MVT_DEV_VALID;
	if (pelt & IODA_PELTM_FUNC_VALID)
		mve |= IODA_MVT_FUNC_VALID;
	mve = SETFIELD(IODA_MVT_PE_NUM, mve, pe_number);

	p7ioc_phb_ioda_sel(p, IODA_TBL_MVT, mve_number, false);
	out_be64(p->regs + PHB_IODA_DATA0, mve);

	/* Update cache */
	*cache = mve;

	return OPAL_SUCCESS;
}

static int64_t p7ioc_set_mve_enable(struct phb *phb, uint32_t mve_number,
				    uint32_t state)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t mve;
	uint64_t *cache = &p->mve_cache[mve_number];

	if (mve_number > 255)
		return OPAL_PARAMETER;

	p7ioc_phb_ioda_sel(p, IODA_TBL_MVT, mve_number, false);
	mve = in_be64(p->regs + PHB_IODA_DATA0);
	if (state)
		mve |= IODA_MVT_VALID;
	else
		mve &= ~IODA_MVT_VALID;
	out_be64(p->regs + PHB_IODA_DATA0, mve);

	/* Update cache */
	*cache = mve;

	return OPAL_SUCCESS;
}

static int64_t p7ioc_set_xive_pe(struct phb *phb, uint32_t pe_number,
				 uint32_t xive_num)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	uint64_t xive;

	if (pe_number > 127 || xive_num > 255)
		return OPAL_PARAMETER;

	/* Update MXIVE cache */
	xive = p->mxive_cache[xive_num];
	xive = SETFIELD(IODA_XIVT_PENUM, xive, pe_number);
	p->mxive_cache[xive_num] = xive;

	/* Update HW */
	p7ioc_phb_ioda_sel(p, IODA_TBL_MXIVT, xive_num, false);	
	xive = in_be64(p->regs + PHB_IODA_DATA0);
	xive = SETFIELD(IODA_XIVT_PENUM, xive, pe_number);
	out_be64(p->regs + PHB_IODA_DATA0, xive);

	return OPAL_SUCCESS;
}

static int64_t p7ioc_get_xive_source(struct phb *phb, uint32_t xive_num,
				     int32_t *interrupt_source_number)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);

	if (xive_num > 255 || !interrupt_source_number)
		return OPAL_PARAMETER;

	*interrupt_source_number = (p->buid_msi << 4) | xive_num;

	return OPAL_SUCCESS;
}

static int64_t p7ioc_get_msi_32(struct phb *phb __unused, uint32_t mve_number,
				uint32_t xive_num, uint8_t msi_range,
				uint32_t *msi_address, uint32_t *message_data)
{
	if (mve_number > 255 || xive_num > 255 || msi_range != 1)
		return OPAL_PARAMETER;

	*msi_address = 0xffff0000 | (mve_number << 4);
	*message_data = xive_num;

	return OPAL_SUCCESS;
}

static int64_t p7ioc_get_msi_64(struct phb *phb __unused, uint32_t mve_number,
				uint32_t xive_num, uint8_t msi_range,
				uint64_t *msi_address, uint32_t *message_data)
{
	if (mve_number > 255 || xive_num > 255 || msi_range != 1)
		return OPAL_PARAMETER;

	*msi_address = (9ul << 60) | (((u64)mve_number) << 48);
	*message_data = xive_num;

	return OPAL_SUCCESS;
}

static uint8_t p7ioc_choose_bus(struct phb *phb __unused,
				struct pci_device *bridge,
				uint8_t candidate, uint8_t *max_bus,
				bool *use_max)
{
	uint8_t m, al;
	int i;	

	/* Bus number selection is nasty on P7IOC. Our EEH HW can only cope
	 * with bus ranges that are naturally aligned powers of two. It also
	 * has "issues" with dealing with more than 32 bus numbers.
	 *
	 * On the other hand we can deal with overlaps to some extent as
	 * the PELT-M entries are ordered.
	 *
	 * We also don't need to bother with the busses between the upstream
	 * and downstream ports of switches.
	 *
	 * For now we apply this simple mechanism which matche what OFW does
	 * under OPAL:
	 *
	 * - Top level bus (PHB to RC) is 0
	 * - RC to first device is 1..ff
	 * - Then going down, a switch gets (N = parent bus, M = parent max)
	 *       * Upstream bridge is N+1, M, use_max = false
	 *       * Downstream bridge is closest power of two from 32 down and
	 *       * use max
	 *
	 * XXX NOTE: If we have access to HW VPDs, we could know whether
	 * this is a bridge with a single device on it such as IPR and
	 * limit ourselves to a single bus number.
	 */

	/* Default use_max is false (legacy) */
	*use_max = false;

	/* If we are the root complex or we are not in PCIe land anymore, just
	 * use legacy algorithm
	 */
	if (!bridge || !bridge->is_pcie)
		return candidate;

	/* Figure out the bridge type */
	switch(bridge->dev_type) {
	case PCIE_TYPE_PCIX_TO_PCIE:
		/* PCI-X to PCIE ... hrm, let's not bother too much with that */
		return candidate;
	case PCIE_TYPE_SWITCH_UPPORT:
	case PCIE_TYPE_ROOT_PORT:
		/* Upstream port, we use legacy handling as well */
		return candidate;
	case PCIE_TYPE_SWITCH_DNPORT:
	case PCIE_TYPE_PCIE_TO_PCIX:
		/* That leaves us with the interesting cases that we handle */
		break;
	default:
		/* Should not happen, treat as legacy */
		prerror("PCI: Device %04x has unsupported type %d in choose_bus\n",
			bridge->bdfn, bridge->dev_type);
		return candidate;
	}

	/* Ok, let's find a power of two that fits, fallback to 1 */
	for (i = 5; i >= 0; i--) {
		m = (1 << i) - 1;
		al = (candidate + m) & ~m;
		if (al <= *max_bus && (al + m) <= *max_bus)
			break;
	}
	if (i < 0)
		return 0;
	*use_max = true;
	*max_bus = al + m;
	return al;
}

/* p7ioc_phb_init_ioda_cache - Reset the IODA cache values
 */
static void p7ioc_phb_init_ioda_cache(struct p7ioc_phb *p)
{
	unsigned int i;

	for (i = 0; i < 8; i++)
		p->lxive_cache[i] = SETFIELD(IODA_XIVT_PRIORITY, 0ull, 0xff);
	for (i = 0; i < 256; i++) {
		p->mxive_cache[i] = SETFIELD(IODA_XIVT_PRIORITY, 0ull, 0xff);
		p->mve_cache[i]   = 0;
	}
	for (i = 0; i < 127; i++) {
		p->peltm_cache[i]	= 0;
		p->peltv_lo_cache[i]	= 0;
		p->peltv_hi_cache[i]	= 0;
		p->tve_lo_cache[i]	= 0;
		p->tve_hi_cache[i]	= 0;
		p->iod_cache[i]		= 0;
		p->m32d_cache[i]	= 0;
		p->m64d_cache[i]	= 0;
	}
}

/* p7ioc_phb_ioda_reset - Reset the IODA tables
 *
 * @purge: If true, the cache is cleared and the cleared values
 *         are applied to HW. If false, the cached values are
 *         applied to HW
 *
 * This reset the IODA tables in the PHB. It is called at
 * initialization time, on PHB reset, and can be called
 * explicitly from OPAL
 */
static int64_t p7ioc_ioda_reset(struct phb *phb, bool purge)
{
	struct p7ioc_phb *p = phb_to_p7ioc_phb(phb);
	unsigned int i;
	uint64_t reg64;
	uint64_t data64, data64_hi;

	/* If the "purge" argument is set, we clear the table cache */
	if (purge)
		p7ioc_phb_init_ioda_cache(p);

	/* Init_18..19: Setup the HRT
	 *
	 * XXX NOTE: I still don't completely get that HRT business so
	 * I'll just mimmic BML and put the PHB number + 1 in there
	 */
	p7ioc_phb_ioda_sel(p, IODA_TBL_HRT, 0, true);
	out_be64(p->regs + PHB_IODA_DATA0, p->index + 1);
	out_be64(p->regs + PHB_IODA_DATA0, p->index + 1);
	out_be64(p->regs + PHB_IODA_DATA0, p->index + 1);
	out_be64(p->regs + PHB_IODA_DATA0, p->index + 1);

	/* Init_20..21: Cleanup the LXIVT
	 *
	 * We set the priority to FF (masked) and clear everything
	 * else. That means we leave the HRT index to 0 which is
	 * going to remain unmodified... for now.
	 */
	p7ioc_phb_ioda_sel(p, IODA_TBL_LXIVT, 0, true);
	for (i = 0; i < 8; i++) {
		data64 = p->lxive_cache[i];
		out_be64(p->regs + PHB_IODA_DATA0, data64);
	}

	/* Init_22..23: Cleanup the MXIVT
	 *
	 * We set the priority to FF (masked) and clear everything
	 * else. That means we leave the HRT index to 0 which is
	 * going to remain unmodified... for now.
	 */
	p7ioc_phb_ioda_sel(p, IODA_TBL_MXIVT, 0, true);
	for (i = 0; i < 256; i++) {
		data64 = p->mxive_cache[i];
		out_be64(p->regs + PHB_IODA_DATA0, data64);
	}

	/* Init_24..25: Cleanup the MVT */
	p7ioc_phb_ioda_sel(p, IODA_TBL_MVT, 0, true);
	for (i = 0; i < 256; i++) {
		data64 = p->mve_cache[i];
		out_be64(p->regs + PHB_IODA_DATA0, data64);
	}

	/* Init_26..27: Cleanup the PELTM
	 *
	 * A completely clear PELTM should make everything match PE 0
	 */
	p7ioc_phb_ioda_sel(p, IODA_TBL_PELTM, 0, true);
	for (i = 0; i < 127; i++) {
		data64 = p->peltm_cache[i];
		out_be64(p->regs + PHB_IODA_DATA0, data64);
	}

	/* Init_28..30: Cleanup the PELTV */
	p7ioc_phb_ioda_sel(p, IODA_TBL_PELTV, 0, true);
	for (i = 0; i < 127; i++) {
		data64 = p->peltv_lo_cache[i];
		data64_hi = p->peltv_hi_cache[i];
		out_be64(p->regs + PHB_IODA_DATA1, data64_hi);
		out_be64(p->regs + PHB_IODA_DATA0, data64);
	}

	/* Init_31..33: Cleanup the TVT */
	p7ioc_phb_ioda_sel(p, IODA_TBL_TVT, 0, true);
	for (i = 0; i < 127; i++) {
		data64 = p->tve_lo_cache[i];
		data64_hi = p->tve_hi_cache[i];
		out_be64(p->regs + PHB_IODA_DATA1, data64_hi);
		out_be64(p->regs + PHB_IODA_DATA0, data64);
	}

	/* Init_34..35: Cleanup the M64BT
	 *
	 * We don't enable M64 BARs by default. However,
	 * we shouldn't purge the hw and cache for it in
	 * future.
	 */
	p7ioc_phb_ioda_sel(p, IODA_TBL_M64BT, 0, true);
	for (i = 0; i < 16; i++)
		out_be64(p->regs + PHB_IODA_DATA0, 0);

	/* Init_36..37: Cleanup the IODT */
	p7ioc_phb_ioda_sel(p, IODA_TBL_IODT, 0, true);
	for (i = 0; i < 127; i++) {
		data64 = p->iod_cache[i];
		out_be64(p->regs + PHB_IODA_DATA0, data64);
	}

	/* Init_38..39: Cleanup the M32DT */
	p7ioc_phb_ioda_sel(p, IODA_TBL_M32DT, 0, true);
	for (i = 0; i < 127; i++) {
		data64 = p->m32d_cache[i];
		out_be64(p->regs + PHB_IODA_DATA0, data64);
	}

	/* Init_40..41: Cleanup the M64DT */
	p7ioc_phb_ioda_sel(p, IODA_TBL_M64DT, 0, true);
	for (i = 0; i < 127; i++) {
		data64 = p->m64d_cache[i];
		out_be64(p->regs + PHB_IODA_DATA0, 0);
	}

	/* Clear up the TCE cache */
	reg64 = in_be64(p->regs + PHB_PHB2_CONFIG);
	reg64 &= ~PHB_PHB2C_64B_TCE_EN;
	out_be64(p->regs + PHB_PHB2_CONFIG, reg64);
	reg64 |= PHB_PHB2C_64B_TCE_EN;
	out_be64(p->regs + PHB_PHB2_CONFIG, reg64);
	in_be64(p->regs + PHB_PHB2_CONFIG);

	return OPAL_SUCCESS;
}

static const struct phb_ops p7ioc_phb_ops = {
	.lock			= p7ioc_phb_lock,
	.unlock			= p7ioc_phb_unlock,
	.cfg_read8		= p7ioc_pcicfg_read8,
	.cfg_read16		= p7ioc_pcicfg_read16,
	.cfg_read32		= p7ioc_pcicfg_read32,
	.cfg_write8		= p7ioc_pcicfg_write8,
	.cfg_write16		= p7ioc_pcicfg_write16,
	.cfg_write32		= p7ioc_pcicfg_write32,
	.choose_bus		= p7ioc_choose_bus,
	.eeh_freeze_status	= p7ioc_eeh_freeze_status,
	.eeh_freeze_clear	= p7ioc_eeh_freeze_clear,
	.get_diag_data		= p7ioc_get_diag_data,
	.next_error		= p7ioc_eeh_next_error,
	.phb_mmio_enable	= p7ioc_phb_mmio_enable,
	.set_phb_mem_window	= p7ioc_set_phb_mem_window,
	.map_pe_mmio_window	= p7ioc_map_pe_mmio_window,
	.set_pe			= p7ioc_set_pe,
	.set_peltv		= p7ioc_set_peltv,
	.map_pe_dma_window	= p7ioc_map_pe_dma_window,
	.map_pe_dma_window_real	= p7ioc_map_pe_dma_window_real,
	.set_mve		= p7ioc_set_mve,
	.set_mve_enable		= p7ioc_set_mve_enable,
	.set_xive_pe		= p7ioc_set_xive_pe,
	.get_xive_source	= p7ioc_get_xive_source,
	.get_msi_32		= p7ioc_get_msi_32,
	.get_msi_64		= p7ioc_get_msi_64,
	.ioda_reset		= p7ioc_ioda_reset,
	.presence_detect	= p7ioc_presence_detect,
	.link_state		= p7ioc_link_state,
	.power_state		= p7ioc_power_state,
	.slot_power_off		= p7ioc_slot_power_off,
	.slot_power_on		= p7ioc_slot_power_on,
	.hot_reset		= p7ioc_hot_reset,
	.poll			= p7ioc_poll,
};

/* p7ioc_phb_get_xive - Interrupt control from OPAL */
static int64_t p7ioc_msi_get_xive(void *data, uint32_t isn,
				  uint16_t *server, uint8_t *prio)
{
	struct p7ioc_phb *p = data;
	uint32_t irq, fbuid = IRQ_FBUID(isn);
	uint64_t xive;

	if (fbuid < p->buid_msi || fbuid >= (p->buid_msi + 0x10))
		return OPAL_PARAMETER;

	irq = isn & 0xff;
	xive = p->mxive_cache[irq];

	*server = GETFIELD(IODA_XIVT_SERVER, xive);
	*prio = GETFIELD(IODA_XIVT_PRIORITY, xive);

	return OPAL_SUCCESS;
}

/* p7ioc_phb_set_xive - Interrupt control from OPAL */
static int64_t p7ioc_msi_set_xive(void *data, uint32_t isn,
				  uint16_t server, uint8_t prio)
{
	struct p7ioc_phb *p = data;
	uint32_t irq, fbuid = IRQ_FBUID(isn);
	uint64_t xive, m_server, m_prio;

	if (fbuid < p->buid_msi || fbuid >= (p->buid_msi + 0x10))
		return OPAL_PARAMETER;

	/* We cache the arguments because we have to mangle
	 * it in order to hijack 3 bits of priority to extend
	 * the server number
	 */
	irq = isn & 0xff;
	xive = p->mxive_cache[irq];
	xive = SETFIELD(IODA_XIVT_SERVER, xive, server);
	xive = SETFIELD(IODA_XIVT_PRIORITY, xive, prio);
	p->mxive_cache[irq] = xive;

	/* Now we mangle the server and priority */
	if (prio == 0xff) {
		m_server = 0;
		m_prio = 0xff;
	} else {
		m_server = server >> 3;
		m_prio = (prio >> 3) | ((server & 7) << 5);
	}

	/* We use HRT entry 0 always for now */
	p7ioc_phb_ioda_sel(p, IODA_TBL_MXIVT, irq, false);
	xive = in_be64(p->regs + PHB_IODA_DATA0);
	xive = SETFIELD(IODA_XIVT_SERVER, xive, m_server);
	xive = SETFIELD(IODA_XIVT_PRIORITY, xive, m_prio);
	out_be64(p->regs + PHB_IODA_DATA0, xive);

	return OPAL_SUCCESS;
}

/* p7ioc_phb_get_xive - Interrupt control from OPAL */
static int64_t p7ioc_lsi_get_xive(void *data, uint32_t isn,
				  uint16_t *server, uint8_t *prio)
{
	struct p7ioc_phb *p = data;
	uint32_t irq, fbuid = IRQ_FBUID(isn);
	uint64_t xive;

	if (fbuid != p->buid_lsi)
		return OPAL_PARAMETER;
	irq = isn & 0xf;
	if (irq > 7)
		return OPAL_PARAMETER;

	xive = p->lxive_cache[irq];

	*server = GETFIELD(IODA_XIVT_SERVER, xive);
	*prio = GETFIELD(IODA_XIVT_PRIORITY, xive);

	return OPAL_SUCCESS;
}

/* p7ioc_phb_set_xive - Interrupt control from OPAL */
static int64_t p7ioc_lsi_set_xive(void *data, uint32_t isn,
				  uint16_t server, uint8_t prio)
{
	struct p7ioc_phb *p = data;
	uint32_t irq, fbuid = IRQ_FBUID(isn);
	uint64_t xive, m_server, m_prio;

	if (fbuid != p->buid_lsi)
		return OPAL_PARAMETER;
	irq = isn & 0xf;
	if (irq > 7)
		return OPAL_PARAMETER;

	xive = SETFIELD(IODA_XIVT_SERVER, 0ull, server);
	xive = SETFIELD(IODA_XIVT_PRIORITY, xive, prio);
	p->lxive_cache[irq] = xive;

	/* We cache the arguments because we have to mangle
	 * it in order to hijack 3 bits of priority to extend
	 * the server number
	 */

	/* Now we mangle the server and priority */
	if (prio == 0xff) {
		m_server = 0;
		m_prio = 0xff;
	} else {
		m_server = server >> 3;
		m_prio = (prio >> 3) | ((server & 7) << 5);
	}

	/* We use HRT entry 0 always for now */
	p7ioc_phb_ioda_sel(p, IODA_TBL_LXIVT, irq, false);
	xive = in_be64(p->regs + PHB_IODA_DATA0);
	xive = SETFIELD(IODA_XIVT_SERVER, xive, m_server);
	xive = SETFIELD(IODA_XIVT_PRIORITY, xive, m_prio);
	out_be64(p->regs + PHB_IODA_DATA0, xive);

	return OPAL_SUCCESS;
}

static void p7ioc_phb_err_interrupt(void *data, uint32_t isn)
{
	struct p7ioc_phb *p = data;
	uint64_t peev0, peev1;

	PHBDBG(p, "Got interrupt 0x%04x\n", isn);

	opal_update_pending_evt(OPAL_EVENT_PCI_ERROR, OPAL_EVENT_PCI_ERROR);

	/*
	 * If the PHB is broken, go away
	 */
	if (p->state == P7IOC_PHB_STATE_BROKEN)
		return;

	/*
	 * Check if there's an error pending and update PHB fence state
	 * and return, the ER error is drowned at this point
	 */
	lock(&p->lock);
	if (p7ioc_phb_fenced(p)) {
		p->state = P7IOC_PHB_STATE_FENCED;
		PHBERR(p, "ER error ignored, PHB fenced\n");
		unlock(&p->lock);
		return;
	}

	/* Check the PEEV */
	p7ioc_phb_ioda_sel(p, IODA_TBL_PEEV, 0, true);
	peev0 = in_be64(p->regs + PHB_IODA_DATA0);
	peev1 = in_be64(p->regs + PHB_IODA_DATA0);
	if (peev0 || peev1)
		p->er_pending = true;
	unlock(&p->lock);
}

/* MSIs (OS owned) */
static const struct irq_source_ops p7ioc_msi_irq_ops = {
	.get_xive = p7ioc_msi_get_xive,
	.set_xive = p7ioc_msi_set_xive,
};

/* LSIs (OS owned) */
static const struct irq_source_ops p7ioc_lsi_irq_ops = {
	.get_xive = p7ioc_lsi_get_xive,
	.set_xive = p7ioc_lsi_set_xive,
};

/* PHB Errors (Ski owned) */
static const struct irq_source_ops p7ioc_phb_err_irq_ops = {
	.get_xive = p7ioc_lsi_get_xive,
	.set_xive = p7ioc_lsi_set_xive,
	.interrupt = p7ioc_phb_err_interrupt,
};

/* p7ioc_phb_setup - Setup a p7ioc_phb data structure
 *
 * WARNING: This is called before the AIB register routing is
 * established. If this wants to access PHB registers, it must
 * use the ASB hard coded variant (slower)
 */
void p7ioc_phb_setup(struct p7ioc *ioc, uint8_t index, bool active)
{
	struct p7ioc_phb *p = &ioc->phbs[index];
	unsigned int buid_base = ioc->buid_base + PHBn_BUID_BASE(index);

	p->index = index;
	p->ioc = ioc;
	p->active = active;
	p->phb.ops = &p7ioc_phb_ops;
	p->phb.phb_type = phb_type_pcie_v2;
	p->regs_asb = ioc->regs + PHBn_ASB_BASE(index);
	p->regs = ioc->regs + PHBn_AIB_BASE(index);
	p->buid_lsi = buid_base + PHB_BUID_LSI_OFFSET;
	p->buid_msi = buid_base + PHB_BUID_MSI_OFFSET;
	p->io_base = ioc->mmio1_win_start + PHBn_IO_BASE(index);
	p->m32_base = ioc->mmio2_win_start + PHBn_M32_BASE(index);
	p->m64_base = ioc->mmio2_win_start + PHBn_M64_BASE(index);
	p->state = P7IOC_PHB_STATE_UNINITIALIZED;
	p->phb.scan_map = 0x1; /* Only device 0 to scan */

	/* Register OS interrupt sources */
	register_irq_source(&p7ioc_msi_irq_ops, p, p->buid_msi << 4, 256);
	register_irq_source(&p7ioc_lsi_irq_ops, p, p->buid_lsi << 4, 4);

	/* Register internal interrupt source (LSI 7) */
	register_irq_source(&p7ioc_phb_err_irq_ops, p,
			    (p->buid_lsi << 4) + 7, 1);

	/* Initialize IODA table caches */
	p7ioc_phb_init_ioda_cache(p);

	/* We register the PHB before we initialize it so we
	 * get a useful OPAL ID for it
	 */
	pci_register_phb(&p->phb);
}

/* Synchronous PEST code used at boot */
static void p7ioc_phb_sync_perst(struct p7ioc_phb *p)
{
	uint64_t reg;
	unsigned int timeout;

	/* XXX This is only needed if the slot is powered up ...
	 *
	 * It seems that the slots are off after we do a hot
	 * reset sequence, so we probably want to consider
	 * turning power with PERST asserted or something like
	 * that in that case...
	 */

	/* Disable link to avoid training issues */
	reg = in_be64(p->regs + PHB_PCIE_DLP_TRAIN_CTL);
	PHBDBG(p, "[PERST] old TRAIN_CTL: 0x%016llx\n", reg);
	reg |= PHB_PCIE_DLP_TCTX_DISABLE;
	PHBDBG(p, "[PERST] wr  TRAIN_CTL: 0x%016llx\n", reg);
	out_be64(p->regs + PHB_PCIE_DLP_TRAIN_CTL, reg);

	/* Wait for link disabled status */
	for (timeout = 0; timeout < 12; timeout++) {
		reg = in_be64(p->regs + PHB_PCIE_DLP_TRAIN_CTL);
		if (reg & PHB_PCIE_DLP_TCRX_DISABLED)
			break;
		time_wait_ms(10);
	}

	PHBDBG(p, "[PERST] new TRAIN_CTL: 0x%016llx\n", reg);

	/* Link disable not set, what do we do ? We seem to hit that
	 * when doing the boot-time PERST and so far nothing bad
	 * seem to have come from it but definitely worth investigating
	 */
	if (!(reg & PHB_PCIE_DLP_TCRX_DISABLED))
		PHBERR(p, "Timeout waiting for link disable !\n");

	/* Issue PERST. We keep PERST asserted for 1s like pHyp */
	reg = in_be64(p->regs + PHB_RESET);
	reg &= ~0x2000000000000000ul;
	out_be64(p->regs + PHB_RESET, reg);
	time_wait_ms(1000);
	reg |= 0x2000000000000000ul;
	out_be64(p->regs + PHB_RESET, reg);

	/* Wait 200ms for things to settle */
	time_wait_ms(200);

	/* Restore link control */
	reg = in_be64(p->regs + PHB_PCIE_DLP_TRAIN_CTL);
	reg &= ~PHB_PCIE_DLP_TCTX_DISABLE;
	out_be64(p->regs + PHB_PCIE_DLP_TRAIN_CTL, reg);
}

static bool p7ioc_phb_wait_dlp_reset(struct p7ioc_phb *p)
{
	unsigned int i;
	uint64_t val;

	/*
	 * Firmware cannot access the UTL core regs or PCI config space
	 * until the cores are out of DL_PGRESET.
	 * DL_PGRESET should be polled until it is inactive with a value
	 * of '0'. The recommended polling frequency is once every 1ms.
	 * Firmware should poll at least 200 attempts before giving up.
	 * MMIO Stores to the link are silently dropped by the UTL core if
	 * the link is down.
	 * MMIO Loads to the link will be dropped by the UTL core and will
	 * eventually time-out and will return an all ones response if the
	 * link is down.
	 */
#define DLP_RESET_ATTEMPTS	400

	printf("P7IOC: Waiting for DLP PG reset to complete...\n");
	for (i = 0; i < DLP_RESET_ATTEMPTS; i++) {
		val = in_be64(p->regs + PHB_PCIE_DLP_TRAIN_CTL);
		if (!(val & PHB_PCIE_DLP_TC_DL_PGRESET))
			break;
		time_wait_ms(1);
	}
	if (val & PHB_PCIE_DLP_TC_DL_PGRESET) {
		PHBERR(p, "Timeout waiting for DLP PG reset !\n");
		return false;
	}
	return true;
}

/* p7ioc_phb_init_rc - Initialize the Root Complex config space
 */
static bool p7ioc_phb_init_rc_cfg(struct p7ioc_phb *p)
{
	int64_t ecap, aercap;

	/* XXX Handle errors ? */

	/* Init_51..51:
	 *
	 * Set primary bus to 0, secondary to 1 and subordinate to 0xff
	 */
	p7ioc_pcicfg_write32(&p->phb, 0, PCI_CFG_PRIMARY_BUS, 0x00ff0100);

	/* Init_52..57
	 *
	 * IO and Memory base & limits are set to base > limit, which
	 * allows all inbounds.
	 *
	 * XXX This has the potential of confusing the OS which might
	 * think that nothing is forwarded downstream. We probably need
	 * to fix this to match the IO and M32 PHB windows
	 */
	p7ioc_pcicfg_write16(&p->phb, 0, PCI_CFG_IO_BASE, 0x0010);
	p7ioc_pcicfg_write32(&p->phb, 0, PCI_CFG_MEM_BASE, 0x00000010);
	p7ioc_pcicfg_write32(&p->phb, 0, PCI_CFG_PREF_MEM_BASE, 0x00000010);

	/* Init_58..: Setup bridge control to enable forwarding of CORR, FATAL,
	 * and NONFATAL errors
	*/
	p7ioc_pcicfg_write16(&p->phb, 0, PCI_CFG_BRCTL, PCI_CFG_BRCTL_SERR_EN);

	/* Init_60..61
	 *
	 * PCIE Device control/status, enable error reporting, disable relaxed
	 * ordering, set MPS to 128 (see note), clear errors.
	 *
	 * Note: The doc recommends to set MPS to 4K. This has proved to have
	 * some issues as it requires specific claming of MRSS on devices and
	 * we've found devices in the field that misbehave when doing that.
	 *
	 * We currently leave it all to 128 bytes (minimum setting) at init
	 * time. The generic PCIe probing later on might apply a different
	 * value, or the kernel will, but we play it safe at early init
	 */
	ecap = pci_find_cap(&p->phb, 0, PCI_CFG_CAP_ID_EXP);
	if (ecap < 0) {
		/* Shouldn't happen */
		PHBERR(p, "Failed to locate PCI-E capability in bridge\n");
		return false;
	}
	p->ecap = ecap;

	p7ioc_pcicfg_write16(&p->phb, 0, ecap + PCICAP_EXP_DEVSTAT,
			     PCICAP_EXP_DEVSTAT_CE	|
			     PCICAP_EXP_DEVSTAT_NFE	|
			     PCICAP_EXP_DEVSTAT_FE	|
			     PCICAP_EXP_DEVSTAT_UE);

	p7ioc_pcicfg_write16(&p->phb, 0, ecap + PCICAP_EXP_DEVCTL,
			     PCICAP_EXP_DEVCTL_CE_REPORT	|
			     PCICAP_EXP_DEVCTL_NFE_REPORT	|
			     PCICAP_EXP_DEVCTL_FE_REPORT	|
			     PCICAP_EXP_DEVCTL_UR_REPORT	|
			     SETFIELD(PCICAP_EXP_DEVCTL_MPS, 0, PCIE_MPS_128B));

	/* Init_62..63
	 *
	 * Root Control Register. Enable error reporting
	 *
	 * Note: Added CRS visibility.
	 */
	p7ioc_pcicfg_write16(&p->phb, 0, ecap + PCICAP_EXP_RC,
			     PCICAP_EXP_RC_SYSERR_ON_CE		|
			     PCICAP_EXP_RC_SYSERR_ON_NFE	|
			     PCICAP_EXP_RC_SYSERR_ON_FE		|
			     PCICAP_EXP_RC_CRS_VISIBLE);

	/* Init_64..65
	 *
	 * Device Control 2. Enable ARI fwd, set timer
	 */
	p7ioc_pcicfg_write16(&p->phb, 0, ecap + PCICAP_EXP_DCTL2,
			     SETFIELD(PCICAP_EXP_DCTL2_CMPTOUT, 0, 2) |
			     PCICAP_EXP_DCTL2_ARI_FWD);

	/* Init_66..81
	 *
	 * AER inits
	 */
	aercap = pci_find_ecap(&p->phb, 0, PCIECAP_ID_AER, NULL);
	if (aercap < 0) {
		/* Shouldn't happen */
		PHBERR(p, "Failed to locate AER Ecapability in bridge\n");
		return false;
	}
	p->aercap = aercap;

	/* Clear all UE status */
	p7ioc_pcicfg_write32(&p->phb, 0, aercap + PCIECAP_AER_UE_STATUS,
			     0xffffffff);
	/* Disable some error reporting as per the P7IOC spec */
	p7ioc_pcicfg_write32(&p->phb, 0, aercap + PCIECAP_AER_UE_MASK,
			     PCIECAP_AER_UE_POISON_TLP		|
			     PCIECAP_AER_UE_COMPL_TIMEOUT	|
			     PCIECAP_AER_UE_COMPL_ABORT		|
			     PCIECAP_AER_UE_ECRC);
	/* Report some errors as fatal */
	p7ioc_pcicfg_write32(&p->phb, 0, aercap + PCIECAP_AER_UE_SEVERITY,
			     PCIECAP_AER_UE_DLP 		|
			     PCIECAP_AER_UE_SURPRISE_DOWN	|
			     PCIECAP_AER_UE_FLOW_CTL_PROT	|
			     PCIECAP_AER_UE_UNEXP_COMPL		|
			     PCIECAP_AER_UE_RECV_OVFLOW		|
			     PCIECAP_AER_UE_MALFORMED_TLP);
	/* Clear all CE status */
	p7ioc_pcicfg_write32(&p->phb, 0, aercap + PCIECAP_AER_CE_STATUS,
			     0xffffffff);
	/* Disable some error reporting as per the P7IOC spec */
	p7ioc_pcicfg_write32(&p->phb, 0, aercap + PCIECAP_AER_CE_MASK,
			     PCIECAP_AER_CE_ADV_NONFATAL);
	/* Enable ECRC generation & checking */
	p7ioc_pcicfg_write32(&p->phb, 0, aercap + PCIECAP_AER_CAPCTL,
			     PCIECAP_AER_CAPCTL_ECRCG_EN	|
			     PCIECAP_AER_CAPCTL_ECRCC_EN);
	/* Enable reporting in root error control */
	p7ioc_pcicfg_write32(&p->phb, 0, aercap + PCIECAP_AER_RERR_CMD,
			     PCIECAP_AER_RERR_CMD_FE		|
			     PCIECAP_AER_RERR_CMD_NFE		|
			     PCIECAP_AER_RERR_CMD_CE);
	/* Clear root error status */
	p7ioc_pcicfg_write32(&p->phb, 0, aercap + PCIECAP_AER_RERR_STA,
			     0xffffffff);

	return true;
}

static void p7ioc_phb_init_utl(struct p7ioc_phb *p)
{
	/* Init_82..84: Clear spurrious errors and assign errors to the
	 * right "interrupt" signal
	 */
	out_be64(p->regs + UTL_SYS_BUS_AGENT_STATUS,       0xffffffffffffffff);
	out_be64(p->regs + UTL_SYS_BUS_AGENT_ERR_SEVERITY, 0x0000000000000000);
	out_be64(p->regs + UTL_SYS_BUS_AGENT_IRQ_EN,       0xac80000000000000);

	/* Init_85..89: Setup buffer allocations */
	out_be64(p->regs + UTL_OUT_POST_DAT_BUF_ALLOC,     0x0400000000000000);
	out_be64(p->regs + UTL_IN_POST_HDR_BUF_ALLOC,      0x1000000000000000);
	out_be64(p->regs + UTL_IN_POST_DAT_BUF_ALLOC,      0x4000000000000000);
	out_be64(p->regs + UTL_PCIE_TAGS_ALLOC,            0x0800000000000000);
	out_be64(p->regs + UTL_GBIF_READ_TAGS_ALLOC,       0x0800000000000000);

	/* Init_90: PCI Express port control */
	out_be64(p->regs + UTL_PCIE_PORT_CONTROL,          0x8480000000000000);

	/* Init_91..93: Clean & setup port errors */
	out_be64(p->regs + UTL_PCIE_PORT_STATUS,           0xff7fffffffffffff);
	out_be64(p->regs + UTL_PCIE_PORT_ERROR_SEV,        0x00e0000000000000);
	out_be64(p->regs + UTL_PCIE_PORT_IRQ_EN,           0x7e65000000000000);

	/* Init_94 : Cleanup RC errors */
	out_be64(p->regs + UTL_RC_STATUS,                  0xffffffffffffffff);
}

static void p7ioc_phb_init_errors(struct p7ioc_phb *p)
{
	/* Init_98: LEM Error Mask : Temporarily disable error interrupts */
	out_be64(p->regs + PHB_LEM_ERROR_MASK,		   0xffffffffffffffff);

	/* Init_99..107: Configure main error traps & clear old state */
	out_be64(p->regs + PHB_ERR_STATUS,		   0xffffffffffffffff);
	out_be64(p->regs + PHB_ERR1_STATUS,		   0x0000000000000000);
	out_be64(p->regs + PHB_ERR_LEM_ENABLE,		   0xffffffffefffffff);
	out_be64(p->regs + PHB_ERR_FREEZE_ENABLE,	   0x0000000061c00000);
	out_be64(p->regs + PHB_ERR_AIB_FENCE_ENABLE,	   0xffffffc58c000000);
	out_be64(p->regs + PHB_ERR_LOG_0,		   0x0000000000000000);
	out_be64(p->regs + PHB_ERR_LOG_1,		   0x0000000000000000);
	out_be64(p->regs + PHB_ERR_STATUS_MASK,		   0x0000000000000000);
	out_be64(p->regs + PHB_ERR1_STATUS_MASK,	   0x0000000000000000);

	/* Init_108_116: Configure MMIO error traps & clear old state */
	out_be64(p->regs + PHB_OUT_ERR_STATUS,		   0xffffffffffffffff);
	out_be64(p->regs + PHB_OUT_ERR1_STATUS,		   0x0000000000000000);
	out_be64(p->regs + PHB_OUT_ERR_LEM_ENABLE,	   0xffffffffffffffff);
	out_be64(p->regs + PHB_OUT_ERR_FREEZE_ENABLE,	   0x0000430803000000);
	out_be64(p->regs + PHB_OUT_ERR_AIB_FENCE_ENABLE,   0x9df3bc00f0f0700f);
	out_be64(p->regs + PHB_OUT_ERR_LOG_0,		   0x0000000000000000);
	out_be64(p->regs + PHB_OUT_ERR_LOG_1,		   0x0000000000000000);
	out_be64(p->regs + PHB_OUT_ERR_STATUS_MASK,	   0x0000000000000000);
	out_be64(p->regs + PHB_OUT_ERR1_STATUS_MASK,	   0x0000000000000000);

	/* Init_117_125: Configure DMA_A error traps & clear old state */
	out_be64(p->regs + PHB_INA_ERR_STATUS,		   0xffffffffffffffff);
	out_be64(p->regs + PHB_INA_ERR1_STATUS,		   0x0000000000000000);
	out_be64(p->regs + PHB_INA_ERR_LEM_ENABLE,	   0xffffffffffffffff);
	out_be64(p->regs + PHB_INA_ERR_FREEZE_ENABLE,	   0xc00003ff01006000);
	out_be64(p->regs + PHB_INA_ERR_AIB_FENCE_ENABLE,   0x3fff50007e559fd8);
	out_be64(p->regs + PHB_INA_ERR_LOG_0,		   0x0000000000000000);
	out_be64(p->regs + PHB_INA_ERR_LOG_1,		   0x0000000000000000);
	out_be64(p->regs + PHB_INA_ERR_STATUS_MASK,	   0x0000000000000000);
	out_be64(p->regs + PHB_INA_ERR1_STATUS_MASK,	   0x0000000000000000);

	/* Init_126_134: Configure DMA_B error traps & clear old state */
	out_be64(p->regs + PHB_INB_ERR_STATUS,		   0xffffffffffffffff);
	out_be64(p->regs + PHB_INB_ERR1_STATUS,		   0x0000000000000000);
	out_be64(p->regs + PHB_INB_ERR_LEM_ENABLE,	   0xffffffffffffffff);
	out_be64(p->regs + PHB_INB_ERR_FREEZE_ENABLE,	   0x0000000000000000);
	out_be64(p->regs + PHB_INB_ERR_AIB_FENCE_ENABLE,   0x18ff80ffff7f0000);
	out_be64(p->regs + PHB_INB_ERR_LOG_0,		   0x0000000000000000);
	out_be64(p->regs + PHB_INB_ERR_LOG_1,		   0x0000000000000000);
	out_be64(p->regs + PHB_INB_ERR_STATUS_MASK,	   0x0000000000000000);
	out_be64(p->regs + PHB_INB_ERR1_STATUS_MASK,	   0x0000000000000000);

	/* Init_135..138: Cleanup & configure LEM */
	out_be64(p->regs + PHB_LEM_FIR_ACCUM,		   0x0000000000000000);
	out_be64(p->regs + PHB_LEM_ACTION0,		   0xffffffffffffffff);
	out_be64(p->regs + PHB_LEM_ACTION1,		   0x0000000000000000);
	out_be64(p->regs + PHB_LEM_WOF,			   0x0000000000000000);
}

/* p7ioc_phb_init - Initialize the PHB hardware
 *
 * This is currently only called at boot time. It will eventually
 * be called at runtime, for example in some cases of error recovery
 * after a PHB reset in which case we might need locks etc... 
 */
int64_t p7ioc_phb_init(struct p7ioc_phb *p)
{
	uint64_t val;

	PHBDBG(p, "Initializing PHB %d...\n", p->index);

	p->state = P7IOC_PHB_STATE_INITIALIZING;

	/* For some reason, the doc wants us to read the version
	 * register, so let's do it. We shoud probably check that
	 * the value makes sense...
	 */
	val = in_be64(p->regs_asb + PHB_VERSION);

	PHBDBG(p, "Version reg: %llx\n", val);

	/*
	 * Configure AIB operations
	 *
	 * This register maps upbound commands to AIB channels.
	 * DMA Write=0, DMA Read=2, MMIO Load Response=1,
	 * Interrupt Request=1, TCE Read=3.
	 */
	/* Init_1: AIB TX Channel Mapping */
	out_be64(p->regs_asb + PHB_AIB_TX_CHAN_MAPPING,    0x0211300000000000);

	/*
	 * This group of steps initializes the AIB RX credits for
	 * the CI block’s port that is attached to this PHB.
	 *
	 * Channel 0 (Dkill): 32 command credits, 0 data credits
	 *                    (effectively infinite command credits)
	 * Channel 1 (DMA/TCE Read Responses): 32 command credits, 32 data
	 *                                     credits (effectively infinite
	 *                                     command and data credits)
	 * Channel 2 (Interrupt Reissue/Return): 32 command, 0 data credits
	 *                                       (effectively infinite
	 *                                       command credits)
	 * Channel 3 (MMIO Load/Stores, EOIs): 1 command, 1 data credit
	 */

	/* Init_2: AIB RX Command Credit */
	out_be64(p->regs_asb + PHB_AIB_RX_CMD_CRED,        0x0020002000200001);
	/* Init_3: AIB RX Data Credit */
	out_be64(p->regs_asb + PHB_AIB_RX_DATA_CRED,       0x0000002000000001);
	/* Init_4: AXIB RX Credit Init Timer */
	out_be64(p->regs_asb + PHB_AIB_RX_CRED_INIT_TIMER, 0xFF00000000000000);

	/*
	 * Enable all 32 AIB and TCE tags.
	 *
	 * AIB tags are used for DMA read requests.
	 * TCE tags are used for every internal transaction as well as TCE
	 * read requests.
	 */

	/* Init_5:  PHB - AIB Tag Enable Register */
	out_be64(p->regs_asb + PHB_AIB_TAG_ENABLE,         0xFFFFFFFF00000000);
	/* Init_6: PHB – TCE Tag Enable Register */
	out_be64(p->regs_asb + PHB_TCE_TAG_ENABLE,         0xFFFFFFFF00000000);

	/* Init_7: PCIE - System Configuration Register
	 *
	 * This is the default value out of reset. This register can be
	 * modified to change the following fields if needed:
	 *
	 *  bits 04:09 - SYS_EC0C_MAXLINKWIDTH[5:0]
	 *               The default link width is x8. This can be reduced
	 *               to x1 or x4, if needed.
	 *
	 *  bits 10:12 - SYS_EC04_MAX_PAYLOAD[2:0]
	 *
	 *               The default max payload size is 4KB. This can be
	 *               reduced to the allowed ranges from 128B
	 *               to 2KB if needed.
	 */
	out_be64(p->regs + PHB_PCIE_SYSTEM_CONFIG,         0x422800FC20000000);

	/* Init_8: PHB - PCI-E Reset Register
	 *
	 * This will deassert reset for the PCI-E cores, including the
	 * PHY and HSS macros. The TLDLP core will begin link training
	 * shortly after this register is written.
	 * This will also assert reset for the internal scan-only error
	 * report macros. The error report macro reset will be deasserted
	 * in a later step.
	 * Firmware will verify in a later step whether the PCI-E link
	 * has been established.
	 *
	 * NOTE: We perform a PERST at the end of the init sequence so
	 * we could probably skip that link training.
	 */
	out_be64(p->regs + PHB_RESET,                      0xE800000000000000);

	/* Init_9: BUID
	 *
	 * Only the top 5 bit of the MSI field are implemented, the bottom
	 * are always 0. Our buid_msi value should also be a multiple of
	 * 16 so it should all fit well
	 */
	val  = SETFIELD(PHB_BUID_LSI, 0ul, BUID_BASE(p->buid_lsi));
	val |= SETFIELD(PHB_BUID_MSI, 0ul, BUID_BASE(p->buid_msi));
	out_be64(p->regs + PHB_BUID, val);

	/* Init_10..12: IO Space */
	out_be64(p->regs + PHB_IO_BASE_ADDR, p->io_base);
	out_be64(p->regs + PHB_IO_BASE_MASK, ~(PHB_IO_SIZE - 1));
	out_be64(p->regs + PHB_IO_START_ADDR, 0);

	/* Init_13..15: M32 Space */
	out_be64(p->regs + PHB_M32_BASE_ADDR, p->m32_base + M32_PCI_START);
	out_be64(p->regs + PHB_M32_BASE_MASK, ~(M32_PCI_SIZE - 1));
	out_be64(p->regs + PHB_M32_START_ADDR, M32_PCI_START);

	/* Init_16: PCIE-E Outbound Request Upper Address */
	out_be64(p->regs + PHB_M64_UPPER_BITS, 0);

	/* Init_17: PCIE-E PHB2 Configuration
	 *
	 * We enable IO, M32, 32-bit MSI and 64-bit MSI
	 */
	out_be64(p->regs + PHB_PHB2_CONFIG,
		 PHB_PHB2C_32BIT_MSI_EN	|
		 PHB_PHB2C_IO_EN	|
		 PHB_PHB2C_64BIT_MSI_EN	|
		 PHB_PHB2C_M32_EN |
		 PHB_PHB2C_64B_TCE_EN);

	/* Init_18..xx: Reset all IODA tables */
	p7ioc_ioda_reset(&p->phb, false);

	/* Init_42..47: Clear UTL & DLP error log regs */
	out_be64(p->regs + PHB_PCIE_UTL_ERRLOG1,	   0xffffffffffffffff);
	out_be64(p->regs + PHB_PCIE_UTL_ERRLOG2,	   0xffffffffffffffff);
	out_be64(p->regs + PHB_PCIE_UTL_ERRLOG3,	   0xffffffffffffffff);
	out_be64(p->regs + PHB_PCIE_UTL_ERRLOG4,	   0xffffffffffffffff);
	out_be64(p->regs + PHB_PCIE_DLP_ERRLOG1,	   0xffffffffffffffff);
	out_be64(p->regs + PHB_PCIE_DLP_ERRLOG2,	   0xffffffffffffffff);

	/* Init_48: Wait for DLP core to be out of reset */
	if (!p7ioc_phb_wait_dlp_reset(p))
		goto failed;

	/* Init_50..81: Init root complex config space */
	if (!p7ioc_phb_init_rc_cfg(p))
		goto failed;

	/* Init_82..94 : Init UTL */
	p7ioc_phb_init_utl(p);

	/* Init_95: PCI-E Reset, deassert reset for internal error macros */
	out_be64(p->regs + PHB_RESET,			   0xe000000000000000);

	/* Init_96: PHB Control register. Various PHB settings:
	 *
	 * - Enable ECC for various internal RAMs
	 * - Enable all TCAM entries
	 * - Set failed DMA read requests to return Completer Abort on error
	 */
	out_be64(p->regs + PHB_CONTROL, 	       	   0x7f38000000000000);

	/* Init_97: Legacy Control register
	 *
	 * The spec sets bit 0 to enable DKill to flush the TCEs. We do not
	 * use that mechanism however, we require the OS to directly access
	 * the TCE Kill register, so we leave that bit set to 0
	 */
	out_be64(p->regs + PHB_LEGACY_CTRL,		   0x0000000000000000);

	/* Init_98..138  : Setup error registers */
	p7ioc_phb_init_errors(p);

	/* Init_139: Read error summary */
	val = in_be64(p->regs + PHB_ETU_ERR_SUMMARY);
	if (val) {
		PHBERR(p, "Errors detected during PHB init: 0x%16llx\n", val);
		goto failed;
	}

	/* Steps Init_140..142 have been removed from the spec. */

	/* Init_143..144: Enable IO, MMIO, Bus master etc... and clear
	 * status bits
	 */
	p7ioc_pcicfg_write16(&p->phb, 0, PCI_CFG_STAT,
			     PCI_CFG_STAT_SENT_TABORT	|
			     PCI_CFG_STAT_RECV_TABORT	|
			     PCI_CFG_STAT_RECV_MABORT	|
			     PCI_CFG_STAT_SENT_SERR	|
			     PCI_CFG_STAT_RECV_PERR);
	p7ioc_pcicfg_write16(&p->phb, 0, PCI_CFG_CMD,
			     PCI_CFG_CMD_SERR_EN	|
			     PCI_CFG_CMD_PERR_RESP	|
			     PCI_CFG_CMD_BUS_MASTER_EN	|
			     PCI_CFG_CMD_MEM_EN		|
			     PCI_CFG_CMD_IO_EN);

	/* At this point, the spec suggests doing a bus walk. However we
	 * haven't powered up the slots with the SHCP controller. We'll
	 * deal with that and link training issues later, for now, let's
	 * enable the full range of error detection
	 */

	/* Init_145..149: Enable error interrupts and LEM */
	out_be64(p->regs + PHB_ERR_IRQ_ENABLE,		   0x0000000061c00000);
	out_be64(p->regs + PHB_OUT_ERR_IRQ_ENABLE,	   0x0000430803000000);
	out_be64(p->regs + PHB_INA_ERR_IRQ_ENABLE,	   0xc00003ff01006000);
	out_be64(p->regs + PHB_INB_ERR_IRQ_ENABLE,	   0x0000000000000000);
	out_be64(p->regs + PHB_LEM_ERROR_MASK,		   0x1249a1147f500f2c);

	/* Init_150: Enable DMA read/write TLP address speculation */
	out_be64(p->regs + PHB_TCE_PREFETCH,		   0x0000c00000000000);

	/* Init_151..152: Set various timeouts */
	out_be64(p->regs + PHB_TIMEOUT_CTRL1,		   0x1611112010200000);
	out_be64(p->regs + PHB_TIMEOUT_CTRL2,		   0x0000561300000000);

	/* Mark the PHB as functional which enables all the various sequences */
	p->state = P7IOC_PHB_STATE_FUNCTIONAL;

	/* This is an addition to the standard sequence, we perform a PERST
	 * with the links disabled to work around a training issue
	 */
	p7ioc_phb_sync_perst(p);

	return OPAL_SUCCESS;

 failed:
	PHBERR(p, "Initialization failed\n");
	p->state = P7IOC_PHB_STATE_BROKEN;

	return OPAL_HARDWARE;
}

void p7ioc_phb_add_nodes(struct p7ioc_phb *p)
{
	char name[sizeof("pciex@") + STR_MAX_CHARS(p->regs)];
	static const char p7ioc_phb_compat[] =
		"ibm,p7ioc-pciex\0ibm,ioda-phb";
	uint64_t reg[2], m32b, iob, tkill;
	uint32_t lsibase, icsp = get_ics_phandle();
	struct pci_lsi_state lstate;

	reg[0] = cleanup_addr((uint64_t)p->regs);
	reg[1] = 0x100000;

	sprintf(name, "pciex@%llx", reg[0]);
	dt_begin_node(name);
	dt_property("compatible", p7ioc_phb_compat, sizeof(p7ioc_phb_compat));
	dt_property_string("device_type", "pciex");
	dt_property("reg", reg, sizeof(reg));
	dt_property_cell("#address-cells", 3);
	dt_property_cell("#size-cells", 2);
	dt_property_cell("#interrupt-cells", 1);
	dt_property_cells("bus-range", 2, 0, 0xff);
	//dt_property_cell("bus-width", 8); /* Figure it out from VPD ? */
	dt_property_cells("clock-frequency", 2, 0x400, 0); /* ??? */
	dt_property_cells("ibm,opal-phbid", 2, 0, p->phb.opal_id);
	dt_property_cell("interrupt-parent", get_ics_phandle());
	/* XXX FIXME: add phb own interrupts */
	/* XXX FIXME: add opal-memwin32, 64, dmawins, etc... */
	dt_property_cell("ibm,opal-msi-ports", 256);
	dt_property_cell("ibm,opal-num-pes", 128);
	dt_property_cells("ibm,opal-msi-ranges", 2, p->buid_msi << 4, 0x100);
	tkill = reg[0] + PHB_TCE_KILL;
	dt_property_cells("ibm,opal-tce-kill", 2, hi32(tkill), lo32(tkill));

	/* XXX FIXME: add slot-name */

	/* "ranges", we only expose IO and M32
	 *
	 * Note: The kernel expects us to have chopped of 64k from the
	 * M32 size (for the 32-bit MSIs). If we don't do that, it will
	 * get confused (OPAL does it)
	 */
	iob = cleanup_addr(p->io_base);
	m32b = cleanup_addr(p->m32_base + M32_PCI_START);
	dt_property_cells("ranges", 14,
			  /* IO space */
			  0x01000000, 0x00000000, 0x00000000,
			  hi32(iob), lo32(iob), 0, PHB_IO_SIZE,
			  /* M32 space */
			  0x02000000, 0x00000000, M32_PCI_START,
			  hi32(m32b), lo32(m32b), 0, M32_PCI_SIZE - 0x10000);

	/* The interrupt maps will be generated in the RC node by the
	 * PCI code based on the content of this structure:
	 */
	lsibase = p->buid_lsi << 4;
	lstate.int_size = 1;
	lstate.int_val[0][0] = lsibase + PHB_LSI_PCIE_INTA;
	lstate.int_val[1][0] = lsibase + PHB_LSI_PCIE_INTB;
	lstate.int_val[2][0] = lsibase + PHB_LSI_PCIE_INTC;
	lstate.int_val[3][0] = lsibase + PHB_LSI_PCIE_INTD;
	lstate.int_parent[0] = icsp;
	lstate.int_parent[1] = icsp;
	lstate.int_parent[2] = icsp;
	lstate.int_parent[3] = icsp;

	/* Add the child nodes */
	pci_add_nodes(&p->phb, &lstate);
	dt_end_node();
}

void p7ioc_phb_reset(struct p7ioc_phb *p)
{
	struct p7ioc *ioc = p->ioc;
	uint64_t ci_idx, rreg;
	unsigned int i;
	bool fenced;

	/* Check our fence status. The fence bits we care about are
	 * two bits per PHB at IBM bit location 14 and 15 + 4*phb
	 */
	fenced = p7ioc_phb_fenced(p);

	PHBDBG(p, "PHB reset... (fenced: %d)\n", (int)fenced);

	/*
	 * If not fenced and already functional, let's do an IODA reset
	 * to clear pending DMAs and wait a bit for thing to settle. It's
	 * notable that the IODA table cache won't be emptied so that we
	 * can restore them during error recovery.
	 */
	if (p->state == P7IOC_PHB_STATE_FUNCTIONAL && !fenced) {
		PHBDBG(p, "  ioda reset ...\n");
		p7ioc_ioda_reset(&p->phb, false);
		time_wait_ms(100);
	}

	/* CI port index */
	ci_idx = p->index + 2;

	/* Reset register bits for this PHB */
	rreg =  0;/*PPC_BIT(8 + ci_idx * 2);*/	/* CI port config reset */
	rreg |= PPC_BIT(9 + ci_idx * 2);	/* CI port func reset */
	rreg |= PPC_BIT(32 + p->index);		/* PHBn config reset */

	/* Mask various errors during reset and clear pending errors */
	out_be64(ioc->regs + P7IOC_CIn_LEM_ERR_MASK(ci_idx),
		 0xa4f4000000000000ul);
	out_be64(p->regs_asb + PHB_LEM_ERROR_MASK, 0xadb650c9808dd051ul);
	out_be64(ioc->regs + P7IOC_CIn_LEM_FIR(ci_idx), 0);

	/* We need to retry in case the fence doesn't lift due to a
	 * problem with lost credits (HW guys). How many times ?
	 */
#define MAX_PHB_RESET_RETRIES	5
	for (i = 0; i < MAX_PHB_RESET_RETRIES; i++) {
		PHBDBG(p, "  reset try %d...\n", i);
		/* Apply reset */
		out_be64(ioc->regs + P7IOC_CCRR, rreg);
		time_wait_ms(1);
		out_be64(ioc->regs + P7IOC_CCRR, 0);

		/* Check if fence lifed */
		fenced = p7ioc_phb_fenced(p);
		PHBDBG(p, "  fenced: %d...\n", (int)fenced);
		if (!fenced)
			break;
	}

	/* Reset failed, not much to do, maybe add an error return */
	if (fenced) {
		PHBERR(p, "Reset failed, fence still set !\n");
		p->state = P7IOC_PHB_STATE_BROKEN;
		return;
	}

	/* Wait a bit */
	time_wait_ms(100);

	/* Re-initialize the PHB */
	p7ioc_phb_init(p);

	/* Restore the CI error mask */
	out_be64(ioc->regs + P7IOC_CIn_LEM_ERR_MASK_AND(ci_idx), 0);
}

