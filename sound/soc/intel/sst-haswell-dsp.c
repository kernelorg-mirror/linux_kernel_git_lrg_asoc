/*
 * Intel Haswell SST DSP driver
 *
 * Copyright (C) 2013, Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define DEBUG

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
#include <linux/pm_runtime.h>

#include <linux/acpi.h>
#include <acpi/acpi_bus.h>

#include "sst-dsp.h"
#include "sst-dsp-priv.h"
#include "sst-haswell-ipc.h"

#include <trace/events/hswadsp.h>

#define SST_HSW_FW_SIGNATURE_SIZE	4
#define SST_HSW_FW_SIGN			"$SST"
#define SST_HSW_FW_LIB_SIGN		"$LIB"

#define SST_WPT_SHIM_OFFSET	0xFB000
#define SST_LP_SHIM_OFFSET	0xE7000
#define SST_WPT_IRAM_OFFSET	0xA0000
#define SST_LP_IRAM_OFFSET	0x80000

enum sst_ram_type {
	SST_HSW_IRAM	= 1,
	SST_HSW_DRAM	= 2,
	SST_HSW_REGS	= 3,
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
	u16 padding;
	u16 type; /* codec type, pp lib */
	u32 entry_point;
	struct fw_module_info info;
};

static void hsw_free(struct sst_dsp *sst);

/* Internal Generic SST IO functions - can be overidden */
void shim_write(void __iomem *addr, u32 offset, u32 value)
{
	writel(value, addr + offset);
}

u32 shim_read(void __iomem *addr, u32 offset)
{
	return readl(addr + offset);
}

void shim_write64(void __iomem *addr, u32 offset, u64 value)
{
	memcpy_toio(addr + offset, &value, sizeof(value));
}

u64 shim_read64(void __iomem *addr, u32 offset)
{
	u64 val;

	memcpy_fromio(&val, addr + offset, sizeof(val));
	return val;
}

/* Internal Generic SST memcpy functions - can be overidden */
static inline void _sst_memcpy_toio_32(volatile u32 __iomem *dest,
	u32 *src, size_t bytes)
{
	int i, words = bytes >> 2;

	for (i = 0; i < words; i++)
		writel(src[i], dest + i);
}

static inline void _sst_memcpy_fromio_32(u32 *dest,
	const volatile __iomem u32 *src, size_t bytes)
{
	int i, words = bytes >> 2;

	for (i = 0; i < words; i++)
		dest[i] = readl(src + i);
}

void sst_memcpy_toio_32(struct sst_dsp *sst,
	void __iomem *dest, void *src, size_t bytes)
{
	_sst_memcpy_toio_32(dest, src, bytes);
}

void sst_memcpy_fromio_32(struct sst_dsp *sst, void *dest,
	void __iomem *src, size_t bytes)
{
	_sst_memcpy_fromio_32(dest, src, bytes);
}

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
	struct sst_module_data block_data;
	struct sst_module_template template;
	int count;
	void __iomem *ram;

	dev_dbg(dsp->dev, "module sign %s size 0x%x blocks 0x%x type 0x%x\n",
			module->signature, module->mod_size,
			module->blocks, module->type);
	dev_dbg(dsp->dev, "module entrypoint 0x%x\n", module->entry_point);
	dev_dbg(dsp->dev, "module persistent 0x%x scratch 0x%x\n",
		module->info.persistent_size, module->info.scratch_size);

	/* TODO: allowed module types need to be configurable */
	if (module->type != SST_HSW_MODULE_BASE_FW
		&& module->type != SST_HSW_MODULE_PCM_SYSTEM
		&& module->type != SST_HSW_MODULE_PCM
		&& module->type != SST_HSW_MODULE_PCM_REFERENCE
		&& module->type != SST_HSW_MODULE_PCM_CAPTURE
		&& module->type != SST_HSW_MODULE_LPAL)
		return 0;

	memset(&template, 0, sizeof(template));
	template.id = module->type;
	template.entry = module->entry_point;
	template.p.size = module->info.persistent_size;
	template.p.type = SST_MEM_DRAM;
	template.p.data_type = SST_DATA_P;
	template.s.size = module->info.scratch_size;
	template.s.type = SST_MEM_DRAM;
	template.s.data_type = SST_DATA_S;

	mod = sst_module_new(fw, &template, NULL);
	if (mod == NULL)
		return -ENOMEM;

	block = (void *)module + sizeof(*module);

	for (count = 0; count < module->blocks; count++) {

		if (block->size <= 0) {
			dev_err(dsp->dev, "block %d size invalid\n", count);
			return -EINVAL;
		}

		switch (block->type) {
		case SST_HSW_IRAM:
			ram = dsp->addr.ram;
			block_data.offset = block->ram_offset
				+ dsp->addr.iram_offset;
			block_data.type = SST_MEM_IRAM;
			break;
		case SST_HSW_DRAM:
			ram = dsp->addr.ram;
			block_data.offset = block->ram_offset + 0x0;
			block_data.type = SST_MEM_DRAM;
			break;
		default:
			dev_err(dsp->dev, "wrong ram type 0x%x in block0x%x\n",
					block->type, count);
			return -EINVAL;
		}

		block_data.size = block->size;
		block_data.data_type = SST_DATA_M;
		block_data.data = (void *)block + sizeof(*block);
		block_data.data_offset = block_data.data - fw->dma_buf;

		dev_dbg(dsp->dev, "Copy block %d type 0x%x size 0x%x ==> ram %p offset 0x%x\n",
				count, block->type, block->size, ram, block->ram_offset);

		sst_module_insert_fixed_block(mod, &block_data);

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
		/* Invalid FW signature */
		dev_err(dsp->dev, "Invalid FW sign/filesize mismatch\n");
		return -EINVAL;
	}

	dev_dbg(dsp->dev, "header sign=%4s size=0x%x modules=0x%x fmt=0x%x size=%zu\n",
			header->signature, header->file_size, header->modules,
			header->file_format, sizeof(*header));

	/* free persistent/scratch mem regions */
	sst_mem_block_drv_managed_free(dsp);

	module = (void *)sst_fw->dma_buf + sizeof(*header);
	for (count = 0; count < header->modules; count++) {
		/* module */
		ret = hsw_parse_module(dsp, sst_fw, module);
		if (ret < 0) {
			dev_err(dsp->dev, "invalid module %d\n", count);
			return ret;
		}
		module = (void *)module + sizeof(*module) + module->mod_size;
	}

	/* allocate persistent/scratch mem regions */
	sst_mem_block_drv_managed_alloc(dsp);

	return 0;
}

