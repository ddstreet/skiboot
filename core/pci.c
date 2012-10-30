#include <skiboot.h>
#include <pci.h>
#include <pci-cfg.h>
#include <time.h>
#include <lock.h>
#include <device_tree.h>

static struct lock pci_lock = LOCK_UNLOCKED;
#define PCI_MAX_PHBs	64
static struct phb *phbs[PCI_MAX_PHBs];

#define DBG(fmt...) do { } while(0)

/*
 * Generic PCI utilities
 */

/* pci_find_cap - Find a PCI capability in a device config space
 *
 * This will return a config space offset (positive) or a negative
 * error (OPAL error codes).
 *
 * OPAL_UNSUPPORTED is returned if the capability doesn't exist
 */
int64_t pci_find_cap(struct phb *phb, uint16_t bdfn, uint8_t want)
{
	int64_t rc;
	uint16_t stat, cap;
	uint8_t pos;

	rc = pci_cfg_read16(phb, bdfn, PCI_CFG_STAT, &stat);
	if (rc)
		return rc;
	if (!(stat & PCI_CFG_STAT_CAP))
		return OPAL_UNSUPPORTED;
	rc = pci_cfg_read8(phb, bdfn, PCI_CFG_CAP, &pos);
	if (rc)
		return rc;
	pos &= 0xfc;
	while(pos) {
		rc = pci_cfg_read16(phb, bdfn, pos, &cap);
		if (rc)
			return rc;
		if ((cap & 0xff) == want)
			return pos;
		pos = (cap >> 8) & 0xfc;
	}
	return OPAL_UNSUPPORTED;
}

/* pci_find_ecap - Find a PCIe extended capability in a device
 *                 config space
 *
 * This will return a config space offset (positive) or a negative
 * error (OPAL error code). Additionally, if the "version" argument
 * is non-NULL, the capability version will be returned there.
 *
 * OPAL_UNSUPPORTED is returned if the capability doesn't exist
 */
int64_t pci_find_ecap(struct phb *phb, uint16_t bdfn, uint16_t want,
		      uint8_t *version)
{
	int64_t rc;
	uint32_t cap;
	uint16_t off;

	for (off = 0x100; off && off < 0x1000; off = (cap >> 20) & 0xffc ) {
		rc = pci_cfg_read32(phb, bdfn, off, &cap);
		if (rc)
			return rc;
		if ((cap & 0xffff) == want) {
			if (version)
				*version = (cap >> 16) & 0xf;
			return off;
		}
	}
	return OPAL_UNSUPPORTED;
}

static struct pci_device *pci_scan_one(struct phb *phb, uint16_t bdfn)
{
	struct pci_device *pd = NULL;
	uint32_t retries, vdid;
	int64_t rc, ecap;
	uint8_t htype;
	uint16_t capreg;
	bool had_crs = false;

	for (retries = 40; retries; retries--) {
		rc = pci_cfg_read32(phb, bdfn, 0, &vdid);
		if (rc)
			return NULL;
		if (vdid == 0xffffffff || vdid == 0x00000000)
			return NULL;
		if (vdid != 0xffff0001)
			break;
		had_crs = true;
		time_wait_ms(100);
	}
	if (vdid == 0xffff0001) {
		prerror("PCI: Device %04x CRS timeout !\n", bdfn);
		return NULL;
	}
	if (had_crs)
		printf("PCI: Device %04x replied after CRS\n", bdfn);
	pd = zalloc(sizeof(struct pci_device));
	if (!pd) {
		prerror("PCI: Failed to allocate structure pci_device !\n");
		goto fail;
	}
	pd->bdfn = bdfn;
	list_head_init(&pd->children);
	rc = pci_cfg_read8(phb, bdfn, PCI_CFG_HDR_TYPE, &htype);
	if (rc) {
		prerror("PCI: Failed to read header type !\n");
		goto fail;
	}
	pd->is_multifunction = !!(htype & 0x80);
	pd->is_bridge = (htype & 0x7f) != 0;
	pd->scan_map = 0xffff; /* Default */

