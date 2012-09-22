/*
 * Console IO routine for use by libc
 *
 * fd is the classic posix 0,1,2 (stdin, stdout, stderr)
 */
#include <skiboot.h>
#include <unistd.h>
#include <console.h>

static char *con_buf = (char *)INMEM_CON_START;
static size_t con_in;
static size_t con_out;
static struct con_ops *con_driver;

struct lock con_lock;

#ifdef MAMBO_CONSOLE
static void mambo_write(const char *buf, size_t count)
{
#define SIM_WRITE_CONSOLE_CODE	0
	register int c asm("r3") = 0; /* SIM_WRITE_CONSOLE_CODE */
	register unsigned long a1 asm("r4") = (unsigned long)buf;
	register unsigned long a2 asm("r5") = count;
	register unsigned long a3 asm("r6") = 0;
	asm volatile (".long 0x000eaeb0":"=r" (c):"r"(c), "r"(a1), "r"(a2),
		      "r"(a3));
}
#else
static void mambo_write(const char *buf __unused, size_t count __unused) { }
#endif /* MAMBO_CONSOLE */

/* Flush the console buffer into the driver, returns true
 * if there is more to go
 */
bool __flush_console(void)
{
	size_t req, len = 0;

	if (con_in == con_out || !con_driver)
		return false;

	if (con_out > con_in) {
		req = INMEM_CON_LEN - con_out;
		len = con_driver->write(con_buf + con_out, req);		
		con_out = (con_out + len) % INMEM_CON_LEN;
		if (len < req)
			goto bail;
	}
	if (con_out < con_in) {
		len = con_driver->write(con_buf + con_out, con_in - con_out);
		con_out = (con_out + len) % INMEM_CON_LEN;
	}
bail:
	return con_out != con_in;
}

bool flush_console(void)
{
	bool ret;

	lock(&con_lock);
	ret = __flush_console();
	unlock(&con_lock);

	return ret;
}

static void inmem_write(const char *buf, size_t count)
{
	while(count--) {
		con_buf[con_in++] = *(buf++);
		if (con_in >= INMEM_CON_LEN)
			con_in = 0;

		/* If head reaches tail, push tail around & drop chars */
		if (con_in == con_out)
			con_out = (con_in + 1) % INMEM_CON_LEN;
	}
}

ssize_t write(int fd __unused, const void *buf, size_t count)
{

	lock(&con_lock);

	mambo_write(buf, count);
	inmem_write(buf, count);

	__flush_console();

	unlock(&con_lock);

	return count;
}

ssize_t read(int fd __unused, void *buf __unused, size_t count __unused)
{
	return 0;
}

void set_console(struct con_ops *driver)
{
	con_driver = driver;
	if (driver)
		flush_console();
}
