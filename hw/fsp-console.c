/* (C) Copyright IBM Corp., 2013 and provided pursuant to the Technology
 * Licensing Agreement between Google Inc. and International Business
 * Machines Corporation, IBM License Reference Number AA130103030256 and
 * confidentiality governed by the Parties’ Mutual Nondisclosure Agreement
 * number V032404DR, executed by the parties on November 6, 2007, and
 * Supplement V032404DR-3 dated August 16, 2012 (the “NDA”). */
/*
 * Service Processor serial console handling code
 */
#include <skiboot.h>
#include <processor.h>
#include <io.h>
#include <fsp.h>
#include <console.h>
#include <opal.h>
#include <time.h>
#include <device.h>

struct fsp_serbuf_hdr {
	u16	partition_id;
	u8	session_id;
	u8	hmc_id;
	u16	data_offset;
	u16	last_valid;
	u16	ovf_count;
	u16	next_in;
	u8	flags;
	u8	reserved;
	u16	next_out;
	u8	data[];
};
#define SER_BUF_DATA_SIZE	(0x10000 - sizeof(struct fsp_serbuf_hdr))

struct fsp_serial {
	bool			available;
	bool			open;
	bool			has_part0;
	bool			has_part1;
	bool			log_port;
	bool			out_poke;
	char			loc_code[LOC_CODE_SIZE];
	u16			rsrc_id;
	struct fsp_serbuf_hdr	*in_buf;
	struct fsp_serbuf_hdr	*out_buf;
	struct fsp_msg		*poke_msg;
};

#define MAX_SERIAL	4

static struct fsp_serial fsp_serials[MAX_SERIAL];
static bool got_intf_query;
static bool got_assoc_resp;
static bool got_deassoc_resp;

static void fsp_pokemsg_reclaim(struct fsp_msg *msg)
{
	struct fsp_serial *fs = msg->user_data;

	/* Synchronize with fsp_write_serial() */
	lock(&con_lock);
	if (fs->open) {
		if (fs->out_poke) {
			fs->out_poke = false;
			fsp_queue_msg(fs->poke_msg, fsp_pokemsg_reclaim);
		} else
			fs->poke_msg->state = fsp_msg_unused;
	} else {
		fsp_freemsg(msg);
		fs->poke_msg = NULL;
	}
	unlock(&con_lock);
}

/* NOTE: This is meant to be called with the con_lock held. This will
 * be true as well of the runtime variant called via the OPAL APIs
 * unless we change the locking scheme (might be suitable to have
 * the console call this without lock, and use the FSP lock here,
 * since fsp_queue_msg() supports recursive locking. That would
 * limit the number of atomic ops on the console path.
 */
static size_t fsp_write_vserial(struct fsp_serial *fs, const char *buf,
				size_t len)
{
	struct fsp_serbuf_hdr *sb = fs->out_buf;
	u16 old_nin = sb->next_in;
	u16 space, chunk;

	if (!fs->open)
		return 0;

	space = (sb->next_out + SER_BUF_DATA_SIZE - old_nin - 1)
		% SER_BUF_DATA_SIZE;
	if (space < len)
		len = space;
	if (!len)
		return 0;

	chunk = SER_BUF_DATA_SIZE - old_nin;
	if (chunk > len)
		chunk = len;
	memcpy(&sb->data[old_nin], buf, chunk);
	if (chunk < len)
		memcpy(&sb->data[0], buf + chunk, len - chunk);
	lwsync();
	sb->next_in = (old_nin + len) % SER_BUF_DATA_SIZE;
	sync();

	if (sb->next_out == old_nin) {
		if (fs->poke_msg->state == fsp_msg_unused)
			fsp_queue_msg(fs->poke_msg, fsp_pokemsg_reclaim);
		else
			fs->out_poke = true;
	}
#ifndef DISABLE_CON_PENDING_EVT
	opal_update_pending_evt(OPAL_EVENT_CONSOLE_OUTPUT,
				OPAL_EVENT_CONSOLE_OUTPUT);
#endif
	return len;
}

