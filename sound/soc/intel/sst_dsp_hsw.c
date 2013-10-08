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
#include <linux/firmware.h>

#include "sst_dsp.h"
#include "sst_dsp_priv.h"

#include <trace/events/sst.h>

static void hsw_free(struct sst_dsp *sst);

#define SST_HSW_FW_SIGNATURE_SIZE	4
#define SST_HSW_FW_SIGN			"$SST"
#define SST_HSW_FW_LIB_SIGN		"$LIB"

enum sst_ram_type {
	SST_HSW_IRAM	= 1,
	SST_HSW_DRAM	= 2,
};

struct dma_block_info {
	enum sst_ram_type	type;	/* IRAM/DRAM */
	u32			size;	/* Bytes */
	u32			ram_offset; /* Offset in I/DRAM */
	u32			rsvd;	/* Reserved field */
};

struct fw_module_info {
	u32 persistent_size;
	u32 scratch_size;
};

/*
 * struct fw_header - FW file headers
 *
 * @signature : FW signature
 * @modules : # of modules
 * @file_format : version of header format
 * @reserved : reserved fields
 */
struct fw_header {
	unsigned char signature[SST_HSW_FW_SIGNATURE_SIZE]; /* FW signature */
	u32 file_size; /* size of fw minus this header */
	u32 modules; /*  # of modules */
	u32 file_format; /* version of header format */
	u32 reserved[4];
};

struct fw_module_header {
	unsigned char signature[SST_HSW_FW_SIGNATURE_SIZE]; /* module signature */
	u32 mod_size; /* size of module */
	u32 blocks; /* # of blocks */
	u32 type; /* codec type, pp lib */
	u32 entry_point;
	struct fw_module_info info;
};

/**
 * sst_parse_module - Parse audio FW modules
 *
 * @module: FW module header
 *
 * Parses modules that need to be placed in SST IRAM and DRAM
 * returns error or 0 if module sizes are proper
 */
static int hsw_parse_module(struct sst_dsp *dsp, struct sst_fw *fw, 
	struct fw_module_header *module)
{
	struct dma_block_info *block;
	struct sst_module *mod;
	struct sst_module_template template;
	int count;
	void __iomem *ram;

	dev_dbg(dsp->dev, "module sign %s size 0x%x blocks 0x%x type 0x%x\n",
			module->signature, module->mod_size,
			module->blocks, module->type);
	dev_dbg(dsp->dev, "module entrypoint 0x%x\n", module->entry_point);
	dev_dbg(dsp->dev, "module persistent 0x%x scratch 0x%x\n",
		module->info.persistent_size, module->info.scratch_size);

	memset(&template, 0, sizeof(template));
	template.text_size = module->info.persistent_size;
	template.data_size = module->info.scratch_size;
	template.text_offset = module->entry_point;
	//u32 data_offset;
	template.text_fixed = true;
	template.data_fixed = true;

	block = (void *)module + sizeof(*module);

	mod = sst_module_new(fw, &template, NULL);
	if (mod == NULL)
		return -ENOMEM;

	for (count = 0; count < module->blocks; count++) {

		if (block->size <= 0) {
			dev_err(dsp->dev, "block size invalid\n");
			return -EINVAL;
		}

		switch (block->type) {
		case SST_HSW_IRAM:
			ram = dsp->addr.iram;
			break;
		case SST_HSW_DRAM:
			ram = dsp->addr.dram;
			break;
		default:
			dev_err(dsp->dev, "wrong ram type 0x%x in block0x%x\n",
					block->type, count);
			return -EINVAL;
		}

		dev_dbg(dsp->dev, "Copy block %d type 0x%x size 0x%x ==> ram %p offset 0x%x\n",
				count, block->type, block->size, ram, block->ram_offset);

		//sst_fw_copy(dsp, ram + block->ram_offset,
		//		(void *)block + sizeof(*block), block->size);

		sst_module_insert_section(mod, &dsp->bmap,
			block->ram_offset, block->size,
			(void *)block + sizeof(*block));

		block = (void *)block + sizeof(*block) + block->size;
	}
	return 0;
}
/**
 * sst_parse_fw_image - parse and load FW
 *
 * @sst_fw: pointer to audio fw
 *
 * This function is called to parse and download the FW image
 */