	ecap = pci_find_cap(phb, bdfn, PCI_CFG_CAP_ID_EXP);
	if (ecap > 0) {
		pd->is_pcie = true;
		pci_cfg_read16(phb, bdfn, ecap + PCICAP_EXP_CAPABILITY_REG,
			       &capreg);
		pd->dev_type = GETFIELD(PCICAP_EXP_CAP_TYPE, capreg);
		/* XXX Handle ARI */
		if (pd->dev_type == PCIE_TYPE_SWITCH_DNPORT ||
		    pd->dev_type == PCIE_TYPE_ROOT_PORT)
			pd->scan_map = 0x1;
	} else {
		pd->is_pcie = false;
		pd->dev_type = PCIE_TYPE_LEGACY;
	}

	/* If it's a bridge, sanitize the bus numbers to avoid forwarding
	 *
	 * This will help when walking down those bridges later on
	 */
	if (pd->is_bridge) {
		pci_cfg_write8(phb, bdfn, PCI_CFG_PRIMARY_BUS, bdfn >> 8);
		pci_cfg_write8(phb, bdfn, PCI_CFG_SECONDARY_BUS, 0);
		pci_cfg_write8(phb, bdfn, PCI_CFG_SUBORDINATE_BUS, 0);
	}

	/* XXX Need to do some basic setups, such as MPSS, MRS,
	 * RCB, etc...
	 */

	printf("PCI: Device %04x VID:%04x DEV:%04x MF:%d BR:%d EX:%d TYP:%d\n",
	       bdfn, vdid & 0xffff, vdid >> 16, pd->is_multifunction,
	       pd->is_bridge, pd->is_pcie, pd->dev_type);

	return pd;
 fail:
	if (pd)
		free(pd);
	return NULL;
}

/* pci_check_clear_freeze - Probing empty slot will result in an EEH
 *                          freeze. Currently we have a single PE mapping
 *                          everything (default state of our backend) so
 *                          we just check and clear the state of PE#0
 *
 * NOTE: We currently only habdle simple PE freeze, not PHB fencing
 *       (or rather our backend does)
 */
static void pci_check_clear_freeze(struct phb *phb)
{
	int64_t rc;
	uint8_t freeze_state;
	uint16_t pci_error_type;

	rc = phb->ops->eeh_freeze_status(phb, 0, &freeze_state,
					 &pci_error_type, NULL);
	if (rc)
		return;
	if (freeze_state == OPAL_EEH_STOPPED_NOT_FROZEN)
		return;
	phb->ops->eeh_freeze_clear(phb, 0, OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);
}

/* pci_enable_bridge - Called before scanning a bridge
 *
 * Ensures error flags are clean, disable master abort, and
 * check if the subordinate bus isn't reset, the slot is enabled
 * on PCIe, etc...
 */
