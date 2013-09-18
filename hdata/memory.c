/* (C) Copyright IBM Corp., 2013 and provided pursuant to the Technology
 * Licensing Agreement between Google Inc. and International Business
 * Machines Corporation, IBM License Reference Number AA130103030256 and
 * confidentiality governed by the Parties’ Mutual Nondisclosure Agreement
 * number V032404DR, executed by the parties on November 6, 2007, and
 * Supplement V032404DR-3 dated August 16, 2012 (the “NDA”). */
#include <memory.h>
#include <cpu.h>
#include <device_tree.h>
#include <device.h>
#include <ccan/str/str.h>
#include <libfdt/libfdt.h>
#include <types.h>

#include "spira.h"
#include "hdata.h"

struct HDIF_ram_area_id {
	__be16 id;
#define RAM_AREA_INSTALLED	0x8000
#define RAM_AREA_FUNCTIONAL	0x4000
	__be16 flags;
};

struct HDIF_ram_area_size {
	__be64 mb;
};

struct ram_area {
	const struct HDIF_ram_area_id *raid;
	const struct HDIF_ram_area_size *rasize;
};

struct HDIF_ms_area_address_range {
	__be64 start;
	__be64 end;
	__be32 chip;
	__be32 mirror_attr;
	__be64 mirror_start;
};

struct HDIF_ms_area_id {
	__be16 id;
	__be16 parent_type;
#define MS_AREA_INSTALLED	0x8000
#define MS_AREA_FUNCTIONAL	0x4000
#define MS_AREA_SHARED		0x2000
	__be16 flags;
	__be16 share_id;
};

static struct dt_node *find_shared(struct dt_node *root, u16 id, u64 start, u64 len)
{
	struct dt_node *i;

	for (i = dt_first(root); i; i = dt_next(root, i)) {
		__be64 reg[2];
		const struct dt_property *shared, *type;

		type = dt_find_property(i, "device_type");
		if (!type || strcmp(type->prop, "memory") != 0)
			continue;

		shared = dt_find_property(i, DT_PRIVATE "share-id");
		if (!shared || fdt32_to_cpu(*(u32 *)shared->prop) != id)
			continue;

		memcpy(reg, dt_find_property(i, "reg")->prop, sizeof(reg));
		if (be64_to_cpu(reg[0]) == start && be64_to_cpu(reg[1]) == len)
			break;
	}
	return i;
}

static void append_chip_id(struct dt_node *mem, u32 id)
{
	struct dt_property *prop;
	size_t len, i;
	u32 *p;

	prop = __dt_find_property(mem, "ibm,chip-id");
	if (!prop)
		return;
	len = prop->len >> 2;
	p = (u32 *)prop->prop;

	/* Check if it exists already */
	for (i = 0; i < len; i++) {
		if (be32_to_cpu(p[i]) == id)
			return;
	}

	/* Add it to the list */
	dt_resize_property(prop, (len + 1) << 2);
	p = (u32 *)prop->prop;
	p[len] = cpu_to_be32(id);
}

static bool add_address_range(struct dt_node *root,
			      const struct HDIF_ms_area_id *id,
			      const struct HDIF_ms_area_address_range *arange)
{
	struct dt_node *mem;
	u64 reg[2];
	char name[sizeof("memory@") + STR_MAX_CHARS(reg[0])];
	u32 chip_id;

	printf("  Range: 0x%016llx..0x%016llx on Chip 0x%x mattr: 0x%x\n",
	       (long long)arange->start, (long long)arange->end,
	       pcid_to_chip_id(arange->chip), arange->mirror_attr);

	/* reg contains start and length */
	reg[0] = cleanup_addr(be64_to_cpu(arange->start));
	reg[1] = cleanup_addr(be64_to_cpu(arange->end)) - reg[0];

	chip_id = pcid_to_chip_id(be32_to_cpu(arange->chip));

	if (be16_to_cpu(id->flags) & MS_AREA_SHARED) {
		/* Only enter shared nodes once. */ 
		mem = find_shared(root, be16_to_cpu(id->share_id),
				  reg[0], reg[1]);
		if (mem) {
			append_chip_id(mem, chip_id);
			return true;
		}
	}
	sprintf(name, "memory@%llx", (long long)reg[0]);

	mem = dt_new(root, name);
	dt_add_property_string(mem, "device_type", "memory");
	dt_add_property_cells(mem, "ibm,chip-id", chip_id);
	dt_add_property_u64s(mem, "reg", reg[0], reg[1]);
	if (be16_to_cpu(id->flags) & MS_AREA_SHARED)
		dt_add_property_cells(mem, DT_PRIVATE "share-id",
				      be16_to_cpu(id->share_id));

	return true;
}

