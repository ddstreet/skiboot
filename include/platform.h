/* Copyright 2013-2014 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __PLATFORM_H
#define __PLATFORM_H

/* Some fwd declarations for types used further down */
struct phb;
struct pci_device;

/*
 * Each platform can provide a set of hooks
 * that can affect the generic code
 */
struct platform {
	const char	*name;

	/*
	 * Probe platform, return true on a match, called before
	 * any allocation has been performed outside of the heap
	 * so the platform can perform additional memory reservations
	 * here if needed.
	 *
	 * Only the boot CPU is running at this point and the cpu_thread
	 * structure for secondaries have not been initialized yet. The
	 * timebases are not synchronized.
	 *
	 * Services available:
	 *
	 * - Memory allocations / reservations
	 * - XSCOM
	 * - FSI
	 * - Host Services
	 */
	bool		(*probe)(void);

	/*
	 * This is called right after the secondary processors are brought
	 * up and the timebases in sync to perform any additional platform
	 * specific initializations. On FSP based machines, this is where
	 * the FSP driver is brought up.
	 */
	void		(*init)(void);

	/*
	 * These are used to power down and reboot the machine
	 */
	int64_t		(*cec_power_down)(uint64_t request);
	int64_t		(*cec_reboot)(void);

	/*
	 * This is called once per PHB before probing. It allows the
	 * platform to setup some PHB private data that can be used
	 * later on by calls such as pci_get_slot_info() below. The
	 * "index" argument is the PHB index within the IO HUB (or
	 * P8 chip).
	 *
	 * This is called before the PHB HW has been initialized.
	 */
	void		(*pci_setup_phb)(struct phb *phb, unsigned int index);

	/*
	 * Called during PCI scan for each device. For bridges, this is
	 * called before its children are probed. This is called for
	 * every device and for the PHB itself with a NULL pd though
	 * typically the implementation will only populate the slot
	 * info structure for bridge ports
	 */
	void		(*pci_get_slot_info)(struct phb *phb,
					     struct pci_device *pd);

	/*
	 * Called after PCI probe is complete and before inventory is
	 * displayed in console. This can either run platform fixups or
	 * can be used to send the inventory to a service processor.
	 */
	void		(*pci_probe_complete)(void);

	/*
	 * If the above is set to skiboot, the handler is here
	 */
	void		(*external_irq)(unsigned int chip_id);

	/*
	 * nvram ops.
	 *
	 * Note: To keep the FSP driver simple, we only ever read the
	 * whole nvram once at boot and we do this passing a dst buffer
	 * that is 4K aligned. The read is asynchronous, the backend
	 * must call nvram_read_complete() when done (it's allowed to
	 * do it recursively from nvram_read though).
	 */
	int		(*nvram_info)(uint32_t *total_size);
	int		(*nvram_start_read)(void *dst, uint32_t src,
					    uint32_t len);
	int		(*nvram_write)(uint32_t dst, void *src, uint32_t len);
};

extern struct platform __platforms_start;
extern struct platform __platforms_end;

struct platform	platform;

#define DECLARE_PLATFORM(name)\
static const struct platform __used __section(".platforms") name ##_platform

extern void probe_platform(void);

#endif /* __PLATFORM_H */