static bool pci_enable_bridge(struct phb *phb, struct pci_device *pd)
{
	uint16_t bctl;
	bool was_reset = false;
	int64_t ecap = 0;

	/* Disable master aborts, clear errors */
	pci_cfg_read16(phb, pd->bdfn, PCI_CFG_BRCTL, &bctl);
	bctl &= ~PCI_CFG_BRCTL_MABORT_REPORT;
	pci_cfg_write16(phb, pd->bdfn, PCI_CFG_BRCTL, bctl);

	/* PCI-E bridge, check the slot state */
	if (pd->dev_type == PCIE_TYPE_ROOT_PORT ||
	    pd->dev_type == PCIE_TYPE_SWITCH_DNPORT) {
		uint16_t slctl, slcap, slsta, lctl;

		ecap = pci_find_cap(phb, pd->bdfn, PCI_CFG_CAP_ID_EXP);

		/* Read the slot status & check for presence detect */
		pci_cfg_read16(phb, pd->bdfn, ecap+PCICAP_EXP_SLOTSTAT, &slsta);
		DBG(" slstat=%04x\n", slsta);
		if (!(slsta & PCICAP_EXP_SLOTSTAT_PDETECTST)) {
			printf("PCI: No card in slot\n");
			return false;
		}
		
		/* Read the slot capabilities */
		pci_cfg_read16(phb, pd->bdfn, ecap+PCICAP_EXP_SLOTCAP, &slcap);
		DBG(" slcap=%04x\n", slcap);
		if (!(slcap & PCICAP_EXP_SLOTCAP_PWCTRL))
			goto power_is_on;

		/* Read the slot control register, check if the slot is off */
		pci_cfg_read16(phb, pd->bdfn, ecap+PCICAP_EXP_SLOTCTL, &slctl);
		DBG(" slctl=%04x\n", slctl);
		if (!(slctl & PCICAP_EXP_SLOTCTL_PWRCTLR))
			goto power_is_on;

		/* Turn power on
		 *
		 * XXX This is a "command", we should wait for it to complete
		 * etc... but just waiting 2s will do for now
		 */
		DBG("PCI: Bridge power is off, turning on ...\n");
		slctl &= ~PCICAP_EXP_SLOTCTL_PWRCTLR;
		slctl |= SETFIELD(PCICAP_EXP_SLOTCTL_PWRI, 0, PCIE_INDIC_ON);
		pci_cfg_write16(phb, pd->bdfn, ecap+PCICAP_EXP_SLOTCTL, slctl);

		/* Wait a couple of seconds */
		time_wait_ms(2000);

 power_is_on:
		/* Enable link */
		pci_cfg_read16(phb, pd->bdfn, ecap+PCICAP_EXP_LCTL, &lctl);
		DBG(" lctl=%04x\n", lctl);
		lctl &= ~PCICAP_EXP_LCTL_LINK_DIS;
		pci_cfg_write16(phb, pd->bdfn, ecap+PCICAP_EXP_LCTL, lctl);
	}

	/* Clear secondary reset */
	if (bctl & PCI_CFG_BRCTL_SECONDARY_RESET) {
		printf("PCI: Bridge secondary reset is on, clearing it ...\n");
		bctl &= ~PCI_CFG_BRCTL_SECONDARY_RESET;
		pci_cfg_write16(phb, pd->bdfn, PCI_CFG_BRCTL, bctl);
		time_wait_ms(1000);
		was_reset = true;
	}

	/* PCI-E bridge, wait for link */
	if (pd->dev_type == PCIE_TYPE_ROOT_PORT ||
	    pd->dev_type == PCIE_TYPE_SWITCH_DNPORT) {
		uint32_t lcap;

		/* Read link caps */
		pci_cfg_read32(phb, pd->bdfn, ecap+PCICAP_EXP_LCAP, &lcap);

		/* Did link capability say we got reporting ?
		 *
		 * If yes, wait up to 10s, if not, wait 1s if we didn't already
		 */
		if (lcap & PCICAP_EXP_LCAP_DL_ACT_REP) {
			uint32_t retries = 100;
			uint16_t lstat;

			printf("%016lx: waiting for link... \n", mftb());

			while(retries--) {
				pci_cfg_read16(phb, pd->bdfn,
					       ecap+PCICAP_EXP_LSTAT, &lstat);
				if (lstat & PCICAP_EXP_LSTAT_DLLL_ACT)
					break;
				time_wait_ms(100);
			}
			printf("%016lx: end wait for link...\n", mftb());
			if (!(lstat & PCICAP_EXP_LSTAT_DLLL_ACT)) {
				prerror("PCI: Bridge %04x, timeout waiting"
					" for downstream link\n", pd->bdfn);
				return false;
			}
			/* Need to wait another 100ms before touching
			 * the config space
			 */
			time_wait_ms(100);
		} else if (!was_reset)
			time_wait_ms(1000);
	}

	/* Clear error status */
	pci_cfg_write16(phb, pd->bdfn, PCI_CFG_STAT, 0xffff);

	return true;
}

/* Clear up bridge resources */
static void pci_cleanup_bridge(struct phb *phb, struct pci_device *pd)
{
	uint16_t cmd;

	pci_cfg_write16(phb, pd->bdfn, PCI_CFG_IO_BASE_U16, 0xffff);
	pci_cfg_write8(phb, pd->bdfn, PCI_CFG_IO_BASE, 0xf0);
	pci_cfg_write16(phb, pd->bdfn, PCI_CFG_IO_LIMIT_U16, 0);
	pci_cfg_write8(phb, pd->bdfn, PCI_CFG_IO_LIMIT, 0);
	pci_cfg_write16(phb, pd->bdfn, PCI_CFG_MEM_BASE, 0xfff0);
	pci_cfg_write16(phb, pd->bdfn, PCI_CFG_MEM_LIMIT, 0);
	pci_cfg_write32(phb, pd->bdfn, PCI_CFG_PREF_MEM_BASE_U32, 0xffffffff);
	pci_cfg_write16(phb, pd->bdfn, PCI_CFG_PREF_MEM_BASE, 0xfff0);
	pci_cfg_write32(phb, pd->bdfn, PCI_CFG_PREF_MEM_LIMIT_U32, 0);
	pci_cfg_write16(phb, pd->bdfn, PCI_CFG_PREF_MEM_LIMIT, 0);

	/* Note: This is a bit fishy but since we have closed all the
	 * bridge windows above, it shouldn't be a problem. Basically
	 * we enable Memory, IO and Bus Master on the bridge because
	 * some versions of Linux will fail to do it themselves.
	 */
	pci_cfg_read16(phb, pd->bdfn, PCI_CFG_CMD, &cmd);
	cmd |= PCI_CFG_CMD_IO_EN | PCI_CFG_CMD_MEM_EN;
	cmd |= PCI_CFG_CMD_BUS_MASTER_EN;
	pci_cfg_write16(phb, pd->bdfn, PCI_CFG_CMD, cmd);	
}