#ifdef DVS_CONSOLE
static int fsp_con_port = -1;
static bool fsp_con_full;

static size_t fsp_con_write(const char *buf, size_t len)
{
	size_t written;

	if (fsp_con_port < 0)
		return 0;

	written = fsp_write_vserial(&fsp_serials[fsp_con_port], buf, len);
	fsp_con_full = (written < len);

	return written;
}

static struct con_ops fsp_con_ops = {
	.write = fsp_con_write,
};
#endif /* DVS_CONSOLE */

static void fsp_open_vserial(struct fsp_msg *msg)
{
	u16 part_id = msg->data.words[0] & 0xffff;
	u16 sess_id = msg->data.words[1] & 0xffff;
	u8 hmc_sess = msg->data.bytes[0];	
	u8 hmc_indx = msg->data.bytes[1];
	u8 authority = msg->data.bytes[4];
	u32 tce_in, tce_out;
	struct fsp_serial *fs;

	printf("FSPCON: Got VSerial Open\n");
	printf("  part_id   = 0x%04x\n", part_id);
	printf("  sess_id   = 0x%04x\n", sess_id);
	printf("  hmc_sess  = 0x%02x\n", hmc_sess);
	printf("  hmc_indx  = 0x%02x\n", hmc_indx);
	printf("  authority = 0x%02x\n", authority);

	if (sess_id >= MAX_SERIAL || !fsp_serials[sess_id].available) {
		fsp_queue_msg(fsp_mkmsg(FSP_RSP_OPEN_VSERIAL | 0x2f, 0),
			      fsp_freemsg);
		printf("  NOT AVAILABLE !\n");
		return;
	}

	fs = &fsp_serials[sess_id];

	/* Hack ! On blades, the console opened via the mm has partition 1
	 * while the debug DVS generally has partition 0 (though you can
	 * use what you want really).
	 * We don't want a DVS open/close to crap on the blademm console
	 * thus if it's a raw console, gets an open with partID 1, we
	 * set a flag that ignores the close of partid 0
	 */
	if (fs->rsrc_id == 0xffff) {
		if (part_id == 0)
			fs->has_part0 = true;
		if (part_id == 1)
			fs->has_part1 = true;
	}

	tce_in = PSI_DMA_SER0_BASE + PSI_DMA_SER0_SIZE * sess_id;
	tce_out = tce_in + SER0_BUFFER_SIZE/2;

	if (fs->open) {
		printf("  already open, skipping init !\n");
		goto already_open;
	}

	fs->open = true;

	/* If we still have a msg, wait for it to go away */
	while (fs->poke_msg)
		fsp_poll();

	fs->poke_msg = fsp_mkmsg(FSP_CMD_VSERIAL_OUT, 2,
				 msg->data.words[0],
				 msg->data.words[1] & 0xffff);
	fs->poke_msg->user_data = fs;

	fs->in_buf->partition_id = fs->out_buf->partition_id = part_id;
	fs->in_buf->session_id	 = fs->out_buf->session_id   = sess_id;
	fs->in_buf->hmc_id       = fs->out_buf->hmc_id       = hmc_indx;
	fs->in_buf->data_offset  = fs->out_buf->data_offset  =
		sizeof(struct fsp_serbuf_hdr);
	fs->in_buf->last_valid   = fs->out_buf->last_valid   =
		SER_BUF_DATA_SIZE - 1;
	fs->in_buf->ovf_count    = fs->out_buf->ovf_count    = 0;
	fs->in_buf->next_in      = fs->out_buf->next_in      = 0;
	fs->in_buf->flags        = fs->out_buf->flags        = 0;
	fs->in_buf->reserved     = fs->out_buf->reserved     = 0;
	fs->in_buf->next_out     = fs->out_buf->next_out     = 0;

 already_open:
	fsp_queue_msg(fsp_mkmsg(FSP_RSP_OPEN_VSERIAL, 6,
				msg->data.words[0],
				msg->data.words[1] & 0xffff,
				0, tce_in, 0, tce_out), fsp_freemsg);

#ifdef DVS_CONSOLE
	printf("  log_port  = %d\n", fs->log_port);
	if (fs->log_port) {
		fsp_con_port = sess_id;
		sync();
		set_console(&fsp_con_ops);
	}
#endif
}

