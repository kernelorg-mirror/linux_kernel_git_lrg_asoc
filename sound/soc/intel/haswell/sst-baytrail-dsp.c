/*
 * Intel Baytrail SST DSP driver
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>

#include "../common/sst-dsp.h"
#include "../common/sst-dsp-priv.h"
#include "../haswell/sst-haswell-ipc.h"

#include <trace/events/hswadsp.h>

#define SST_BYT_IRAM_OFFSET	0xC0000
#define SST_BYT_DRAM_OFFSET	0x100000
#define SST_BYT_SHIM_OFFSET	0x140000

#define SST_HSW_FW_SIGNATURE_SIZE	4
#define SST_HSW_FW_SIGN			"$SST"
#define SST_HSW_FW_LIB_SIGN		"$LIB"

#define SST_BYT_SHIM_OFFSET	0x140000
#define SST_BYT_DSP_DRAM_OFFSET	0x28000
#define SST_BYT_DSP_IRAM_OFFSET	0x00000

#define SST_SHIM_PM_REG		0x84

#define SST_HSW_IRAM	1
#define SST_HSW_DRAM	2
#define SST_HSW_REGS	3

struct dma_block_info {
	__le32 type;		/* IRAM/DRAM */
	__le32 size;		/* Bytes */
	__le32 ram_offset;	/* Offset in I/DRAM */
	__le32 rsvd;		/* Reserved field */
} __attribute__((packed));

struct fw_module_info {
	__le32 persistent_size;
	__le32 scratch_size;
} __attribute__((packed));

struct fw_header {
	unsigned char signature[SST_HSW_FW_SIGNATURE_SIZE]; /* FW signature */
	__le32 file_size;		/* size of fw minus this header */
	__le32 modules;		/*  # of modules */
	__le32 file_format;	/* version of header format */
	__le32 reserved[4];
} __attribute__((packed));

struct fw_module_header {
	unsigned char signature[SST_HSW_FW_SIGNATURE_SIZE]; /* module signature */
	__le32 mod_size;	/* size of module */
	__le32 blocks;	/* # of blocks */
	__le16 padding;
	__le16 type;	/* codec type, pp lib */
	__le32 entry_point;
	struct fw_module_info info;
} __attribute__((packed));

static int hsw_parse_module(struct sst_dsp *dsp, struct sst_fw *fw,
	struct fw_module_header *module)
{
	struct dma_block_info *block;
	struct sst_module *mod;
	struct sst_module_template template;
	int count, ret;
	void __iomem *ram;

	/* TODO: allowed module types need to be configurable */
	if (module->type != SST_HSW_MODULE_BASE_FW
		&& module->type != SST_HSW_MODULE_PCM_SYSTEM
		&& module->type != SST_HSW_MODULE_PCM
		&& module->type != SST_HSW_MODULE_PCM_REFERENCE
		&& module->type != SST_HSW_MODULE_PCM_CAPTURE
		&& module->type != SST_HSW_MODULE_WAVES
		&& module->type != SST_HSW_MODULE_LPAL)
		return 0;

	dev_dbg(dsp->dev, "new module sign 0x%s size 0x%x blocks 0x%x type 0x%x\n",
		module->signature, module->mod_size,
		module->blocks, module->type);
	dev_dbg(dsp->dev, " entrypoint 0x%x\n", module->entry_point);
	dev_dbg(dsp->dev, " persistent 0x%x scratch 0x%x\n",
		module->info.persistent_size, module->info.scratch_size);

	memset(&template, 0, sizeof(template));
	template.id = module->type;
	template.entry = module->entry_point - 4;
	template.persistent_size = module->info.persistent_size;
	template.scratch_size = module->info.scratch_size;

	mod = sst_module_new(fw, &template, NULL);
	if (mod == NULL)
		return -ENOMEM;

	block = (void *)module + sizeof(*module);

