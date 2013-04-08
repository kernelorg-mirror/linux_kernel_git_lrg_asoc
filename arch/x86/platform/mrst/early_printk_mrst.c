/*
 * early_printk_mrst.c - early consoles for Intel MID platforms
 *
 * Copyright (c) 2008-2010, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

/*
 * This file implements two early consoles named mrst and hsu.
 * mrst is based on Maxim3110 spi-uart device, it exists in both
 * Moorestown and Medfield platforms, while hsu is based on a High
 * Speed UART device which only exists in the Medfield platform
 */

#include <linux/serial_reg.h>
#include <linux/serial_mfd.h>
#include <linux/kmsg_dump.h>
#include <linux/console.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/fixmap.h>
#include <asm/pgtable.h>
#include <asm/mrst.h>

#define MRST_REGBASE_SPI0		0xff128000
#define MRST_REGBASE_SPI1		0xff128400

static struct kmsg_dumper dw_dumper;
static int dumper_registered;

static void dw_kmsg_dump(struct kmsg_dumper *dumper,
			 enum kmsg_dump_reason reason)
{
	static char line[1024];
	size_t len;

	/* When run to this, we'd better re-init the HW */
	mid_spi_early_console_reset();

	while (kmsg_dump_get_line(dumper, true, line, sizeof(line), &len))
		mid_spi_early_console.write(&mid_spi_early_console, line, len);
}

void __init mrst_early_console_init(void)
{
	/* Default use SPI0 register for mrst */
	unsigned long spi_paddr = MRST_REGBASE_SPI0;

	/* Detect Penwell and use SPI1 */
	if (mrst_identify_cpu() == MRST_CPU_CHIP_PENWELL)
		spi_paddr = MRST_REGBASE_SPI1;
	mid_spi_early_console_init(spi_paddr);

	/* Register the kmsg dumper */
	if (!dumper_registered) {
		dw_dumper.dump = dw_kmsg_dump;
		kmsg_dump_register(&dw_dumper);
		dumper_registered = 1;
	}
}

/*
 * Following is the early console based on Medfield HSU (High
 * Speed UART) device.
 */
#define HSU_PORT_BASE		0xffa28080

static void __iomem *phsu;

void hsu_early_console_init(const char *s)
{
	unsigned long paddr, port = 0;
	u8 lcr;

	/*
	 * Select the early HSU console port if specified by user in the
	 * kernel command line.
	 */
	if (*s && !kstrtoul(s, 10, &port))
		port = clamp_val(port, 0, 2);

	paddr = HSU_PORT_BASE + port * 0x80;
	phsu = (void *)set_fixmap_offset_nocache(FIX_EARLYCON_MEM_BASE, paddr);

	/* Disable FIFO */
	writeb(0x0, phsu + UART_FCR);

	/* Set to default 115200 bps, 8n1 */
	lcr = readb(phsu + UART_LCR);
	writeb((0x80 | lcr), phsu + UART_LCR);
	writeb(0x18, phsu + UART_DLL);
	writeb(lcr,  phsu + UART_LCR);
	writel(0x3600, phsu + UART_MUL*4);

	writeb(0x8, phsu + UART_MCR);
	writeb(0x7, phsu + UART_FCR);
	writeb(0x3, phsu + UART_LCR);

	/* Clear IRQ status */
	readb(phsu + UART_LSR);
	readb(phsu + UART_RX);
	readb(phsu + UART_IIR);
	readb(phsu + UART_MSR);

	/* Enable FIFO */
	writeb(0x7, phsu + UART_FCR);
}

#define BOTH_EMPTY (UART_LSR_TEMT | UART_LSR_THRE)

static void early_hsu_putc(char ch)
{
	unsigned int timeout = 10000; /* 10ms */
	u8 status;

	while (--timeout) {
		status = readb(phsu + UART_LSR);
		if (status & BOTH_EMPTY)
			break;
		udelay(1);
	}

	/* Only write the char when there was no timeout */
	if (timeout)
		writeb(ch, phsu + UART_TX);
}

static void early_hsu_write(struct console *con, const char *str, unsigned n)
{
	int i;

	for (i = 0; i < n && *str; i++) {
		if (*str == '\n')
			early_hsu_putc('\r');
		early_hsu_putc(*str);
		str++;
	}
}

struct console early_hsu_console = {
	.name =		"earlyhsu",
	.write =	early_hsu_write,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};