static void fsp_close_vserial(struct fsp_msg *msg)
{
	u16 part_id = msg->data.words[0] & 0xffff;
	u16 sess_id = msg->data.words[1] & 0xffff;
	u8 hmc_sess = msg->data.bytes[0];	
	u8 hmc_indx = msg->data.bytes[1];
	u8 authority = msg->data.bytes[4];
	struct fsp_serial *fs;

	printf("FSPCON: Got VSerial Close\n");
	printf("  part_id   = 0x%04x\n", part_id);
	printf("  sess_id   = 0x%04x\n", sess_id);
	printf("  hmc_sess  = 0x%02x\n", hmc_sess);
	printf("  hmc_indx  = 0x%02x\n", hmc_indx);
	printf("  authority = 0x%02x\n", authority);

	if (sess_id >= MAX_SERIAL || !fsp_serials[sess_id].available) {
		fsp_queue_msg(fsp_mkmsg(FSP_RSP_CLOSE_VSERIAL /*| 0x2f*/, 0),
			      fsp_freemsg);
		printf("  NOT AVAILABLE !\n");
		return;
	}

	fs = &fsp_serials[sess_id];

	/* See "HACK" comment in open */
	if (fs->rsrc_id == 0xffff) {
		if (part_id == 0)
			fs->has_part0 = false;
		if (part_id == 1)
			fs->has_part1 = false;
		if (fs->has_part0 || fs->has_part1) {
			printf("  skipping close !\n");
			goto skip_close;
		}
	}

#ifdef DVS_CONSOLE
	if (fs->log_port) {
		fsp_con_port = -1;
		set_console(NULL);
	}
#endif
	
	lock(&con_lock);
	fs->open = false;
	fs->out_poke = false;
	if (fs->poke_msg && fs->poke_msg->state == fsp_msg_unused) {
		fsp_freemsg(fs->poke_msg);
		fs->poke_msg = NULL;
	}
	unlock(&con_lock);
 skip_close:
	fsp_queue_msg(fsp_mkmsg(FSP_RSP_CLOSE_VSERIAL, 0), fsp_freemsg);

}

static bool fsp_con_msg_hmc(u32 cmd_sub_mod, struct fsp_msg *msg)
{
	/* Associate response */
	if ((cmd_sub_mod >> 8) == 0xe08a) {
		printf("FSPCON: Got associate response, status 0x%02x\n",
		       cmd_sub_mod & 0xff);
		got_assoc_resp = true;
		return true;
	}
	if ((cmd_sub_mod >> 8) == 0xe08b) {
		printf("Got unassociate response, status 0x%02x\n",
		       cmd_sub_mod & 0xff);
		got_deassoc_resp = true;
		return true;
	}
	switch(cmd_sub_mod) {
	case FSP_CMD_OPEN_VSERIAL:
		fsp_open_vserial(msg);
		return true;
	case FSP_CMD_CLOSE_VSERIAL:
		fsp_close_vserial(msg);
		return true;
	case FSP_CMD_HMC_INTF_QUERY:
		printf("FSPCON: Got HMC interface query\n");

		/* Keep that synchronous due to FSP fragile ordering
		 * of the boot sequence
		 */
		fsp_sync_msg(fsp_mkmsg(FSP_RSP_HMC_INTF_QUERY, 1,
				       msg->data.words[0] & 0x00ffffff), true);
		got_intf_query = true;
		return true;
	}
	return false;
}