	for (count = 0; count < module->blocks; count++) {

		if (block->size <= 0) {
			dev_err(dsp->dev,
				"error: block %d size invalid\n", count);
			sst_module_free(mod);
			return -EINVAL;
		}

		switch (block->type) {
		case SST_HSW_IRAM:
			ram = dsp->addr.lpe;
			mod->offset =
				block->ram_offset + dsp->addr.iram_offset;
			mod->type = SST_MEM_IRAM;
			break;
		case SST_HSW_DRAM:
		case SST_HSW_REGS:
			ram = dsp->addr.lpe;
			mod->offset = block->ram_offset + dsp->addr.dram_offset;
			mod->type = SST_MEM_DRAM;
			break;
		default:
			dev_err(dsp->dev, "error: bad type 0x%x for block 0x%x\n",
				block->type, count);
			sst_module_free(mod);
			return -EINVAL;
		}

		mod->size = block->size;
		mod->data = (void *)block + sizeof(*block);
		mod->data_offset = mod->data - fw->dma_buf;

		dev_dbg(dsp->dev, "module block %d type 0x%x "
			"size 0x%x ==> ram %p offset 0x%x\n",
			count, mod->type, block->size, ram,
			block->ram_offset);

		ret = sst_module_alloc_blocks(mod);
		if (ret < 0) {
			dev_err(dsp->dev, "error: could not allocate blocks for module %d\n",
				count);
			sst_module_free(mod);
			return ret;
		}

		block = (void *)block + sizeof(*block) + block->size;
	}
	mod->state = SST_MODULE_STATE_LOADED;

	return 0;
}

static int hsw_parse_fw_image(struct sst_fw *sst_fw)
{
	struct fw_header *header;
	struct fw_module_header *module;
	struct sst_dsp *dsp = sst_fw->dsp;
	int ret, count;

	/* Read the header information from the data pointer */
	header = (struct fw_header *)sst_fw->dma_buf;

	/* verify FW */
	if ((strncmp(header->signature, SST_HSW_FW_SIGN, 4) != 0) ||
		(sst_fw->size != header->file_size + sizeof(*header))) {
		dev_err(dsp->dev, "error: invalid fw sign/filesize mismatch got 0x%x expected 0x%lx\n",
			sst_fw->size, header->file_size + sizeof(*header));
		return -EINVAL;
	}

	dev_dbg(dsp->dev, "header size=0x%x modules=0x%x fmt=0x%x size=%zu\n",
		header->file_size, header->modules,
		header->file_format, sizeof(*header));

	/* parse each module */
	module = (void *)sst_fw->dma_buf + sizeof(*header);
	for (count = 0; count < header->modules; count++) {

		/* module */
		ret = hsw_parse_module(dsp, sst_fw, module);
		if (ret < 0) {
			dev_err(dsp->dev, "error: invalid module %d\n", count);
			return ret;
		}
		module = (void *)module + sizeof(*module) + module->mod_size;

	}

	return 0;
}

static void sst_byt_dump_shim(struct sst_dsp *sst)
{
	int i;
	u64 reg;

	for (i = 0; i <= 0xF0; i += 8) {
		reg = sst_dsp_shim_read64_unlocked(sst, i);
		if (reg)
			dev_dbg(sst->dev, "shim 0x%2.2x value 0x%16.16llx\n",
				i, reg);
	}

	for (i = 0x00; i <= 0xff; i += 4) {
		reg = readl(sst->addr.pci_cfg + i);
		if (reg)
			dev_dbg(sst->dev, "pci 0x%2.2x value 0x%8.8x\n",
				i, (u32)reg);
	}
}

static irqreturn_t sst_byt_irq(int irq, void *context)
{
	struct sst_dsp *sst = (struct sst_dsp *) context;
	u64 isr;
	int ret = IRQ_NONE;

	spin_lock(&sst->spinlock);

	/* Interrupt arrived, check src */
	isr = sst_dsp_shim_read64_unlocked(sst, SST_ISRX);
	if (isr & SST_ISRX_DONE) {
		//trace_sst_irq_done(isr,
		//	sst_dsp_shim_read_unlocked(sst, SST_IMRX));

		/* Mask Done interrupt before return */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IMRX,
			SST_IMRX_DONE, SST_IMRX_DONE);
		ret = IRQ_WAKE_THREAD;
	}

	if (isr & SST_ISRX_BUSY) {
		//trace_sst_irq_busy(isr,
		//	sst_dsp_shim_read_unlocked(sst, SST_IMRX));

		/* Mask Busy interrupt before return */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IMRX,
			SST_IMRX_BUSY, SST_IMRX_BUSY);
		ret = IRQ_WAKE_THREAD;
	}

	spin_unlock(&sst->spinlock);
	return ret;
}

