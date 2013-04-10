/* (C) Copyright IBM Corp., 2013 and provided pursuant to the Technology
 * Licensing Agreement between Google Inc. and International Business
 * Machines Corporation, IBM License Reference Number AA130103030256 and
 * confidentiality governed by the Parties’ Mutual Nondisclosure Agreement
 * number V032404DR, executed by the parties on November 6, 2007, and
 * Supplement V032404DR-3 dated August 16, 2012 (the “NDA”). */
/*
 * Definitions relative to the P7 and P7+ GX controller
 */
#ifndef __GX_H
#define __GX_H

#include <bitutils.h>

/* P7 GX Mode 1 register (contains PSI BUID) */
#define GX_P7_MODE1_REG		0x0201180A
#define GX_P7_MODE1_PSI_BUID_MASK	PPC_BITMASK(18,26)
#define GX_P7_MODE1_PSI_BUID_LSH	PPC_BITLSHIFT(26)
#define GX_P7_MODE1_PSI_BUID_DISABLE	PPC_BIT(27)

/* P7+ GX Mode 4 register (PSI and NX BUIDs ) */
#define GX_P7P_MODE4_REG	0x02011811
#define GX_P7P_MODE4_ENABLE_NX_BUID	PPC_BIT(0)
#define GX_P7P_MODE4_NX_BUID_BASE_MASK	PPC_BITMASK(1,9)
#define GX_P7P_MODE4_NX_BUID_BASE_LSH	PPC_BITLSHIFT(9)
#define GX_P7P_MODE4_NX_BUID_MASK_MASK	PPC_BITMASK(10,18)
#define GX_P7P_MODE4_NX_BUID_MASK_LSH	PPC_BITLSHIFT(18)
#define GX_P7P_MODE4_PSI_BUID_MASK	PPC_BITMASK(19,27)
#define GX_P7P_MODE4_PSI_BUID_LSH	PPC_BITLSHIFT(27)
#define GX_P7P_MODE4_PSI_BUID_DISABLE	PPC_BIT(28)

/* P7 GX TCE BAR and mask */
#define GX_P7_GX0_TCE_BAR	0x02011845
#define GX_P7_TCE_BAR_ADDR_MASK		PPC_BITMASK(0,25)
#define GX_P7_TCE_BAR_ADDR_LSH		PPC_BITLSHIFT(25)
#define GX_P7_TCE_BAR_ADDR_SHIFT	PPC_BITLSHIFT(43)
#define GX_P7_TCE_BAR_ENABLE		PPC_BIT(26)
#define GX_P7_GX0_TCE_MASK	0x0201184B
#define GX_P7_TCE_MASK_MASK		PPC_BITMASK(0,25)
#define GX_P7_TCE_MASK_LSH		PPC_BITLSHIFT(25)
#define GX_P7_GX1_TCE_BAR	0x02011885
#define GX_P7_GX1_TCE_MASK	0x0201188B


extern int gx_configure_psi_buid(uint32_t chip, uint32_t buid);
extern int gx_configure_tce_bar(uint32_t chip, uint32_t gx, uint64_t addr,
				uint64_t size);

#endif /* __GX_H */