static bool fsp_con_msg_vt(u32 cmd_sub_mod, struct fsp_msg *msg)
{
	u16 sess_id = msg->data.words[1] & 0xffff;

	if (cmd_sub_mod == FSP_CMD_VSERIAL_IN && sess_id < MAX_SERIAL) {
		struct fsp_serial *fs = &fsp_serials[sess_id];

		if (!fs->open)
			return true;

		/* FSP is signaling some incoming data. We take the console
		 * lock to avoid racing with a simultaneous read, though we
		 * might want to consider to simplify all that locking into
		 * one single lock that covers the console and the pending
		 * events.
		 */
		lock(&con_lock);
		opal_update_pending_evt(OPAL_EVENT_CONSOLE_INPUT,
					OPAL_EVENT_CONSOLE_INPUT);
		unlock(&con_lock);
	}
	return true;
}

static struct fsp_client fsp_con_client_hmc = {
	.message = fsp_con_msg_hmc,
};

static struct fsp_client fsp_con_client_vt = {
	.message = fsp_con_msg_vt,
};

static void fsp_serial_add(int index, u16 rsrc_id, const char *loc_code,
			   bool log_port)
{
	struct fsp_serial *ser;

	lock(&con_lock);
	ser = &fsp_serials[index];

	if (ser->available) {
		unlock(&con_lock);
		return;
	}

	ser->rsrc_id = rsrc_id;
	strncpy(ser->loc_code, loc_code, LOC_CODE_SIZE);
	ser->available = true;
	ser->log_port = log_port;
	unlock(&con_lock);

	/* DVS doesn't have that */
	if (rsrc_id != 0xffff) {
		got_assoc_resp = false;
		fsp_sync_msg(fsp_mkmsg(FSP_CMD_ASSOC_SERIAL, 2,
				       (rsrc_id << 16) | 1, index), true);
		/* XXX add timeout ? */
		while(!got_assoc_resp)
			fsp_poll();
	}
}

void fsp_console_preinit(void)
{
	int i;
	void *base;

	if (!fsp_present())
		return;

	/* Initialize out data structure pointers & TCE maps */
	base = (void *)SER0_BUFFER_BASE;
	for (i = 0; i < MAX_SERIAL; i++) {
		struct fsp_serial *ser = &fsp_serials[i];

		ser->in_buf = base;
		ser->out_buf = base + SER0_BUFFER_SIZE/2;
		base += SER0_BUFFER_SIZE;
	}
	fsp_tce_map(PSI_DMA_SER0_BASE, (void*)SER0_BUFFER_BASE,
		    4 * PSI_DMA_SER0_SIZE);

	/* Register for class E0 and E1 */
	fsp_register_client(&fsp_con_client_hmc, FSP_MCLASS_HMC_INTFMSG);
	fsp_register_client(&fsp_con_client_vt, FSP_MCLASS_HMC_VT);

	/* Add DVS ports. We currently have session 0 and 3, 0 is for
	 * OS use. 3 is our debug port. We need to add those before
	 * we complete the OPL or we'll potentially miss the
	 * console setup on Firebird blades.
	 */
	fsp_serial_add(0, 0xffff, "DVS_OS", false);
	op_display(OP_LOG, OP_MOD_FSPCON, 0x0001);
	fsp_serial_add(3, 0xffff, "DVS_FW", true);
	op_display(OP_LOG, OP_MOD_FSPCON, 0x0002);

}

static int64_t fsp_console_write(int64_t term_number, int64_t *length,
				 const uint8_t *buffer)
{
	struct fsp_serial *fs;
	size_t written, requested;

	if (term_number < 0 || term_number >= MAX_SERIAL)
		return OPAL_PARAMETER;
	fs = &fsp_serials[term_number];
	if (!fs->available || fs->log_port)
		return OPAL_PARAMETER;
	lock(&con_lock);
	if (!fs->open) {
		unlock(&con_lock);
		return OPAL_CLOSED;
	}
	/* Clamp to a reasonable size */
	requested = *length;
	if (requested > 0x1000)
		requested = 0x1000;
	written = fsp_write_vserial(fs, buffer, requested);

#ifdef OPAL_DEBUG_CONSOLE_IO
	printf("OPAL: console write req=%ld written=%ld ni=%d no=%d\n",
	       requested, written, fs->out_buf->next_in, fs->out_buf->next_out);
	printf("      %02x %02x %02x %02x "
	       "%02x \'%c\' %02x \'%c\' %02x \'%c\'.%02x \'%c\'..\n",
	       buffer[0], buffer[1], buffer[2], buffer[3],
	       buffer[4], buffer[4], buffer[5], buffer[5],
	       buffer[6], buffer[6], buffer[7], buffer[7]);
#endif /* OPAL_DEBUG_CONSOLE_IO */

	*length = written;
	unlock(&con_lock);

	return written ? OPAL_SUCCESS : OPAL_BUSY_EVENT;
}

