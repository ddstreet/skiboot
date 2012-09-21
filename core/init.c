#include <skiboot.h>
#include <fsp.h>

/*
 * Boot semaphore, incremented by each CPU calling in
 *
 * Forced into data section as it will be used before BSS is initialized
 */
unsigned int boot_cpu_count __force_data = 0;
enum ipl_state ipl_state = ipl_initial;

static bool state_control_msg(u32 cmd_sub_mod, struct fsp_msg *msg)
{
	switch(cmd_sub_mod) {
	case FSP_CMD_CONTINUE_IPL:
		/* We get a CONTINUE_IPL as a response to OPL */
		printf("INIT: Got CONTINUE_IPL !\n");
		ipl_state |= ipl_got_continue;
		free(msg);
		return true;

	case FSP_CMD_HV_STATE_CHG:
		printf("INIT: Got HV state change request to %d\n",
		       msg->data.bytes[0]);

		/* Send response synchronously for now, we might want to
		 * deal with that sort of stuff asynchronously if/when
		 * we add support for auto-freeing of messages
		 */
		fsp_sync_msg(fsp_mkmsg(FSP_RSP_HV_STATE_CHG, 0), true);
		free(msg);
		return true;

	case FSP_CMD_SP_NEW_ROLE:
		/* FSP is assuming a new role */
		printf("INIT: FSP assuming new role\n");
		fsp_sync_msg(fsp_mkmsg(FSP_RSP_SP_NEW_ROLE, 0), true);
		ipl_state |= ipl_got_new_role;
		free(msg);
		return true;

	case FSP_CMD_SP_QUERY_CAPS:
		printf("INIT: FSP query capabilities\n");
		/* XXX Do something saner */
		fsp_sync_msg(fsp_mkmsgw(FSP_RSP_SP_QUERY_CAPS, 4,
					0x3ff80000, 0, 0, 0), true);
		ipl_state |= ipl_got_caps;
		free(msg);
		return true;		
	}
	return false;
}

static struct fsp_client state_control = {
	.message = state_control_msg,
};

static void start_fsp_state_control(void)
{
	/* Register for IPL/SERVICE messages */
	fsp_register_client(&state_control, FSP_MCLASS_IPL);

	/* Send OPL */
	ipl_state |= ipl_opl_sent;
	fsp_sync_msg(fsp_mkmsg(FSP_CMD_OPL, 0), true);
	while(!(ipl_state & ipl_got_continue))
		fsp_poll();

	/* Send continue ACK */
	fsp_sync_msg(fsp_mkmsg(FSP_CMD_CONTINUE_ACK, 0), true);

	printf("INIT: Waiting for FSP to advertize new role...\n");
	while(!(ipl_state & ipl_got_new_role))
		fsp_poll();

	printf("INIT: Waiting for FSP to request capabilities...\n");
	while(!(ipl_state & ipl_got_caps))
		fsp_poll();
}

void main_cpu_entry(void)
{
	printf("SkiBoot starting...\n");

	/* Early initializations of the FSP interface */
	fsp_init();

	/* Get ready to receive E0 class messages. We need to respond
	 * to some of these for the init sequence to make forward progress
	 */
	fsp_console_preinit();

	/* Start FSP/HV state controller */
	start_fsp_state_control();

	/* Tell FSP we are in standby (XXX use running ?) */
	fsp_sync_msg(fsp_mkmsgw(FSP_CMD_HV_FUNCTNAL, 1, 0x01000000), true);

	/* Finish initializing the console */
	fsp_console_init();

	/* Nothing to do */
	while(true)
		fsp_poll();
}
