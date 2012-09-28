#include <stddef.h>
#include <types.h>
#include <skiboot.h>
#include <spira.h>
#include <processor.h>
#include <cpu.h>
#include <stack.h>

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define OFFSET(sym, str, mem) \
	DEFINE(sym, offsetof(struct str, mem))

int main(void)
{
	OFFSET(SPIRA_ACTUAL_SIZE, spira, reserved);

	DEFINE(CPUTHREAD_SIZE, sizeof(struct cpu_thread));
	OFFSET(CPUTHREAD_PIR, cpu_thread, pir);
	OFFSET(CPUTHREAD_STACK, cpu_thread, stack);

	OFFSET(STACK_TYPE,	stack_frame, type);
	OFFSET(STACK_GPR0,	stack_frame, gpr[0]);
	OFFSET(STACK_GPR1,	stack_frame, gpr[1]);
	OFFSET(STACK_GPR2,	stack_frame, gpr[2]);
	OFFSET(STACK_GPR3,	stack_frame, gpr[3]);
	OFFSET(STACK_GPR4,	stack_frame, gpr[4]);
	OFFSET(STACK_GPR5,	stack_frame, gpr[5]);
	OFFSET(STACK_GPR6,	stack_frame, gpr[6]);
	OFFSET(STACK_GPR7,	stack_frame, gpr[7]);
	OFFSET(STACK_GPR8,	stack_frame, gpr[8]);
	OFFSET(STACK_GPR9,	stack_frame, gpr[9]);
	OFFSET(STACK_GPR10,	stack_frame, gpr[10]);
	OFFSET(STACK_GPR11,	stack_frame, gpr[11]);
	OFFSET(STACK_GPR12,	stack_frame, gpr[12]);
	OFFSET(STACK_GPR13,	stack_frame, gpr[13]);
	OFFSET(STACK_GPR14,	stack_frame, gpr[14]);
	OFFSET(STACK_GPR15,	stack_frame, gpr[15]);
	OFFSET(STACK_GPR16,	stack_frame, gpr[16]);
	OFFSET(STACK_GPR17,	stack_frame, gpr[17]);
	OFFSET(STACK_GPR18,	stack_frame, gpr[18]);
	OFFSET(STACK_GPR19,	stack_frame, gpr[19]);
	OFFSET(STACK_GPR20,	stack_frame, gpr[20]);
	OFFSET(STACK_GPR21,	stack_frame, gpr[21]);
	OFFSET(STACK_GPR22,	stack_frame, gpr[22]);
	OFFSET(STACK_GPR23,	stack_frame, gpr[23]);
	OFFSET(STACK_GPR24,	stack_frame, gpr[24]);
	OFFSET(STACK_GPR25,	stack_frame, gpr[25]);
	OFFSET(STACK_GPR26,	stack_frame, gpr[26]);
	OFFSET(STACK_GPR27,	stack_frame, gpr[27]);
	OFFSET(STACK_GPR28,	stack_frame, gpr[28]);
	OFFSET(STACK_GPR29,	stack_frame, gpr[29]);
	OFFSET(STACK_GPR30,	stack_frame, gpr[30]);
	OFFSET(STACK_GPR31,	stack_frame, gpr[31]);

	OFFSET(STACK_CR,	stack_frame, cr);
	OFFSET(STACK_XER,	stack_frame, xer);
	OFFSET(STACK_CTR,	stack_frame, ctr);
	OFFSET(STACK_LR,	stack_frame, lr);
	OFFSET(STACK_PC,	stack_frame, pc);
	OFFSET(STACK_CFAR,	stack_frame, cfar);
	DEFINE(STACK_FRAMESIZE,	sizeof(struct stack_frame));

	return 0;
}