static int64_t fsp_console_write_buffer_space(int64_t term_number,
					      int64_t *length)
{
	struct fsp_serial *fs;
	struct fsp_serbuf_hdr *sb;

	if (term_number < 0 || term_number >= MAX_SERIAL)
		return OPAL_PARAMETER;
	fs = &fsp_serials[term_number];
	if (!fs->available || fs->log_port)
		return OPAL_PARAMETER;
	lock(&con_lock);
	if (!fs->open) {
		unlock(&con_lock);
		return OPAL_CLOSED;
	}
	sb = fs->out_buf;
	*length = (sb->next_out + SER_BUF_DATA_SIZE - sb->next_in - 1)
		% SER_BUF_DATA_SIZE;
	unlock(&con_lock);

	return OPAL_SUCCESS;
}

static int64_t fsp_console_read(int64_t term_number, int64_t *length,
				uint8_t *buffer __unused)
{
	struct fsp_serial *fs;
	struct fsp_serbuf_hdr *sb;
	bool pending = false;
	uint32_t old_nin, n, i, chunk, req = *length;

	if (term_number < 0 || term_number >= MAX_SERIAL)
		return OPAL_PARAMETER;
	fs = &fsp_serials[term_number];
	if (!fs->available || fs->log_port)
		return OPAL_PARAMETER;
	lock(&con_lock);
	if (!fs->open) {
		unlock(&con_lock);
		return OPAL_CLOSED;
	}
	sb = fs->in_buf;
	old_nin = sb->next_in;
	lwsync();
	n = (old_nin + SER_BUF_DATA_SIZE - sb->next_out)
		% SER_BUF_DATA_SIZE;
	if (n > req) {
		pending = true;
		n = req;
	}
	*length = n;

	chunk = SER_BUF_DATA_SIZE - sb->next_out;
	if (chunk > n)
		chunk = n;
	memcpy(buffer, &sb->data[sb->next_out], chunk);
	if (chunk < n)
		memcpy(buffer + chunk, &sb->data[0], n - chunk);
	sb->next_out = (sb->next_out + n) % SER_BUF_DATA_SIZE;

#ifdef OPAL_DEBUG_CONSOLE_IO
	printf("OPAL: console read req=%d read=%d ni=%d no=%d\n",
	       req, n, sb->next_in, sb->next_out);
	printf("      %02x %02x %02x %02x %02x %02x %02x %02x ...\n",
	       buffer[0], buffer[1], buffer[2], buffer[3],
	       buffer[4], buffer[5], buffer[6], buffer[7]);
#endif /* OPAL_DEBUG_CONSOLE_IO */

	/* Might clear the input pending flag */
	for (i = 0; i < MAX_SERIAL && !pending; i++) {
		struct fsp_serial *fs = &fsp_serials[i];
		struct fsp_serbuf_hdr *sb = fs->in_buf;

		if (fs->log_port || !fs->open)
			continue;
		if (sb->next_out != sb->next_in)
			pending = true;
	}
	if (!pending)
		opal_update_pending_evt(OPAL_EVENT_CONSOLE_INPUT, 0);

	unlock(&con_lock);

	return OPAL_SUCCESS;
}

