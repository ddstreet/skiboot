#include <skiboot.h>
#include <opal.h>
#include <stack.h>

void opal_table_init(void)
{
	struct opal_table_entry *s = &__opal_table_start;
	struct opal_table_entry *e = &__opal_table_end;
	extern void *opal_branch_table[];

	printf("OPAL table: %p .. %p, branch table: %p\n",
	       s, e, opal_branch_table);
	while(s < e) {
		opal_branch_table[s->token] = s->func;
		s++;
	}
}

long opal_bad_token(uint64_t token)
{
	prerror("OPAL: Called with bad token %lld !\n", token);

	return OPAL_PARAMETER;
}

void opal_trace_entry(struct stack_frame *eframe)
{
	printf("OPAL: Entry, token %lld args:\n", eframe->gpr[0]);
	printf("OPAL:  r3=%016llx\n", eframe->gpr[3]);
	printf("OPAL:  r4=%016llx\n", eframe->gpr[4]);
	printf("OPAL:  r5=%016llx\n", eframe->gpr[5]);
	printf("OPAL:  r6=%016llx\n", eframe->gpr[6]);
	printf("OPAL:  r7=%016llx\n", eframe->gpr[7]);
	printf("OPAL:  r8=%016llx\n", eframe->gpr[8]);
	printf("OPAL:  r9=%016llx\n", eframe->gpr[9]);
	printf("OPAL: r10=%016llx\n", eframe->gpr[10]);
	printf("OPAL: r11=%016llx\n", eframe->gpr[11]);
	printf("OPAL: caller LR: %016llx SP: %016llx\n",
	       eframe->lr, eframe->gpr[1]);
}

/* Test function */
static uint64_t opal_test_func(uint64_t arg)
{
	printf("OPAL: Test function called with arg 0x%llx\n", arg);

	return 0xfeedf00d;
}
opal_call(OPAL_TEST, opal_test_func);
