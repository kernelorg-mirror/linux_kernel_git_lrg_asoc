/*
 * intel_mid_spi.c - SPI UART early consoles for Intel MID platforms
 *
 * Copyright (c) 2008-2012, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

/*
 * This file is originally implemented by Feng Tang <feng.tang@intel.com>.
 * It is moved here to co-exist with CONFIG_EARLY_PRINTK_ACPI.
 * Any questions you should contact the original author.
 */

#include <linux/serial_reg.h>
#include <linux/serial_mfd.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/intel_mid_early.h>
#include <asm/fixmap.h>

#define DW_SPI_TIMEOUT			0x200000
#define DW_SPI0_CLK_REG			0xff11d86c

/* Bit fields in CTRLR0 */
#define SPI_DFS_OFFSET			0

#define SPI_FRF_OFFSET			4
#define SPI_FRF_SPI			0x0
#define SPI_FRF_SSP			0x1
#define SPI_FRF_MICROWIRE		0x2
#define SPI_FRF_RESV			0x3

#define SPI_MODE_OFFSET			6
#define SPI_SCPH_OFFSET			6
#define SPI_SCOL_OFFSET			7
#define SPI_TMOD_OFFSET			8
#define	SPI_TMOD_TR			0x0		/* xmit & recv */
#define SPI_TMOD_TO			0x1		/* xmit only */
#define SPI_TMOD_RO			0x2		/* recv only */
#define SPI_TMOD_EPROMREAD		0x3		/* eeprom read mode */

#define SPI_SLVOE_OFFSET		10
#define SPI_SRL_OFFSET			11
#define SPI_CFS_OFFSET			12

/* Bit fields in SR, 7 bits */
#define SR_MASK				0x7f		/* cover 7 bits */
#define SR_BUSY				(1 << 0)
#define SR_TF_NOT_FULL			(1 << 1)
#define SR_TF_EMPT			(1 << 2)
#define SR_RF_NOT_EMPT			(1 << 3)
#define SR_RF_FULL			(1 << 4)
#define SR_TX_ERR			(1 << 5)
#define SR_DCOL				(1 << 6)

struct dw_spi_reg {
	u32	ctrl0;
	u32	ctrl1;
	u32	ssienr;
	u32	mwcr;
	u32	ser;
	u32	baudr;
	u32	txfltr;
	u32	rxfltr;
	u32	txflr;
	u32	rxflr;
	u32	sr;
	u32	imr;
	u32	isr;
	u32	risr;
	u32	txoicr;
	u32	rxoicr;
	u32	rxuicr;
	u32	msticr;
	u32	icr;
	u32	dmacr;
	u32	dmatdlr;
	u32	dmardlr;
	u32	idr;
	u32	version;

	/* Currently operates as 32 bits, though only the low 16 bits matter */
	u32	dr;
} __packed;

#define dw_readl(dw, name)		__raw_readl(&(dw)->name)
#define dw_writel(dw, name, val)	__raw_writel((val), &(dw)->name)

static u32 *pclk_spi0;
/* Always contains an accessible address, start with 0 */
static struct dw_spi_reg *pspi;
static int dw_spi_initialized;
static u32 dw_spi_regbase;

/* Translate char to a eligible word and send to max3110 */
static void max3110_write_data(char c)
{
	u16 data;

	data = 0x8000 | c;
	dw_writel(pspi, dr, data);
}

/* Set the ratio rate to 115200, 8n1, IRQ disabled */
static void max3110_write_config(void)
{
	u16 config;

	config = 0xc001;
	dw_writel(pspi, dr, config);
}

int mid_spi_early_console_reset(void)
{
	u32 ctrlr0 = 0;
	u32 spi0_cdiv;
	u32 freq; /* Freqency info only need be searched once */

	/* Base clk is 100 MHz, the actual clk = 100M / (clk_divider + 1) */
	pclk_spi0 = (void *)set_fixmap_offset_nocache(FIX_EARLYCON_MEM_BASE,
						      DW_SPI0_CLK_REG);
	spi0_cdiv = ((*pclk_spi0) & 0xe00) >> 9;
	freq = 100000000 / (spi0_cdiv + 1);

	pspi = (void *)set_fixmap_offset_nocache(FIX_EARLYCON_MEM_BASE,
						 dw_spi_regbase);

	/* Disable SPI controller */
	dw_writel(pspi, ssienr, 0);

	/* Set control param, 8 bits, transmit only mode */
	ctrlr0 = dw_readl(pspi, ctrl0);

	ctrlr0 &= 0xfcc0;
	ctrlr0 |= 0xf | (SPI_FRF_SPI << SPI_FRF_OFFSET)
		      | (SPI_TMOD_TO << SPI_TMOD_OFFSET);
	dw_writel(pspi, ctrl0, ctrlr0);

	/*
	 * Change the spi0 clk to comply with 115200 bps, use 100000 to
	 * calculate the clk dividor to make the clock a little slower
	 * than real baud rate.
	 */
	dw_writel(pspi, baudr, freq/100000);

	/* Disable all INT for early phase */
	dw_writel(pspi, imr, 0x0);

	/* Set the cs to spi-uart */
	dw_writel(pspi, ser, 0x2);

	/* Enable the HW, the last step for HW init */
	dw_writel(pspi, ssienr, 0x1);

	/* Set the default configuration */
	max3110_write_config();

	return 0;
}
EXPORT_SYMBOL(mid_spi_early_console_reset);

/* Slave select should be called in the read/write function */
static void dw_early_spi_putc(char c)
{
	unsigned int timeout;
	u32 sr;

	timeout = DW_SPI_TIMEOUT;
	/* Early putc needs to make sure the TX FIFO is not full */
	while (--timeout) {
		sr = dw_readl(pspi, sr);
		if (!(sr & SR_TF_NOT_FULL))
			cpu_relax();
		else
			break;
	}

	if (!timeout)
		pr_warn("mid-spi-early: timed out\n");
	else
		max3110_write_data(c);
}

/* Early SPI only uses polling mode */
static void dw_early_spi_write(struct console *con,
			       const char *str, unsigned n)
{
	int i;

	for (i = 0; i < n && *str; i++) {
		if (*str == '\n')
			dw_early_spi_putc('\r');
		dw_early_spi_putc(*str);
		str++;
	}
}

struct console mid_spi_early_console = {
	.name =		"earlymidspi",
	.write =	dw_early_spi_write,
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};

void __init mid_spi_early_console_init(u32 spi_paddr)
{
	if (dw_spi_initialized)
		return;
	dw_spi_regbase = spi_paddr;
	mid_spi_early_console_reset();
	dw_spi_initialized = 1;
}
