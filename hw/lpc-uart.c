#include <skiboot.h>
#include <lpc.h>
#include <console.h>

/* UART reg defs */
#define REG_RBR		0
#define REG_THR		0
#define REG_DLL		0
#define REG_IER		1
#define REG_DLM		1
#define REG_FCR		2
#define REG_IIR		2
#define REG_LCR		3
#define REG_MCR		4
#define REG_LSR		5
#define REG_MSR		6
#define REG_SCR		7

#define LSR_DR   0x01  /* Data ready */
#define LSR_OE   0x02  /* Overrun */
#define LSR_PE   0x04  /* Parity error */
#define LSR_FE   0x08  /* Framing error */
#define LSR_BI   0x10  /* Break */
#define LSR_THRE 0x20  /* Xmit holding register empty */
#define LSR_TEMT 0x40  /* Xmitter empty */
#define LSR_ERR  0x80  /* Error */

#define LCR_DLAB 0x80  /* DLL access */

static uint32_t uart_base;

static uint8_t uart_read(unsigned int reg)
{
	uint8_t val;
	int rc;

	rc = lpc_read8(uart_base + reg, &val);
	if (rc) {
		printf("UART: LPC Read error %d\n", rc);
		/* XXX Disable UART ? */
		return 0xff;
	}
	return val;
}

static void uart_write(unsigned int reg, uint8_t val)
{
	int rc;

	rc = lpc_write8(uart_base + reg, val);
	if (rc) {
		printf("UART: LPC Write error %d\n", rc);
		/* XXX Disable UART ? */
	}
}

static size_t uart_con_write(const char *buf, size_t len)
{
	size_t written = 0;

	while(written < len) {
		while ((uart_read(REG_LSR) & LSR_THRE) == 0)
			/* wait for idle */;
		uart_write(REG_THR, buf[written++]);
	};

	return written;
}

static size_t uart_con_read(char *buf, size_t len)
{
	size_t read_cnt = 0;

	while(read_cnt < len && (uart_read(REG_LSR) & LSR_DR) != 0)
		buf[read_cnt++] = uart_read(REG_RBR);

	return read_cnt;
}

static struct con_ops uart_con_driver = {
	.read = uart_con_read,
	.write = uart_con_write
};

static void uart_init_hw(unsigned int speed, unsigned int clock)
{
	unsigned int dll = (clock / 16) / speed;

	uart_write(REG_LCR, 0x00);
	uart_write(REG_IER, 0xff);
	uart_write(REG_IER, 0x00);
	uart_write(REG_LCR, LCR_DLAB);
	uart_write(REG_DLL, dll & 0xff);
	uart_write(REG_DLM, dll >> 8);
	uart_write(REG_LCR, 0x03); /* 8N1 */
	uart_write(REG_MCR, 0x03); /* RTS/DTR */
	uart_write(REG_FCR, 0x07); /* clear & en. fifos */
}

void uart_init(void)
{
	if (!lpc_present())
		return;

	/* XXX Assume UART is on LPC. Fix that when HB adds it
	 * to the device-tree
	 */
	uart_base = 0xd0000000l;

	printf("UART: RBR=%x\n", uart_read(0));
	printf("UART: IER=%x\n", uart_read(1));
	printf("UART: FCR=%x\n", uart_read(2));
	printf("UART: LCR=%x\n", uart_read(3));
	printf("UART: MCR=%x\n", uart_read(4));
	printf("UART: LSR=%x\n", uart_read(5));
	printf("UART: MSR=%x\n", uart_read(6));
	printf("UART: SCR=%x\n", uart_read(7));

	uart_init_hw(9600, 1843200);

	uart_write(0, 'F');
	uart_write(0, 'O');
	uart_write(0, 'O');

	set_console(&uart_con_driver);
}