void fsp_console_poll(void *data __unused)
{
#ifdef OPAL_DEBUG_CONSOLE_POLL
       	static int debug;
#endif

	/* We don't get messages for out buffer being consumed, so we
	 * need to poll
	 */
	if (fsp_con_full ||
	    (opal_pending_events & OPAL_EVENT_CONSOLE_OUTPUT)) {
		unsigned int i;
		bool pending = false;

		/* We take the console lock. This is somewhat inefficient
		 * but it guarantees we aren't racing with a write, and
		 * thus clearing an event improperly
		 */
		lock(&con_lock);
		for (i = 0; i < MAX_SERIAL && !pending; i++) {
			struct fsp_serial *fs = &fsp_serials[i];
			struct fsp_serbuf_hdr *sb = fs->out_buf;

			if (!fs->open)
				continue;
			if (sb->next_out == sb->next_in)
				continue;
			if (fs->log_port)
				__flush_console();
			else {
#ifdef OPAL_DEBUG_CONSOLE_POLL
				if (debug < 5) {
					printf("OPAL: %d still pending"
					       " ni=%d no=%d\n",
					       i, sb->next_in, sb->next_out);
					debug++;
				}
#endif /* OPAL_DEBUG_CONSOLE_POLL */
				pending = true;
			}
		}
		if (!pending) {
			opal_update_pending_evt(OPAL_EVENT_CONSOLE_OUTPUT, 0);
#ifdef OPAL_DEBUG_CONSOLE_POLL
			debug = 0;
#endif
		}
		unlock(&con_lock);
	}
}

void fsp_console_init(void)
{
	struct dt_node *serials, *ser;
	int i;

	if (!fsp_present())
		return;

	opal_register(OPAL_CONSOLE_READ, fsp_console_read);
	opal_register(OPAL_CONSOLE_WRITE_BUFFER_SPACE,
		      fsp_console_write_buffer_space);
	opal_register(OPAL_CONSOLE_WRITE, fsp_console_write);

	/* Wait until we got the intf query before moving on */
	while (!got_intf_query)
		fsp_poll();

	op_display(OP_LOG, OP_MOD_FSPCON, 0x0000);

	/* Register poller */
	opal_add_poller(fsp_console_poll, NULL);

	/* Parse serial port data */
	serials = dt_find_by_path(dt_root, "ipl-params/fsp-serial");
	if (!serials) {
		prerror("FSPCON: No FSP serial ports in device-tree\n");
		return;
	}

	i = 1;
	dt_for_each_child(serials, ser) {
		u32 rsrc_id = dt_prop_get_u32(ser, "reg");
		const void *lc = dt_prop_get(ser, "ibm,loc-code");

		printf("FSPCON: Serial %d rsrc: %04x loc: %s\n",
		       i, rsrc_id, (char *)lc);
		fsp_serial_add(i++, rsrc_id, lc, false);
		op_display(OP_LOG, OP_MOD_FSPCON, 0x0010 + i);
	}

	op_display(OP_LOG, OP_MOD_FSPCON, 0x0005);
}

static void flush_all_input(void)
{
	unsigned int i;

	lock(&con_lock);
 	for (i = 0; i < MAX_SERIAL; i++) {
		struct fsp_serial *fs = &fsp_serials[i];
		struct fsp_serbuf_hdr *sb = fs->in_buf;

		if (fs->log_port)
			continue;

		sb->next_out = sb->next_in;
	}
	unlock(&con_lock);
}
		
static bool send_all_hvsi_close(void)
{
	unsigned int i;
	bool has_hvsi = false;
	static const uint8_t close_packet[] = { 0xfe, 6, 0, 1, 0, 3 };

	lock(&con_lock);
 	for (i = 0; i < MAX_SERIAL; i++) {
		struct fsp_serial *fs = &fsp_serials[i];
		struct fsp_serbuf_hdr *sb = fs->out_buf;
		unsigned int space, timeout = 10;

		if (fs->log_port)
			continue;
		if (fs->rsrc_id == 0xffff)
			continue;
		has_hvsi = true;

		/* Do we have room ? Wait a bit if not */
		while(timeout--) {
			space = (sb->next_out + SER_BUF_DATA_SIZE -
				 sb->next_in - 1) % SER_BUF_DATA_SIZE;
			if (space >= 6)
				break;
			time_wait_ms(500);
		}
		fsp_write_vserial(fs, close_packet, 6);
	}
	unlock(&con_lock);

	return has_hvsi;
}