/* pci_scan - Perform a recursive scan of the bus at bus_number
 *            populating the list passed as an argument. This also
 *            performs the bus numbering, so it returns the largest
 *            bus number that was assigned.
 *
 * Note: Eventually this might want to access some VPD information
 *       in order to know what slots to scan and what not etc..
 *
 * XXX NOTE: We might want to enable ARI along the way...
 *
 * XXX NOTE: We might also want to setup the PCIe MPS/MRSS properly
 *           here as Linux may or may not do it
 */
static uint8_t pci_scan(struct phb *phb, uint8_t bus, uint8_t max_bus,
			struct list_head *list, struct pci_device *parent)
{
	struct pci_device *pd;
	uint8_t dev, fn, next_bus, max_sub, save_max;
	uint16_t scan_map;

	/* Decide what to scan  */
	scan_map = parent ? parent->scan_map : phb->scan_map;

	/* Do scan */
	for (dev = 0; dev < 32; dev++) {
		if (!(scan_map & (1ul << dev)))
			continue;

		/* Scan the device */
		pd = pci_scan_one(phb, (bus << 8) | (dev << 3));
		pci_check_clear_freeze(phb);
		if (!pd)
			continue;

		/* Link it up */
		list_add_tail(list, &pd->link);

		/* XXX Handle ARI */
		if (!pd->is_multifunction)
			continue;
		for (fn = 1; fn < 8; fn++) {
			pd = pci_scan_one(phb, (bus << 8) | (dev << 3) | fn);
			pci_check_clear_freeze(phb);
			if (pd)
				list_add_tail(list, &pd->link);
		}
	}

	next_bus = bus + 1;
	max_sub = bus;
	save_max = max_bus;

	/* Scan down bridges */
	list_for_each(list, pd, link) {
		bool use_max, do_scan;

		if (!pd->is_bridge)
			continue;

		/* We need to figure out a new bus number to start from.
		 *
		 * This can be tricky due to our HW constraints which differ
		 * from bridge to bridge so we are going to let the phb
		 * driver decide what to do. This can return us a maxium
		 * bus number to assign as well
		 *
		 * This function will:
		 *
		 *  - Return the bus number to use as secondary for the
		 *    bridge or 0 for a failure
		 *
		 *  - "max_bus" will be adjusted to represent the max
		 *    subordinate that can be associated with the downstream
		 *    device
		 *
		 *  - "use_max" will be set to true if the returned max_bus
		 *    *must* be used as the subordinate bus number of that
		 *    bridge (when we need to give aligned powers of two's
		 *    on P7IOC). If is is set to false, we just adjust the
		 *    subordinate bus number based on what we probed.
		 *    
		 */
		max_bus = save_max;
		next_bus = phb->ops->choose_bus(phb, pd, next_bus,
						&max_bus, &use_max);

		/* Configure the bridge with the returned values */
		if (next_bus <= bus) {
			printf("PCI: Bridge %04x, out of bus numbers !\n",
			       pd->bdfn);
			max_bus = next_bus = 0; /* Failure case */
		}
		pci_cfg_write8(phb, pd->bdfn, PCI_CFG_SECONDARY_BUS, next_bus);
		pci_cfg_write8(phb, pd->bdfn, PCI_CFG_SUBORDINATE_BUS, max_bus);
		if (!next_bus)
			break;

		printf("PCI: Bridge %04x, bus: %02x..%02x %s scanning...\n",
		       pd->bdfn, next_bus, max_bus, use_max ? "[use max]" : "");

		/* Clear up bridge resources */
		pci_cleanup_bridge(phb, pd);

		/* Configure the bridge. This will enable power to the slot
		 * if it's currently disabled, lift reset, etc...
		 *
		 * Return false if we know there's nothing behind the bridge
		 */
		do_scan = pci_enable_bridge(phb, pd);

		/* Perform recursive scan */
		if (do_scan) {
			max_sub = pci_scan(phb, next_bus, max_bus,
					   &pd->children, pd);
		} else if (!use_max) {
			/* XXX Empty bridge... we leave room for hotplug
			 * slots etc.. but we should be smarter at figuring
			 * out if this is actually a hotpluggable one
			 */
			max_sub = next_bus + 4;
			if (max_sub > max_bus)
				max_sub = max_bus;
		}

		/* Update the max subordinate as described previously */
		if (use_max)
			max_sub = max_bus;
		pci_cfg_write8(phb, pd->bdfn, PCI_CFG_SUBORDINATE_BUS, max_sub);
		next_bus = max_sub + 1;
	}