static void sst_byt_reset(struct sst_dsp *sst);

static void byt_set_dsp_D3(struct sst_dsp *sst)
{
	u32 val;

	/* Set D3 state, delay 50 us */
	val = readl(sst->addr.pci_cfg + SST_PMCS);
	val |= SST_PMCS_PS_MASK;
	writel(val, sst->addr.pci_cfg + SST_PMCS);
	udelay(50);
}

static int byt_set_dsp_D0(struct sst_dsp *sst)
{

	int tries = 10;
	u32 reg;

	/* Set D0 state */
	reg = readl(sst->addr.pci_cfg + SST_PMCS);
	reg &= ~SST_PMCS_PS_MASK;
	writel(reg, sst->addr.pci_cfg + SST_PMCS);

	/* check that ADSP shim is enabled */
	while (tries--) {
		reg = readl(sst->addr.pci_cfg + SST_PMCS) & SST_PMCS_PS_MASK;
		if (reg == 0)
			goto finish;

		msleep(1);
	}

	return -ENODEV;

finish:
	/* Stall and reset core, set CSR */
	sst_byt_reset(sst);

	/* Enable Interrupt from both sides */
	sst_dsp_shim_update_bits(sst, SST_IMRX, (SST_IMRX_BUSY | SST_IMRX_DONE),
				 0x0);
	//sst_dsp_shim_update_bits(sst, SST_IMRD, (SST_IMRD_DONE | SST_IMRD_BUSY |
				//SST_IMRD_SSP0 | SST_IMRD_DMAC), 0x0);

	/* clear IPC registers */
	sst_dsp_shim_write(sst, SST_IPCX, 0x0);
	sst_dsp_shim_write(sst, SST_IPCD, 0x0);
	//sst_dsp_shim_write(sst, SST_PISR, 0xffff8438);
	//sst_dsp_shim_write(sst, 0xe0, 0x300a);

	return 0;
}

static void sst_byt_boot(struct sst_dsp *sst)
{
	int tries = 10;

	/* release stall and wait to unstall */
	sst_dsp_shim_update_bits64(sst, SST_CSR, SST_BYT_CSR_STALL, 0x0);
	while (tries--) {
		if (!(sst_dsp_shim_read64(sst, SST_CSR) &
		      SST_BYT_CSR_PWAITMODE))
			break;
		msleep(100);
	}
	if (tries < 0) {
		dev_err(sst->dev, "unable to start DSP\n");
		sst_byt_dump_shim(sst);
	}
}

static void sst_byt_reset(struct sst_dsp *sst)
{
	/* put DSP into reset, set reset vector and stall */
	sst_dsp_shim_update_bits64(sst, SST_CSR,
		SST_BYT_CSR_RST | SST_BYT_CSR_VECTOR_SEL | SST_BYT_CSR_STALL,
		SST_BYT_CSR_RST | SST_BYT_CSR_VECTOR_SEL | SST_BYT_CSR_STALL);

	udelay(10);

	sst_dsp_shim_write(sst, SST_PIMR, 0x0);

	/* take DSP out of reset and keep stalled for FW loading */
	sst_dsp_shim_update_bits64(sst, SST_CSR, SST_BYT_CSR_RST, 0);
}

static void sst_byt_stall(struct sst_dsp *sst)
{
	/* stall DSP */
	sst_dsp_shim_update_bits(sst, SST_CSR,
		SST_BYT_CSR_STALL, SST_BYT_CSR_STALL);
}

static void sst_byt_sleep(struct sst_dsp *sst)
{
	dev_dbg(sst->dev, "BYT_PM dsp runtime suspend\n");

	/* put DSP into reset and stall */
	sst_dsp_shim_update_bits(sst, SST_CSR,
		SST_BYT_CSR_RST | SST_BYT_CSR_STALL,
		SST_BYT_CSR_RST | SST_BYT_CSR_STALL);

	byt_set_dsp_D3(sst);
	dev_dbg(sst->dev, "BYT_PM dsp runtime suspend exit\n");
}