static void reopen_all_hvsi(void)
{
	unsigned int i;

 	for (i = 0; i < MAX_SERIAL; i++) {
		struct fsp_serial *fs = &fsp_serials[i];
		if (fs->rsrc_id == 0xffff)
			continue;
		printf("FSP: Deassociating HVSI console %d\n", i);
		got_deassoc_resp = false;
		fsp_sync_msg(fsp_mkmsg(FSP_CMD_UNASSOC_SERIAL, 1,
				       (i << 16) | 1), true);
		/* XXX add timeout ? */
		while(!got_deassoc_resp)
			fsp_poll();
	}
 	for (i = 0; i < MAX_SERIAL; i++) {
		struct fsp_serial *fs = &fsp_serials[i];
		if (fs->rsrc_id == 0xffff)
			continue;
		printf("FSP: Reassociating HVSI console %d\n", i);
		got_assoc_resp = false;
		fsp_sync_msg(fsp_mkmsg(FSP_CMD_ASSOC_SERIAL, 2,
				       (fs->rsrc_id << 16) | 1, i), true);
		/* XXX add timeout ? */
		while(!got_assoc_resp)
			fsp_poll();
	}
}

void fsp_console_reset(void)
{
	printf("FSP: Console reset !\n");

	/* This is called on a fast-reset. To work around issues with HVSI
	 * initial negotiation, before we reboot the kernel, we flush all
	 * input and send an HVSI close packet.
	 */
	flush_all_input();

	/* Returns false if there is no HVSI console */
	if (!send_all_hvsi_close())
		return;

	time_wait_ms(500);
	
	flush_all_input();

	reopen_all_hvsi();

}

void fsp_console_add_nodes(struct dt_node *opal)
{
	unsigned int i;
	struct dt_node *consoles;

	consoles = dt_new(opal, "consoles");
	dt_add_property_cells(consoles, "#address-cells", 1);
	dt_add_property_cells(consoles, "#size-cells", 0);
	for (i = 0; i < MAX_SERIAL; i++) {
		struct fsp_serial *fs = &fsp_serials[i];
		struct dt_node *fs_node;
		char name[32];

		if (fs->log_port || !fs->available)
			continue;

		snprintf(name, sizeof(name), "serial@%d", i);
		fs_node = dt_new(consoles, name);
		if (fs->rsrc_id == 0xffff)
			dt_add_property_string(fs_node, "compatible",
					       "ibm,opal-console-raw");
		else
			dt_add_property_string(fs_node, "compatible",
					       "ibm,opal-console-hvsi");
		dt_add_property_cells(fs_node,
				     "#write-buffer-size", SER_BUF_DATA_SIZE);
		dt_add_property_cells(fs_node, "reg", i);
		dt_add_property_string(fs_node, "device_type", "serial");
	}
}

void fsp_console_select_stdout(void)
{
	if (!fsp_present())
		return;

	/* If DVS is connected, use that as a console. Else, check for
	 * available serial ports, and finally if nothing's connected,
	 * leave the command line alone and let the kernel pick whatever
	 * it wants
	 */
	if (fsp_serials[0].open) {
		dt_add_property_string(dt_chosen, "linux,stdout-path",
				       "/ibm,opal/consoles/serial@0");
		printf("FSPCON: default console 0\n");
	} else if (fsp_serials[1].open) {
		dt_add_property_string(dt_chosen, "linux,stdout-path",
				       "/ibm,opal/consoles/serial@1");
		printf("FSPCON: default console 1\n");
	} else if (fsp_serials[2].open) {
		dt_add_property_string(dt_chosen, "linux,stdout-path",
				       "/ibm,opal/consoles/serial@2");
		printf("FSPCON: default console 2\n");
	}
}

