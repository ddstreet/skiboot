/* (C) Copyright IBM Corp., 2013 and provided pursuant to the Technology
 * Licensing Agreement between Google Inc. and International Business
 * Machines Corporation, IBM License Reference Number AA130103030256 and
 * confidentiality governed by the Parties’ Mutual Nondisclosure Agreement
 * number V032404DR, executed by the parties on November 6, 2007, and
 * Supplement V032404DR-3 dated August 16, 2012 (the “NDA”). */


#include <skiboot.h>
#include <xscom.h>
#include <io.h>
#include <cpu.h>

#define NX_P7_RNG_BAR		XSCOM_SAT(0x1, 0x2, 0x0c)
#define   NX_P7_RNG_BAR_ADDR_MASK	PPC_BITMASK(18, 51)
#define   NX_P7_RNG_BAR_ADDR_LSH	PPC_BITLSHIFT(51)
#define   NX_P7_RNG_BAR_SIZE_MASK	PPC_BITMASK(53, 55)
#define   NX_P7_RNG_BAR_SIZE_LSH	PPC_BITLSHIFT(55)
#define   NX_P7_RNG_BAR_ENABLE		PPC_BIT(52)

#define NX_P8_RNG_BAR		XSCOM_SAT(0xc, 0x2, 0x0d)
#define   NX_P8_RNG_BAR_ADDR_MASK	PPC_BITMASK(14, 51)
#define   NX_P8_RNG_BAR_ADDR_LSH	PPC_BITLSHIFT(51)
#define   NX_P8_RNG_BAR_SIZE_MASK	PPC_BITMASK(53, 55)
#define   NX_P8_RNG_BAR_SIZE_LSH	PPC_BITLSHIFT(55)
#define   NX_P8_RNG_BAR_ENABLE		PPC_BIT(52)

#define NX_P7_RNG_CFG		XSCOM_SAT(0x1, 0x2, 0x12)
#define   NX_P7_RNG_CFG_ENABLE		PPC_BIT(63)
#define NX_P8_RNG_CFG		XSCOM_SAT(0xc, 0x2, 0x12)
#define   NX_P8_RNG_CFG_ENABLE		PPC_BIT(63)

static void nx_create_node(struct dt_node *node)
{
	u64 bar, cfg;
	u64 xbar, xcfg;
	u32 pb_base;
	u32 gcid;
	u64 rng_addr, rng_len, len;
	struct dt_node *rng;
	int rc;

	gcid = dt_get_chip_id(node);
	pb_base = dt_get_address(node, 0, NULL);

	if (dt_node_is_compatible(node, "ibm,power7-nx")) {
		xbar = pb_base + NX_P7_RNG_BAR;
		xcfg = pb_base + NX_P7_RNG_CFG;
	} else if (dt_node_is_compatible(node, "ibm,power8-nx")) {
		xbar = pb_base + NX_P8_RNG_BAR;
		xcfg = pb_base + NX_P8_RNG_CFG;
	} else {
		prerror("NX%d: Unknown NX type!\n", gcid);
		return;
	}

	rc = xscom_read(gcid, xbar, &bar); /* Get RNG BAR */
	if (rc)
		return;	/* Hope xscom always prints error message */

	rc = xscom_read(gcid, xcfg, &cfg); /* Get RNG CFG */
	if (rc)
		return;

	/*
	 * We use the P8 BAR constants. The layout of the BAR is the
	 * same, with more bits at the top of P8 which are hard wired to
	 * 0 on P7. We also mask in-place rather than using GETFIELD
	 * for the base address as we happen to *know* that it's properly
	 * aligned in the register.
	 *
	 * FIXME? Always assusme BAR gets a valid address from FSP
	 */
	rng_addr = bar & NX_P8_RNG_BAR_ADDR_MASK;
	len  = GETFIELD(NX_P8_RNG_BAR_SIZE, bar);
	if (len > 4) {
		prerror("NX%d: Corrupted bar size %lld\n", gcid, len);
		return;
	}
	rng_len = (u64[]){  0x1000,         /* 4K */
			    0x10000,        /* 64K */
			    0x400000000,    /* 16G*/
			    0x100000,       /* 1M */
			    0x1000000       /* 16M */} [len];


	printf("NX%d: RNG BAR set to 0x%016llx..0x%016llx\n",
	       gcid, rng_addr, rng_addr + rng_len - 1);

	/* RNG must be enabled before MMIO is enabled */
	rc = xscom_write(gcid, xcfg, cfg | NX_P8_RNG_CFG_ENABLE);
	if (rc)
		return;

	/* The BAR needs to be enabled too */
	rc = xscom_write(gcid, xbar, bar | NX_P8_RNG_BAR_ENABLE);
	if (rc)
		return;
	rng = dt_new_addr(dt_root, "hwrng", rng_addr);
	dt_add_property_strings(rng, "compatible", "ibm,power-rng");
	dt_add_property_cells(rng, "reg", hi32(rng_addr), lo32(rng_addr),
			hi32(rng_len), lo32(rng_len));
	dt_add_property_cells(rng, "ibm,chip-id", gcid);
}

/* Create nodes for MMIO accesible components in NX (only RNG) */
void nx_init(void)
{
	struct dt_node *node;

	dt_for_each_compatible(dt_root, node, "ibm,power-nx")
		nx_create_node(node);
}