static int sst_byt_wake(struct sst_dsp *sst)
{
	int ret;

	dev_dbg(sst->dev, "BYT_PM dsp runtime resume\n");

	ret = byt_set_dsp_D0(sst);
	if (ret < 0)
		return ret;

	dev_dbg(sst->dev, "BYT_PM dsp runtime resume exit\n");

	return 0;
}

struct sst_adsp_memregion {
	u32 start;
	u32 end;
	int blocks;
	enum sst_mem_type type;
};

/* BYT test stuff */
static const struct sst_adsp_memregion byt_region[] = {
	{0xC0000, 0x100000, 8, SST_MEM_IRAM}, /* I-SRAM - 8 * 32kB */
	{0x100000, 0x140000, 8, SST_MEM_DRAM}, /* D-SRAM0 - 8 * 32kB */
};

static int sst_byt_resource_map(struct sst_dsp *sst, struct sst_pdata *pdata)
{

	sst->addr.lpe_base = pdata->lpe_base;
	sst->addr.lpe = ioremap(pdata->lpe_base, pdata->lpe_size);
	if (!sst->addr.lpe)
		return -ENODEV;

	/* ADSP PCI MMIO config space */
	sst->addr.pci_cfg = ioremap(pdata->pcicfg_base, pdata->pcicfg_size);
	if (!sst->addr.pci_cfg) {
		iounmap(sst->addr.lpe);
		return -ENODEV;
	}

	/* SST Shim */
	sst->addr.shim = sst->addr.lpe + sst->addr.shim_offset;
	sst->irq = pdata->irq;

	return 0;
}

static int sst_byt_init(struct sst_dsp *sst, struct sst_pdata *pdata)
{
	const struct sst_adsp_memregion *region;
	struct device *dev;
	int ret = -ENODEV, i, j, region_count;
	u32 offset, size;

	dev = sst->dev;

	switch (sst->id) {
	case SST_DEV_ID_BYT:
		region = byt_region;
		region_count = ARRAY_SIZE(byt_region);
		sst->addr.iram_offset = SST_BYT_IRAM_OFFSET;
		sst->addr.dram_offset = SST_BYT_DRAM_OFFSET;
		sst->addr.shim_offset = SST_BYT_SHIM_OFFSET;
		break;
	default:
		dev_err(dev, "failed to get mem resources\n");
		return ret;
	}

	ret = sst_byt_resource_map(sst, pdata);
	if (ret < 0) {
		dev_err(dev, "failed to map resources\n");
		return ret;
	}

	ret = dma_coerce_mask_and_coherent(sst->dma_dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	/* enable Interrupt from both sides */
	sst_dsp_shim_update_bits64(sst, SST_IMRX, 0x3, 0x0);
	sst_dsp_shim_update_bits64(sst, SST_IMRD, 0x3, 0x0);

	/* register DSP memory blocks - ideally we should get this from ACPI */
	for (i = 0; i < region_count; i++) {
		offset = region[i].start;
		size = (region[i].end - region[i].start) / region[i].blocks;

		/* register individual memory blocks */
		for (j = 0; j < region[i].blocks; j++) {
			sst_mem_block_register(sst, offset, size,
					       region[i].type, NULL, j, sst);
			offset += size;
		}
	}

	return 0;
}

static void sst_byt_free(struct sst_dsp *sst)
{
	sst_mem_block_unregister_all(sst);
	iounmap(sst->addr.lpe);
	iounmap(sst->addr.pci_cfg);
}

struct sst_ops sst_baytrail_ops = {
	.reset = sst_byt_reset,
	.stall = sst_byt_stall,
	.wake = sst_byt_wake,
	.sleep = sst_byt_sleep,
	.boot = sst_byt_boot,
	.write = sst_shim32_write,
	.read = sst_shim32_read,
	.write64 = sst_shim32_write64,
	.read64 = sst_shim32_read64,
	.ram_read = sst_memcpy_fromio_32,
	.ram_write = sst_memcpy_toio_32,
	.irq_handler = sst_byt_irq,
	.init = sst_byt_init,
	.free = sst_byt_free,
	.parse_fw = hsw_parse_fw_image,
};