static void get_msareas(struct dt_node *root,
			const struct HDIF_common_hdr *ms_vpd)
{
	unsigned int i;
	const struct HDIF_child_ptr *msptr;

	/* First childptr refers to msareas. */
	msptr = HDIF_child_arr(ms_vpd, MSVPD_CHILD_MS_AREAS);
	if (!CHECK_SPPTR(msptr)) {
		prerror("MS VPD: no children at %p\n", ms_vpd);
		return;
	}

	for (i = 0; i < be32_to_cpu(msptr->count); i++) {
		const struct HDIF_common_hdr *msarea;
		const struct HDIF_array_hdr *arr;
		const struct HDIF_ms_area_address_range *arange;
		const struct HDIF_ms_area_id *id;
		const struct HDIF_child_ptr *ramptr;
		const void *fruid;
		unsigned int size, j;
		u16 flags;

		msarea = HDIF_child(ms_vpd, msptr, i, "MSAREA");
		if (!CHECK_SPPTR(msarea))
			return;

		id = HDIF_get_idata(msarea, 2, &size);
		if (!CHECK_SPPTR(id))
			return;
		if (size < sizeof(*id)) {
			prerror("MS VPD: %p msarea #%i id size too small!\n",
				ms_vpd, i);
			return;
		}

		flags = be16_to_cpu(id->flags);
		printf("MS VPD: %p, area %i: %s %s %s\n",
		       ms_vpd, i,
		       flags & MS_AREA_INSTALLED ?
		       "installed" : "not installed",
		       flags & MS_AREA_FUNCTIONAL ?
		       "functional" : "not functional",
		       flags & MS_AREA_SHARED ?
		       "shared" : "not shared");

		if ((flags & (MS_AREA_INSTALLED|MS_AREA_FUNCTIONAL))
		    != (MS_AREA_INSTALLED|MS_AREA_FUNCTIONAL))
			continue;

		arr = HDIF_get_idata(msarea, 4, &size);
		if (!CHECK_SPPTR(arr))
			continue;

		if (size < sizeof(*arr)) {
			prerror("MS VPD: %p msarea #%i arr size too small!\n",
				ms_vpd, i);
			return;
		}

		if (be32_to_cpu(arr->eactsz) < sizeof(*arange)) {
			prerror("MS VPD: %p msarea #%i arange size too small!\n",
				ms_vpd, i);
			return;
		}

		ramptr = HDIF_child_arr(msarea, 0);
		if (!CHECK_SPPTR(ramptr))
			return;

		fruid = HDIF_get_idata(msarea, 0, &size);
		if (!CHECK_SPPTR(fruid))
			return;

		/* This offset is from the arr, not the header! */
		arange = (void *)arr + be32_to_cpu(arr->offset);
		for (j = 0; j < be32_to_cpu(arr->ecnt); j++) {
			if (!add_address_range(root, id, arange))
				return;
			arange = (void *)arange + be32_to_cpu(arr->esize);
		}
	}
}

bool __memory_parse(struct dt_node *root)
{
	struct HDIF_common_hdr *ms_vpd;
	const struct msvpd_ms_addr_config *msac;
	const struct msvpd_total_config_ms *tcms;
	unsigned int size;

	ms_vpd = get_hdif(&spira.ntuples.ms_vpd, MSVPD_HDIF_SIG);
	if (!ms_vpd) {
		prerror("MS VPD: invalid\n");
		op_display(OP_FATAL, OP_MOD_MEM, 0x0000);
		return false;
	}
	if (be32_to_cpu(spira.ntuples.ms_vpd.act_len) < sizeof(*ms_vpd)) {
		prerror("MS VPD: invalid size %u\n",
			be32_to_cpu(spira.ntuples.ms_vpd.act_len));
		op_display(OP_FATAL, OP_MOD_MEM, 0x0001);
		return false;
	}

	printf("MS VPD: is at %p\n", ms_vpd);

	msac = HDIF_get_idata(ms_vpd, MSVPD_IDATA_MS_ADDR_CONFIG, &size);
	if (!CHECK_SPPTR(msac) || size < sizeof(*msac)) {
		prerror("MS VPD: bad msac size %u @ %p\n", size, msac);
		op_display(OP_FATAL, OP_MOD_MEM, 0x0002);
		return false;
	}
	printf("MS VPD: MSAC is at %p\n", msac);

	dt_add_property_u64(dt_root, DT_PRIVATE "maxmem",
			    be64_to_cpu(msac->max_configured_ms_address));

	tcms = HDIF_get_idata(ms_vpd, MSVPD_IDATA_TOTAL_CONFIG_MS, &size);
	if (!CHECK_SPPTR(tcms) || size < sizeof(*tcms)) {
		prerror("MS VPD: Bad tcms size %u @ %p\n", size, tcms);
		op_display(OP_FATAL, OP_MOD_MEM, 0x0003);
		return false;
	}
	printf("MS VPD: TCMS is at %p\n", tcms);

	printf("MS VPD: Maximum configured address: 0x%llx\n",
	       (long long)be64_to_cpu(msac->max_configured_ms_address));
	printf("MS VPD: Maximum possible address: 0x%llx\n",
	       (long long)be64_to_cpu(msac->max_possible_ms_address));

	get_msareas(root, ms_vpd);

	printf("MS VPD: Total MB of RAM: 0x%llx\n",
	       (long long)be64_to_cpu(tcms->total_in_mb));

	return true;
}

void memory_parse(void)
{
	if (!__memory_parse(dt_root)) {
		prerror("MS VPD: Failed memory init !\n");
		abort();
	}
}