	return max_sub;
}

static void pci_init_slot(struct phb *phb)
{
	int64_t rc;

	printf("PHB%d: Init slot\n", phb->opal_id);

	/* Check if the PHB has anything connected to it */
	rc = phb->ops->presence_detect(phb);
	if (rc != OPAL_SHPC_DEV_PRESENT) {
		printf("PHB%d: Slot empty\n", phb->opal_id);
		return;
	}

	/* Power it up */
	rc = phb->ops->slot_power_on(phb);
	if (rc < 0) {
		printf("PHB%d: Slot power on failed, rc=%lld\n",
		       phb->opal_id, rc);
		return;
	}
	while(rc > 0) {
		time_wait(rc);
		rc = phb->ops->poll(phb);
	}
	if (rc < 0) {
		printf("PHB%d: Slot power on failed, rc=%lld\n",
		       phb->opal_id, rc);
		return;
	}

	/* It's up, print some things */
	rc = phb->ops->link_state(phb);
	if (rc < 0) {
		printf("PHB%d: Failed to query link state, rc=%lld\n",
		       phb->opal_id, rc);
		return;
	}
	if (phb->phb_type >= phb_type_pcie_v1)
		printf("PHB%d: Link up at x%lld width\n", phb->opal_id, rc);
	printf("PHB%d: Scanning...\n", phb->opal_id);

	pci_scan(phb, 0, 0xff, &phb->devices, NULL);
}

int64_t pci_register_phb(struct phb *phb)
{
	int64_t rc = OPAL_SUCCESS;
	unsigned int i;

	lock(&pci_lock);
	for (i = 0; i < PCI_MAX_PHBs; i++)
		if (!phbs[i])
			break;
	if (i >= PCI_MAX_PHBs) {
		prerror("PHB: Failed to find a free ID slot\n");
		rc = OPAL_RESOURCE;
	} else {
		phbs[i] = phb;
		phb->opal_id = i;
	}
	list_head_init(&phb->devices);
	unlock(&pci_lock);

	return rc;
}

int64_t pci_unregister_phb(struct phb *phb)
{
	/* XXX We want some kind of RCU or RWlock to make things
	 * like that happen while no OPAL callback is in progress,
	 * that way we avoid taking a lock in each of them.
	 *
	 * Right now we don't unregister so we are fine
	 */
	lock(&pci_lock);
	phbs[phb->opal_id] = phb;
	unlock(&pci_lock);

	return OPAL_SUCCESS;
}

struct phb *pci_get_phb(uint64_t phb_id)
{
	if (phb_id >= PCI_MAX_PHBs)
		return NULL;

	/* XXX See comment in pci_unregister_phb() about locking etc... */
	return phbs[phb_id];
}

void pci_init_slots(void)
{
	unsigned int i;

	printf("PCI: Initializing PHB slots...\n");

	lock(&pci_lock);

	/* XXX Do those in parallel (at least the power up
	 * state machine could be done in parallel)
	 */
	for (i = 0; i < PCI_MAX_PHBs; i++) {
		if (!phbs[i])
			continue;
		pci_init_slot(phbs[i]);
	}
	unlock(&pci_lock);
}