static int hsw_parse_fw_image(struct sst_fw *sst_fw, const struct firmware *fw)
{
	struct fw_header *header;
	struct fw_module_header *module;
	struct sst_dsp *dsp = sst_fw->dsp;
	int ret, count;

	//sst_fw = sst_fw_new(dsp, fw, NULL);
	///if (sst_fw == NULL)
	//	return -ENOMEM;

	/* Read the header information from the data pointer */
	header = (struct fw_header *)fw->data;

	/* verify FW */
	if ((strncmp(header->signature, SST_HSW_FW_SIGN, 4) != 0) ||
			(fw->size != header->file_size + sizeof(*header))) {
		/* Invalid FW signature */
		dev_err(dsp->dev, "Invalid FW sign/filesize mismatch\n");
		return -EINVAL;
	}

	dev_dbg(dsp->dev, "header sign=%4s size=0x%x modules=0x%x fmt=0x%x size=%zu\n",
			header->signature, header->file_size, header->modules,
			header->file_format, sizeof(*header));

	module = (void *)fw->data + sizeof(*header);
	for (count = 0; count < header->modules; count++) {
		/* module */
		ret = hsw_parse_module(dsp, sst_fw, module);
		if (ret < 0) {
			dev_err(dsp->dev, "invalid module %d\n", count);
			return ret;
		}
		module = (void *)module + sizeof(*module) + module->mod_size;
	}

	return 0;
}

static void dump_shim(struct sst_dsp *sst)
{
	int i;

	for (i = 0; i <= 0xF0; i += 4)
		printk(KERN_ERR "shim 0x%2.2x value 0x%8.8x\n", i,
			sst_dsp_shim_read_unlocked(sst, i));

	for (i = 0xa0; i <= 0xac; i += 4)
		printk(KERN_ERR "vendor 0x%2.2x value 0x%8.8x\n", i,
			readl(sst->addr.pci_cfg + i));
}

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

	/* disable all clock gating */
	writel(0x0, sst->addr.pci_cfg + 0xa8);

	/* set DSP to RUN */
	sst_dsp_shim_update_bits(sst, SST_CSR, SST_CSR_STALL, 0x0);
}

static void hsw_reset(struct sst_dsp *sst)
{
	/* put DSP into reset and stall */
	sst_dsp_shim_update_bits(sst, SST_CSR,
		SST_CSR_RST | SST_CSR_STALL, SST_CSR_RST | SST_CSR_STALL);

	/* keep in reset for 10ms */
	mdelay(10);

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

struct sst_hsw_memregion {
	u32 start;
	u32 end;
	int blocks;
	enum sst_mem_type type;
};

static const struct sst_hsw_memregion region[] = {
	{0x00000, 0x40000, 8, SST_MEM_DRAM}, /* D-SRAM0 - 8 * 32kB */
	{0x40000, 0x80000, 8, SST_MEM_DRAM}, /* D-SRAM1 - 8 * 32kB */
	{0x80000, 0xE0000, 12, SST_MEM_IRAM}, /* I-SRAM - 12 * 32kB */
};	

/* enable 32kB memory block */
static int hsw_block_enable(struct sst_mem_block *block)
{
	return 0; // TODO: Implement
}

/* disable 32kB memory block */
static int hsw_block_disable(struct sst_mem_block *block)
{
	return 0; // TODO: Implement
}

static struct sst_block_ops sst_hsw_ops = {
	.enable = hsw_block_enable,
	.disable = hsw_block_disable,
};

static int hsw_init(struct sst_dsp *sst, struct sst_pdata *pdata)
{
	struct device *dev;
	int ret = -ENODEV, i, j;
	u32 offset, size;

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

	/* register the DSP memory blocks - ideally we should get this from ACPI */
	for (i = 0; i < ARRAY_SIZE(region); i++) {
		offset = region[i].start;
		size = (region[i].end - region[i].start) / region[i].blocks;

		for (j = 0; j < region[i].blocks; j++) {
			sst_mem_block_register(&sst->bmap, offset, size,
				region[i].type, &sst_hsw_ops, sst);
			offset += size;
		}
	}

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
	.parse_fw = hsw_parse_fw_image,
};
