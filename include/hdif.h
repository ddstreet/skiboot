#ifndef __HDIF_H
#define __HDIF_H

#include <skiboot.h>

struct HDIF_common_hdr {
	u16	d1f0;		/* 0xd1f0 */
	char	id[6];		/* eye catcher string */
	u16	instnum;	/* instance number */
	u16	version;	/* version */
	u32	total_len;	/* total structure length */
	u32	hdr_len;	/* header length (currently 0x20) */
	u32	idptr_off;	/* offset to idata pointers */
	u16	idptr_count;	/* number of idata pointers */
	u16	child_count;	/* number of child structures */
	u32	child_off;	/* offset to child structures array */
} __packed __align(0x10);

struct HDIF_idata_ptr {
	u32	offset;
	u32	size;
} __packed __align(0x8);

struct HDIF_array_hdr {
	u32	offset;
	u32	ecnt;
	u32	esize;
	u32	eactsz;
} __packed __align(0x10);

struct HDIF_child_ptr {
	uint32_t	offset;
	uint32_t	size;
	uint32_t	count;
} __packed;

#define HDIF_HDR_LEN		(sizeof(struct HDIF_common_hdr))
#define HDIF_ARRAY_OFFSET	(sizeof(struct HDIF_array_hdr))

#define HDIF_ID(_id)		.d1f0 = 0xd1f0, .id = _id

#define HDIF_SIMPLE_HDR(id, vers, type)				\
{								\
	HDIF_ID(id),						\
	.instnum	= 0,					\
	.version	= vers,					\
	.total_len	= sizeof(type),				\
	.hdr_len	= HDIF_HDR_LEN,				\
	.idptr_off	= HDIF_HDR_LEN,				\
	.idptr_count	= 1,					\
	.child_count	= 0,					\
	.child_off	= 0,					\
}

static inline bool HDIF_check(const void *hdif, const char id[])
{
	const struct HDIF_common_hdr *hdr = hdif;

	return hdr->d1f0 == 0xd1f0 && memcmp(hdr->id, id, sizeof(hdr->id)) == 0;
}

/* HDIF_get_idata - Get a pointer to internal data block
 *
 * @hdif  : HDIF structure pointer
 * @di    : Index of the idata pointer
 * @size  : Return the data size (or NULL if ignored)
 */
extern const void *HDIF_get_idata(const void *hdif, unsigned int di,
				  unsigned int *size);

/* HDIF_get_iarray - Get a pointer to an elemnt of an internal data array
 *
 * @hdif  : HDIF structure pointer
 * @di    : Index of the idata pointer
 * @ai    : Index in the resulting array
 * @size  : Return the entry actual size (or NULL if ignored)
 */
extern const void *HDIF_get_iarray_item(const void *hdif, unsigned int di,
					unsigned int ai, unsigned int *size);

/* HDIF_get_iarray_size - Get the number of elements of an internal data array
 *
 * @hdif  : HDIF structure pointer
 * @di    : Index of the idata pointer
 *
 * A negative result means an error
 */
extern int HDIF_get_iarray_size(const void *hdif, unsigned int di);

/* HDIF_child_arr - Get a child array from this HDIF.
 *
 * @hdif  : HDIF structure pointer
 * @idx	  : the child to get
 *
 * NULL means an error (not that many children).
 */
extern struct HDIF_child_ptr *
HDIF_child_arr(const struct HDIF_common_hdr *hdif, unsigned int idx);

/* HDIF_child - Deref a child_ptr entry.
 *
 * @hdif  : HDIF structure pointer
 * @child : the child returned from HDIF_child_arr
 * @idx	  : the index of the child to get (< child->count).
 * @eyecatcher: the 6-char ID expected for this child.
 *
 * NULL means an error.
 */
extern struct HDIF_common_hdr *HDIF_child(const struct HDIF_common_hdr *hdif,
					  const struct HDIF_child_ptr *child,
					  unsigned int idx,
					  const char *eyecatcher);
#endif /* __HDIF_H */