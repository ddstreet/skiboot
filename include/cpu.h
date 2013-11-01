/* (C) Copyright IBM Corp., 2013 and provided pursuant to the Technology
 * Licensing Agreement between Google Inc. and International Business
 * Machines Corporation, IBM License Reference Number AA130103030256 and
 * confidentiality governed by the Parties’ Mutual Nondisclosure Agreement
 * number V032404DR, executed by the parties on November 6, 2007, and
 * Supplement V032404DR-3 dated August 16, 2012 (the “NDA”). */
#ifndef __CPU_H
#define __CPU_H

#include <processor.h>
#include <ccan/list/list.h>
#include <lock.h>
#include <device.h>
#include <opal.h>

/*
 * cpu_thread is our internal structure representing each
 * thread in the system
 */

enum cpu_thread_state {
	cpu_state_no_cpu	= 0,	/* Nothing there */
	cpu_state_unknown,		/* In PACA, not called in yet */
	cpu_state_unavailable,		/* Not available */
	cpu_state_present,		/* Assumed to spin in asm entry */
	cpu_state_active,		/* Secondary called in */
	cpu_state_os,			/* Under OS control */
	cpu_state_disabled,		/* Disabled by us due to error */
	cpu_state_rvwinkle,		/* Doing an rvwinkle cycle */
};

struct cpu_job;

struct cpu_thread {
	uint32_t			pir;
	uint32_t			server_no;
	uint32_t			chip_id;
	bool				is_secondary;
	struct cpu_thread		*primary;
	enum cpu_thread_state		state;
	struct dt_node			*node;
	struct opal_machine_check_event	mc_event;
	struct trace_info		*trace;
	uint64_t			save_r1;

	struct lock			job_lock;
	struct list_head		job_queue;
};

/* This global is set to 1 to allow secondaries to callin,
 * typically set after the primary has allocated the cpu_thread
 * array and stacks
 */
extern unsigned long cpu_secondary_start;

/* Max PIR in the system */
extern unsigned int cpu_max_pir;

/* Max # of threads per core */
extern unsigned int cpu_thread_count;

/* Boot CPU. */
extern struct cpu_thread *boot_cpu;

/* Initialize CPUs */
void init_boot_cpu(void);
void init_all_cpus(void);

/* This brings up our secondaries */
extern void cpu_bringup(void);

/* This is called by secondaries as they call in */
extern void cpu_callin(struct cpu_thread *cpu);

/* For cpus which fail to call in. */
extern void cpu_remove_node(const struct cpu_thread *t);

/* Find CPUs using different methods */
extern struct cpu_thread *find_cpu_by_chip_id(u32 chip_id);
extern struct cpu_thread *find_cpu_by_node(struct dt_node *cpu);
extern struct cpu_thread *find_cpu_by_server(u32 server_no);
extern struct cpu_thread *find_cpu_by_pir(u32 pir);

extern struct dt_node *get_cpu_node(u32 pir);

/* Iterator */
extern struct cpu_thread *first_cpu(void);
extern struct cpu_thread *next_cpu(struct cpu_thread *cpu);

/* WARNING: CPUs that have been picked up by the OS are no longer
 *          appearing as available and can not have jobs scheduled
 *          on them. Essentially that means that after the OS is
 *          fully started, all CPUs are seen as unavailable from
 *          this API standpoint.
 */

static inline bool cpu_is_available(struct cpu_thread *cpu)
{
	return cpu->state == cpu_state_active ||
		cpu->state == cpu_state_rvwinkle;
}

extern struct cpu_thread *first_available_cpu(void);
extern struct cpu_thread *next_available_cpu(struct cpu_thread *cpu);

#define for_each_cpu(cpu)	\
	for (cpu = first_cpu(); cpu; cpu = next_cpu(cpu))

#define for_each_available_cpu(cpu)	\
	for (cpu = first_available_cpu(); cpu; cpu = next_available_cpu(cpu))

extern struct cpu_thread *first_available_core_in_chip(u32 chip_id);
extern struct cpu_thread *next_available_core_in_chip(struct cpu_thread *cpu, u32 chip_id);

#define for_each_available_core_in_chip(core, chip_id)	\
	for (core = first_available_core_in_chip(chip_id); core; \
		core = next_available_core_in_chip(core, chip_id))

/* Return the caller CPU (only after init_cpu_threads) */
register struct cpu_thread *__this_cpu asm("r13");
static inline struct cpu_thread *this_cpu(void)
{
	return __this_cpu;
}

/* Get the thread # of a cpu within the core */
static inline uint32_t cpu_get_thread_index(struct cpu_thread *cpu)
{
	return cpu->pir - cpu->primary->pir;
}

/* Get the core # of a cpu within the core */
extern uint32_t cpu_get_core_index(struct cpu_thread *cpu);

/* Get the PIR of thread 0 of the same core */
static inline uint32_t cpu_get_thread0(struct cpu_thread *cpu)
{
	return cpu->primary->pir;
}

static inline bool cpu_is_thread0(struct cpu_thread *cpu)
{
	return cpu->primary == cpu;
}

static inline bool cpu_is_sibling(struct cpu_thread *cpu1,
				  struct cpu_thread *cpu2)
{
	return cpu1->primary == cpu2->primary;
}

/* Called when some error condition requires disabling a core */
void cpu_disable_all_threads(struct cpu_thread *cpu);

/* Allocate & queue a job on target CPU */
extern struct cpu_job *__cpu_queue_job(struct cpu_thread *cpu,
				       void (*func)(void *data), void *data,
				       bool no_return);

static inline struct cpu_job *cpu_queue_job(struct cpu_thread *cpu,
					    void (*func)(void *data),
					    void *data)
{
	return __cpu_queue_job(cpu, func, data, false);
}


/* Poll job status, returns true if completed */
extern bool cpu_poll_job(struct cpu_job *job);

/* Synchronously wait for a job to complete, this will
 * continue handling the FSP mailbox if called from the
 * boot CPU. Set free_it to free it automatically.
 */
extern void cpu_wait_job(struct cpu_job *job, bool free_it);

/* Free a CPU job, only call on a completed job */
extern void cpu_free_job(struct cpu_job *job);

/* Called by init to process jobs */
extern void cpu_process_jobs(void);

static inline void cpu_give_self_os(void)
{
	__this_cpu->state = cpu_state_os;
}

extern void *cpu_stack_bottom(unsigned int pir);
extern void *cpu_stack_top(unsigned int pir);

#endif /* __CPU_H */