static const char *pci_class_name(uint32_t class_code)
{
	uint8_t class = class_code >> 16;
	uint8_t sub = (class_code >> 8) & 0xff;
	uint8_t pif = class_code & 0xff;

	switch(class) {
	case 0x00:
		switch(sub) {
		case 0x00: return "device";
		case 0x01: return "vga";
		}
		break;
	case 0x01:
		switch(sub) {
		case 0x00: return "scsi";
		case 0x01: return "ide";
		case 0x02: return "fdc";
		case 0x03: return "ipi";
		case 0x04: return "raid";
		case 0x05: return "ata";
		case 0x06: return "sata";
		case 0x07: return "sas";
		default:   return "mass-storage";
		}
	case 0x02:
		switch(sub) {
		case 0x00: return "ethernet";
		case 0x01: return "token-ring";
		case 0x02: return "fddi";
		case 0x03: return "atm";
		case 0x04: return "isdn";
		case 0x05: return "worldfip";
		case 0x06: return "picmg";
		default:   return "network";
		}
	case 0x03:
		switch(sub) {
		case 0x00: return "vga";
		case 0x01: return "xga";
		case 0x02: return "3d-controller";
		default:   return "display";
		}
	case 0x04:
		switch(sub) {
		case 0x00: return "video";
		case 0x01: return "sound";
		case 0x02: return "telephony";
		default:   return "multimedia-device";
		}
	case 0x05:
		switch(sub) {
		case 0x00: return "memory";
		case 0x01: return "flash";
		default:   return "memory-controller";
		}
	case 0x06:
		switch(sub) {
		case 0x00: return "host";
		case 0x01: return "isa";
		case 0x02: return "eisa";
		case 0x03: return "mca";
		case 0x04: return "pci";
		case 0x05: return "pcmcia";
		case 0x06: return "nubus";
		case 0x07: return "cardbus";
		case 0x08: return "raceway";
		case 0x09: return "semi-transparent-pci";
		case 0x0a: return "infiniband";
		default:   return "unknown-bridge";
		}
	case 0x07:
		switch(sub) {
		case 0x00:
			switch(pif) {
			case 0x01: return "16450-serial";
			case 0x02: return "16550-serial";
			case 0x03: return "16650-serial";
			case 0x04: return "16750-serial";
			case 0x05: return "16850-serial";
			case 0x06: return "16950-serial";
			default:   return "serial";
			}
		case 0x01:
			switch(pif) {
			case 0x01: return "bi-directional-parallel";
			case 0x02: return "ecp-1.x-parallel";
			case 0x03: return "ieee1284-controller";
			case 0xfe: return "ieee1284-device";
			default:   return "parallel";
			}
		case 0x02: return "multiport-serial";
		case 0x03:
			switch(pif) {
			case 0x01: return "16450-modem";
			case 0x02: return "16550-modem";
			case 0x03: return "16650-modem";
			case 0x04: return "16750-modem";
			default:   return "modem";
			}
		case 0x04: return "gpib";
		case 0x05: return "smart-card";
		default:   return "communication-controller";
		}
	case 0x08:
		switch(sub) {
		case 0x00:
			switch(pif) {
			case 0x01: return "isa-pic";
			case 0x02: return "eisa-pic";
			case 0x10: return "io-apic";
			case 0x20: return "iox-apic";
			default:   return "interrupt-controller";
			}
		case 0x01:
			switch(pif) {
			case 0x01: return "isa-dma";
			case 0x02: return "eisa-dma";
			default:   return "dma-controller";
			}
		case 0x02:
			switch(pif) {
			case 0x01: return "isa-system-timer";
			case 0x02: return "eisa-system-timer";
			default:   return "timer";
			}
		case 0x03:
			switch(pif) {
			case 0x01: return "isa-rtc";
			default:   return "rtc";
			}
		case 0x04: return "hotplug-controller";
		case 0x05: return "sd-host-controller";
		default:   return "system-peripheral";
		}
	case 0x09:
		switch(sub) {
		case 0x00: return "keyboard";
		case 0x01: return "pen";
		case 0x02: return "mouse";
		case 0x03: return "scanner";
		case 0x04: return "gameport";
		default:   return "input-controller";
		}
	case 0x0a:
		switch(sub) {
		case 0x00: return "clock";
		default:   return "docking-station";
		}
	case 0x0b:
		switch(sub) {
		case 0x00: return "386";
		case 0x01: return "486";
		case 0x02: return "pentium";
		case 0x10: return "alpha";
		case 0x20: return "powerpc";
		case 0x30: return "mips";
		case 0x40: return "co-processor";
		default:   return "cpu";
		}
	case 0x0c:
		switch(sub) {
		case 0x00: return "firewire";
		case 0x01: return "access-bus";
		case 0x02: return "ssa";
		case 0x03:
			switch(pif) {
			case 0x00: return "usb-uhci";
			case 0x10: return "usb-ohci";
			case 0x20: return "usb-ehci";
			case 0x30: return "usb-xhci";
			case 0xfe: return "usb-device";
			default:   return "usb";
			}
		case 0x04: return "fibre-channel";
		case 0x05: return "smb";
		case 0x06: return "infiniband";
		case 0x07:
			switch(pif) {
			case 0x00: return "impi-smic";
			case 0x01: return "impi-kbrd";
			case 0x02: return "impi-bltr";
			default:   return "impi";
			}
		case 0x08: return "secos";
		case 0x09: return "canbus";
		default:   return "serial-bus";
		}
	case 0x0d:
		switch(sub) {
		case 0x00: return "irda";
		case 0x01: return "consumer-ir";
		case 0x10: return "rf-controller";
		case 0x11: return "bluetooth";
		case 0x12: return "broadband";
		case 0x20: return "enet-802.11a";
		case 0x21: return "enet-802.11b";
		default:   return "wireless-controller";
		}
	case 0x0e: return "intelligent-controller";
	case 0x0f:
		switch(sub) {
		case 0x01: return "satellite-tv";
		case 0x02: return "satellite-audio";
		case 0x03: return "satellite-voice";
		case 0x04: return "satellite-data";
		default:   return "satellite-device";
		}
	case 0x10:
		switch(sub) {
		case 0x00: return "network-encryption";
		case 0x01: return "entertainment-encryption";
		default:   return "encryption";
		}
	case 0x011:
		switch(sub) {
		case 0x00: return "dpio";
		case 0x01: return "counter";
		case 0x10: return "measurement";
		case 0x20: return "management-card";
		default:   return "data-processing";
		}
	}
	return "device";
}