static void dump_shim(struct sst_dsp *sst)
{
	int i;

	for (i = 0; i <= 0xF0; i += 4)
		if (sst_dsp_shim_read_unlocked(sst, i))
			printk(KERN_ERR "shim 0x%2.2x value 0x%8.8x\n", i,
				sst_dsp_shim_read_unlocked(sst, i));

	for (i = 0x00; i <= 0xff; i += 4)
		if (readl(sst->addr.pci_cfg + i))
			printk(KERN_ERR "pci 0x%2.2x value 0x%8.8x\n", i,
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

	/* enable DMA engine 0,1 all channels to access host memory */
	sst_dsp_shim_update_bits(sst, SST_HDMC,
		SST_HDMC_HDDA1(0xff)  | SST_HDMC_HDDA0(0xff),
		SST_HDMC_HDDA1(0xff) | SST_HDMC_HDDA0(0xff));

	/* disable all clock gating */
	writel(0x0, sst->addr.pci_cfg + 0xa8);

	/* set DSP to RUN */
	sst_dsp_shim_update_bits(sst, SST_CSR, SST_CSR_STALL, 0x0);

	dump_shim(sst);
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

struct sst_adsp_memregion {
	u32 start;
	u32 end;
	int blocks;
	enum sst_mem_type type;
};

/* lynx point ADSP mem regions */
static const struct sst_adsp_memregion lp_region[] = {
	{0x00000, 0x40000, 8, SST_MEM_DRAM}, /* D-SRAM0 - 8 * 32kB */
	{0x40000, 0x80000, 8, SST_MEM_DRAM}, /* D-SRAM1 - 8 * 32kB */
	{0x80000, 0xE0000, 12, SST_MEM_IRAM}, /* I-SRAM - 12 * 32kB */
};

/* wild cat point ADSP mem regions */
static const struct sst_adsp_memregion wpt_region[] = {
	{0x00000, 0x40000, 8, SST_MEM_DRAM}, /* D-SRAM0 - 8 * 32kB */
	{0x40000, 0x80000, 8, SST_MEM_DRAM}, /* D-SRAM1 - 8 * 32kB */
	{0x80000, 0xA0000, 4, SST_MEM_DRAM}, /* D-SRAM2 - 4 * 32kB */
	{0xA0000, 0xF0000, 10, SST_MEM_IRAM}, /* I-SRAM - 10 * 32kB */
};

static int hsw_acpi_resource_map(struct sst_dsp *sst, struct sst_pdata *pdata)
{
	dev_dbg(sst->dev, "initialising audio DSP ACPI device\n");

	/* ADSP DRAM & IRAM */
	sst->addr.ram_base = pdata->lpe_base;
	sst->addr.ram = ioremap(pdata->lpe_base, pdata->lpe_size);
	if (!sst->addr.ram)
		return -ENODEV;

	/* ADSP PCI MMIO config space */
	sst->addr.pci_cfg = ioremap(pdata->pcicfg_base, pdata->pcicfg_size);
	if (!sst->addr.pci_cfg) {
		iounmap(sst->addr.ram);
		return -ENODEV;
	}

	/* SST Shim */
	sst->addr.shim = sst->addr.ram + sst->addr.shim_offset;

	return 0;
}

static u32 hsw_block_get_bit(struct sst_mem_block *block)
{
	u32 bit = 0, shift = 0;

	switch (block->type) {
	case SST_MEM_DRAM:
		shift = 16;
		break;
	case SST_MEM_IRAM:
		shift = 6;
		break;
	default:
		return 0;
	}

	bit = 1 << (block->index + shift);

	return bit;
}

/* enable 32kB memory block */
static int hsw_block_enable(struct sst_mem_block *block)
{
	struct sst_dsp *sst = block->dsp;
	u32 bit, val;

	dev_dbg(block->dsp->dev, "enabled block %d type %d at offset 0x%x\n",
		block->index, block->type, block->offset);

	val = readl(sst->addr.pci_cfg + SST_VDRTCTL0);
	bit = hsw_block_get_bit(block);
	writel(val & ~bit, sst->addr.pci_cfg + SST_VDRTCTL0);

	/* wait 18 DSP clock ticks */
	udelay(10);

	return 0;
}

/* disable 32kB memory block */
static int hsw_block_disable(struct sst_mem_block *block)
{
	struct sst_dsp *sst = block->dsp;
	u32 bit, val;

	dev_dbg(block->dsp->dev, "disabled block %d type %d at offset 0x%x\n",
		block->index, block->type, block->offset);

	val = readl(sst->addr.pci_cfg + SST_VDRTCTL0);
	bit = hsw_block_get_bit(block);
	writel(val | bit, sst->addr.pci_cfg + SST_VDRTCTL0);

	return 0;
}

static struct sst_block_ops sst_hsw_ops = {
	.enable = hsw_block_enable,
	.disable = hsw_block_disable,
};

/* set D0 and test that DSP shim is cleared */
static int hsw_set_d0(struct sst_dsp *sst)
{
	int tries = 10;
	u32 reg;

	/* Set D0 state */
	reg = readl(sst->addr.pci_cfg + 0x84);
	writel(reg & ~0x3, sst->addr.pci_cfg + 0x84);

	/* check that ADSP shim is in D0 */
	while (tries--) {
		reg = sst_dsp_shim_read(sst, SST_CSR);
		if (reg != 0xffffffff)
			return 0;

		msleep(1);
	}

	return -ENODEV;
}

static int hsw_init(struct sst_dsp *sst, struct sst_pdata *pdata)
{
	const struct sst_adsp_memregion *region;
	struct device *dev;
	int ret = -ENODEV, i, j, region_count;
	u32 offset, size;

	dev = sst->dev;

	pm_runtime_get(sst->dev);

	switch (sst->id) {
	case SST_DEV_ID_LYNX_POINT:
		region = lp_region;
		region_count = ARRAY_SIZE(lp_region);
		sst->addr.iram_offset = SST_LP_IRAM_OFFSET;
		sst->addr.shim_offset = SST_LP_SHIM_OFFSET;
		break;
	case SST_DEV_ID_WILDCAT_POINT:
		region = wpt_region;
		region_count = ARRAY_SIZE(wpt_region);
		sst->addr.iram_offset = SST_WPT_IRAM_OFFSET;
		sst->addr.shim_offset = SST_WPT_SHIM_OFFSET;
		break;
	default:
		dev_err(dev, "failed to get mem resources\n");
		return ret;
	}

	ret = hsw_acpi_resource_map(sst, pdata);
	if (ret < 0) {
		dev_err(dev, "failed to map resources\n");
		return ret;
	}

	/* check that the DSP can enter D0 */
	ret = hsw_set_d0(sst);
	if (ret < 0) {
		dev_err(dev, "failed to set DSP D0 and reset SHIM\n");
		return ret;
	}

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	/* Enable Interrupt from both sides */
	sst_dsp_shim_update_bits(sst, SST_IMRX, 0x3, 0x0);
	sst_dsp_shim_update_bits(sst, SST_IMRD, (0x3 | 0x1 << 16 | 0x3 << 21), 0x0);

	/* register the DSP memory blocks - ideally we should get this from ACPI */
	for (i = 0; i < region_count; i++) {
		offset = region[i].start;
		size = (region[i].end - region[i].start) / region[i].blocks;

		/* register individual memory blocks */
		for (j = 0; j < region[i].blocks; j++) {
			sst_mem_block_register(sst, offset, size,
				region[i].type, &sst_hsw_ops, j, sst);
			offset += size;
		}
	}
	/* set default power gating mask */
	writel(0x0, sst->addr.pci_cfg + SST_VDRTCTL0);

	return 0;
}

static void hsw_free(struct sst_dsp *sst)
{
	sst_mem_block_unregister_all(sst);
	iounmap(sst->addr.ram);
	iounmap(sst->addr.pci_cfg);
}

struct sst_ops haswell_ops = {
	.reset = hsw_reset,
        .boot = hsw_boot,
        .write = shim_write,
        .read = shim_read,
        .write64 = shim_write64,
        .read64 = shim_read64,
	.ram_read = sst_memcpy_fromio_32,
	.ram_write = sst_memcpy_toio_32,
	.irq_handler = hsw_irq,
	.init = hsw_init,
	.free = hsw_free,
	.parse_fw = hsw_parse_fw_image,
};
