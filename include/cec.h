#ifndef __CEC_H
#define __CEC_H

#include <stdint.h>

/* This represent an IO Hub and contains the function pointers
 * for the IO Hub related OPAL ops and other internal functions
 */

struct io_hub;

struct io_hub_ops {
	/* OPAL_PCI_SET_HUB_TCE_MEMORY (p5ioc2 only) */
	int64_t (*set_tce_mem)(struct io_hub *hub, uint64_t address,
				   uint64_t size);

	/* OPAL_PCI_GET_HUB_DIAG_DATA */
	int64_t (*get_diag_data)(struct io_hub *hub, void *diag_buffer,
				 uint64_t diag_buffer_len);

	/* Called on fast reset */
	void (*reset)(struct io_hub *hub);
};

struct io_hub {
	uint32_t			hub_id;
	const struct io_hub_ops		*ops;
};

extern struct io_hub *cec_get_hub_by_id(uint32_t hub_id);

extern void cec_reset(void);
extern void cec_register(struct io_hub *hub);

#endif /* __CEC_H */