void pci_std_swizzle_irq_map(struct pci_device *pd,
			     struct pci_lsi_state *lstate,
			     uint8_t swizzle)
{
	uint32_t *map, *p;
	int dev, irq;
	size_t map_size;

	/* Size in bytes of a target interrupt */
	size_t isize = lstate->int_size * sizeof(uint32_t);

	/* Calculate the sixe of a map entry:
	 *
	 * 3 cells : PCI Address
	 * 1 cell  : PCI IRQ
	 * 1 cell  : PIC phandle
	 * n cells : PIC irq (n = lstate->int_size)
	 *
	 * Assumption: PIC address is 0-size
	 */
	int esize = 3 + 1 + 1 + lstate->int_size;

	/* Number of map "device" entries
	 *
	 * A PCI Express root or downstream port needs only one
	 * entry for device 0. Anything else will get a full map
	 * for all possible 32 child device numbers
	 *
	 * If we have been passed a host bridge (pd == NULL) we also
	 * do a simple per-pin map
	 */
	int edevcount;

	if (!pd || (pd->dev_type == PCIE_TYPE_ROOT_PORT ||
		    pd->dev_type == PCIE_TYPE_SWITCH_DNPORT)) {
		edevcount = 1;
		dt_property_cells("interrupt-map-mask", 4, 0, 0, 0, 7);
	} else {
		edevcount = 32;
		dt_property_cells("interrupt-map-mask", 4, 0xf800, 0, 0, 7);
	}
	map_size = esize * edevcount * 4 * sizeof(uint32_t);
	map = p = zalloc(map_size);

	for (dev = 0; dev < edevcount; dev++) {
		for (irq = 0; irq < 4; irq++) {
			/* Calculate pin */
			uint32_t new_irq = (irq + dev + swizzle) % 4;

			/* PCI address portion */
			*(p++) = dev << (8 + 3);
			*(p++) = 0;
			*(p++) = 0;

			/* PCI interrupt portion */
			*(p++) = irq + 1;

			/* Parent phandle */
			*(p++) = lstate->int_parent[new_irq];

			/* Parent desc */
			memcpy(p, lstate->int_val[new_irq], isize);
			p += lstate->int_size;
		}
	}

	dt_property("interrupt-map", map, map_size);
	free(map);
}

