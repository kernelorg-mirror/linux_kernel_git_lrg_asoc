/*
 *  sst_dsp_hsw.c - Intel Haswell SST DSP driver
 *
 *  Copyright (C) 2013	Intel Corp
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */


#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/pci.h>

#include "sst_dsp.h"
#include "sst_dsp_priv.h"

#include <trace/events/sst.h>

static void hsw_free(struct sst_dsp *sst);

static irqreturn_t hsw_irq(int irq, void *context)
{
	struct sst_dsp *sst = (struct sst_dsp *) context;
	u32 isr;
	int ret = IRQ_NONE;

	/* Interrupt arrived, check src */
	isr = sst_dsp_shim_read_unlocked(sst, SST_ISRX);
	if (isr & SST_ISRX_DONE) {
		trace_sst_irq_done(isr, sst_dsp_shim_read_unlocked(sst, SST_IMRX));

		/* Mask Done interrupt before return */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IMRX,
			SST_IMRX_DONE, SST_IMRX_DONE);
		ret = IRQ_WAKE_THREAD;
	}

	if (isr & SST_ISRX_BUSY) {
		trace_sst_irq_busy(isr, sst_dsp_shim_read_unlocked(sst, SST_IMRX));

		/* Mask Busy interrupt before return */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IMRX,
			SST_IMRX_BUSY, SST_IMRX_BUSY);
		return IRQ_WAKE_THREAD;
	}

	return ret;
}

static void hsw_boot(struct sst_dsp *sst)
{
	/* select SSP1 19.2MHz base clock, SSP clock 0, turn off Low Power Clock */
	sst_dsp_shim_update_bits(sst, SST_CSR,
		SST_CSR_S1IOCS | SST_CSR_SBCS1 | SST_CSR_LPCS, 0x0);

	/* stall DSP core, set clk to 192/96Mhz */
	sst_dsp_shim_update_bits(sst,
		SST_CSR, SST_CSR_STALL | SST_CSR_DCS_MASK,
		SST_CSR_STALL | SST_CSR_DCS(4));

	/* Set 24MHz MCLK, prevent local clock gating, enable SSP0 clock */
	sst_dsp_shim_update_bits(sst, SST_CLKCTL,
		SST_CLKCTL_MASK | SST_CLKCTL_DCPLCG | SST_CLKCTL_SCOE0,
		SST_CLKCTL_MASK | SST_CLKCTL_DCPLCG | SST_CLKCTL_SCOE0);

	/* disable DMA finish function for SSP0 & SSP1 */
	sst_dsp_shim_update_bits(sst, SST_CSR2, SST_CSR2_SDFD_SSP1,
		SST_CSR2_SDFD_SSP1);

	/* enable DMA engine 0 channel 3 to access host memory */
	sst_dsp_shim_update_bits(sst, SST_HDMC,  SST_HDMC_HDDA0(0x8),
		SST_HDMC_HDDA0(0x8));

	/* set DSP to RUN */
	sst_dsp_shim_update_bits(sst, SST_CSR, SST_CSR_STALL, 0x0);
}

static void hsw_reset(struct sst_dsp *sst)
{
	/* put DSP into reset and stall */
	sst_dsp_shim_update_bits(sst, SST_CSR,
		SST_CSR_RST | SST_CSR_STALL, SST_CSR_RST | SST_CSR_STALL);

	/* take DSP out of reset and keep stalled for FW loading */
	sst_dsp_shim_update_bits(sst, SST_CSR,
		SST_CSR_RST | SST_CSR_STALL, SST_CSR_STALL);
}

static int hsw_acpi_resource_map(struct sst_dsp *sst, struct sst_pdata *pdata)
{
	dev_dbg(sst->dev, "initialising audio DSP ACPI device\n");

	/* DRAM */
	sst->addr.dram_base = pdata->address[0];
	sst->addr.dram_end = pdata->address[0] + pdata->length[0];
	sst->addr.dram = ioremap(pdata->address[0], pdata->length[0]);
	if (!sst->addr.dram)
		return -ENODEV;

	sst->addr.pci_cfg = ioremap(pdata->address[1], pdata->length[1]);
	if (!sst->addr.pci_cfg) {
		iounmap(sst->addr.dram);
		return -ENODEV;
	}

	/* SST Shim */
	sst->addr.shim = sst->addr.dram + 0xE7000;

	/* IRAM */
	sst->addr.iram_end = sst->addr.dram_base + 0xDFFFF;
	sst->addr.iram_base = sst->addr.dram_base + 0x80000;
	sst->addr.iram = sst->addr.dram + 0x80000;

	sst->irq = pdata->irq;

	return 0;
}

static u64 hsw_dmamask = DMA_BIT_MASK(32);

static int hsw_init(struct sst_dsp *sst, struct sst_pdata *pdata)
{
	struct device *dev;
	int ret = -ENODEV;

	dev = sst->dev;

	ret = hsw_acpi_resource_map(sst, pdata);
	if (ret < 0) {
		dev_err(dev, "failed to map resources\n");
		return ret;
	}

	if (!dev->dma_mask)
		dev->dma_mask = &hsw_dmamask;
	if (!dev->coherent_dma_mask)
		dev->coherent_dma_mask = DMA_BIT_MASK(32);

	/* Enable Interrupt from both sides */
	sst_dsp_shim_update_bits(sst, SST_IMRX, 0x3, 0x0);
	sst_dsp_shim_update_bits(sst, SST_IMRD, (0x3 | 0x1 << 16 | 0x3 << 21), 0x0);

	return 0;
}

static void hsw_free(struct sst_dsp *sst)
{
	iounmap(sst->addr.dram);
	iounmap(sst->addr.pci_cfg);
}

struct sst_ops hswult_ops = {
	.reset = hsw_reset,
        .boot = hsw_boot,
        .write = shim_write,
        .read = shim_read,
        .write64 = shim_write64,
        .read64 = shim_read64,
	.iram_read = sst_memcpy_fromio_32,
	.dram_read = sst_memcpy_fromio_32,
	.iram_write = sst_memcpy_toio_32,
	.dram_write = sst_memcpy_toio_32,
	.irq_handler = hsw_irq,
	.init = hsw_init,
	.free = hsw_free,
};
