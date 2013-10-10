/* (C) Copyright IBM Corp., 2013 and provided pursuant to the Technology
 * Licensing Agreement between Google Inc. and International Business
 * Machines Corporation, IBM License Reference Number AA130103030256 and
 * confidentiality governed by the Parties’ Mutual Nondisclosure Agreement
 * number V032404DR, executed by the parties on November 6, 2007, and
 * Supplement V032404DR-3 dated August 16, 2012 (the “NDA”). */
#ifndef __CHIP_H
#define __CHIP_H

#include <stdint.h>
#include <lock.h>

/*
 * Note on chip IDs:
 *
 * We carry a "chip_id" around, in the cpu_thread, but also as
 * ibm,chip-id properties.
 *
 * This ID is the HW fabric ID of a chip based on the XSCOM numbering,
 * also known as "GCID" (Global Chip ID).
 *
 * The format of this number is different between P7 and P8 and care must
 * be taken when trying to convert between this chip ID and some other
 * representation such as PIR values, interrupt-server numbers etc... :
 *
 * P7 GCID
 * -------
 *
 * Global chip ID is a 6 bit number:
 *
 *     NodeID    T   ChipID
 * |           |   |       |
 * |___|___|___|___|___|___|
 *
 * Where T is the "torrent" bit and is 0 for P7 chips and 1 for
 * directly XSCOM'able IO chips such as Torrent
 *
 * This macro converts a PIR to a GCID
 */
#define P7_PIR2GCID(pir) ({ 				\
	uint32_t _pir = pir;				\
	((_pir >> 4) & 0x38) | ((_pir >> 5) & 0x3); })

#define P7_PIR2COREID(pir) (((pir) >> 2) & 0x7)

#define P7_PIR2THREADID(pir) ((pir) & 0x3)

/*
 * P8 GCID
 * -------
 *
 * Global chip ID is a 6 bit number:
 *
 *     NodeID      ChipID
 * |           |           |
 * |___|___|___|___|___|___|
 *
 * The difference with P7 is the absence of T bit, the ChipID
 * is 3 bits long. The GCID is thus the same as the high bits
 * if the PIR
 */
#define P8_PIR2GCID(pir) (((pir) >> 7) & 0x3f)

#define P8_PIR2COREID(pir) (((pir) >> 3) & 0xf)

#define P8_PIR2THREADID(pir) ((pir) & 0x7)

struct dt_node;

/*
 * For each chip in the system, we maintain this structure
 *
 * This contains fields used by different modules including
 * modules in hw/ but is handy to keep per-chip data
 */
struct proc_chip {
	uint32_t	id;		/* HW Chip ID (GCID) */
	struct dt_node	*devnode;	/* "xscom" chip node */

	/* Those two values are only populated on machines with an FSP */
	uint32_t	dbob_id;	/* Drawer/Block/Octant/Blade (DBOBID) */
	uint32_t	pcid;		/* HDAT proc_chip_id */

	/* Used by hw/xscom.c */
	uint64_t	xscom_base;
	struct lock	xscom_lock;

	/* Used by hw/lpc.c */
	uint32_t	lpc_xbase;
	struct lock	lpc_lock;
};

extern uint32_t pir_to_chip_id(uint32_t pir);
extern uint32_t pir_to_core_id(uint32_t pir);
extern uint32_t pir_to_thread_id(uint32_t pir);

extern struct proc_chip *next_chip(struct proc_chip *chip);

#define for_each_chip(__c) for (__c=next_chip(NULL); __c; __c=next_chip(__c))

extern struct proc_chip *get_chip(uint32_t chip_id);

extern void init_chips(void);


#endif /* __CHIP_H */