static void pci_add_one_node(struct phb *phb, struct pci_device *pd,
			     struct pci_lsi_state *lstate, uint8_t swizzle)
{
	struct pci_device *child;
	const char *cname;
#define MAX_NAME 256
	char name[MAX_NAME];
	char compat[MAX_NAME];
	uint32_t rev_class, vdid;
	uint32_t reg[5];
	uint8_t intpin;

	pci_cfg_read32(phb, pd->bdfn, 0, &vdid);
	pci_cfg_read32(phb, pd->bdfn, PCI_CFG_REV_ID, &rev_class);
	pci_cfg_read8(phb, pd->bdfn, PCI_CFG_INT_PIN, &intpin);

	/* Note: Special class name quirk for IBM bridge bogus class
	 * without that, Linux will fail probing things properly.
	 */
	if (vdid == 0x03b91014)
		rev_class = (rev_class & 0xff) | 0x6040000;
	cname = pci_class_name(rev_class >> 8);

	if (pd->bdfn & 0x7)
		snprintf(name, MAX_NAME - 1, "%s@%x,%x",
			 cname, (pd->bdfn >> 3) & 0x1f, pd->bdfn & 0x7);
	else
		snprintf(name, MAX_NAME - 1, "%s@%x",
			 cname, (pd->bdfn >> 3) & 0x1f);
	dt_begin_node(name);

	/* XXX FIXME: make proper "compatible" properties */
	if (pd->is_pcie) {
		snprintf(compat, MAX_NAME, "pciex%x,%x",
			 vdid & 0xffff, vdid >> 16);
		dt_property_cell("ibm,pci-config-space-type", 1);
	} else {
		snprintf(compat, MAX_NAME, "pci%x,%x",
			 vdid & 0xffff, vdid >> 16);
		dt_property_cell("ibm,pci-config-space-type", 0);
	}
	dt_property_cell("class-code", rev_class >> 8);
	dt_property_cell("revision-id", rev_class & 0xff);
	dt_property_cell("vendor-id", vdid & 0xffff);
	dt_property_cell("device-id", vdid >> 16);
	if (intpin)
		dt_property_cell("interrupts", intpin);

	/* XXX FIXME: Add a few missing ones such as
	 *
	 *  - devsel-speed (!express)
	 *  - max-latency
	 *  - min-grant
	 *  - subsystem-id
	 *  - subsystem-vendor-id
	 *  - ...
	 */

	/* XXX FIXME: We don't look for BARs, we only put the config space
	 * entry in the "reg" property. That's enough for Linux and we might
	 * even want to make this legit in future ePAPR
	 */
	reg[0] = pd->bdfn << 8;
	reg[1] = reg[2] = reg[3] = reg[4] = 0;
	dt_property("reg", reg, sizeof(reg));

	if (!pd->is_bridge)
		goto terminate;

	dt_property_cell("#address-cells", 3);
	dt_property_cell("#size-cells", 2);
	dt_property_cell("#interrupt-cells", 1);

	/* We want "device_type" for bridges */
	if (pd->is_pcie)
		dt_property_string("device_type", "pciex");
	else
		dt_property_string("device_type", "pci");

	/* Update the current interrupt swizzling level based on our own
	 * device number
	 */
	swizzle = (swizzle  + ((pd->bdfn >> 3) & 0x1f)) & 3;

	/* We generate a standard-swizzling interrupt map. This is pretty
	 * big, we *could* try to be smarter for things that aren't hotplug
	 * slots at least and only populate those entries for which there's
	 * an actual children (especially on PCI Express), but for now that
	 * will do
	 */
	pci_std_swizzle_irq_map(pd, lstate, swizzle);

	/* We do an empty ranges property for now, we haven't setup any
	 * bridge windows, the kernel will deal with that
	 */
	dt_property("ranges", NULL, 0);

	list_for_each(&pd->children, child, link)
		pci_add_one_node(phb, child, lstate, swizzle);

 terminate:
	dt_end_node();
}

void pci_add_nodes(struct phb *phb, struct pci_lsi_state *lstate)
{
	struct pci_device *pd;

	list_for_each(&phb->devices, pd, link)
		pci_add_one_node(phb, pd, lstate, 0);
}

