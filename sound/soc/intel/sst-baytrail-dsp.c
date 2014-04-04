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

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>

#include <uapi/linux/elf.h>

#include "sst-dsp.h"
#include "sst-dsp-priv.h"
#include "sst-baytrail-ipc.h"

#define SST_BYT_FW_SIGNATURE_SIZE	4
#define SST_BYT_FW_SIGN			"$SST"

#define SST_BYT_IRAM_OFFSET	0xC0000
#define SST_BYT_DRAM_OFFSET	0x100000
#define SST_BYT_SHIM_OFFSET	0x140000

#define SST_BYT_IRAM_PHY_START	0xff2c0000
#define SST_BYT_IRAM_PHY_END	0xff2d4000 /* 0x14000 - 81k differs from doc */
#define SST_BYT_DRAM_PHY_START	0xff300000
#define SST_BYT_DRAM_PHY_END	0xff320000 /* 128k */
#define SST_BYT_IMR_VIRT_START	0xc0000000 /* virtual addr in LPE */
#define SST_BYT_IMR_VIRT_END	0xc01fffff /* 2Mb */

#define SST_BYT_DMA0_PHY_ADDR	0xff298000
#define SST_BYT_DMA1_PHY_ADDR	0xff29c000
#define SST_BYT_SSP0_PHY_ADDR	0xff2a0000
#define SST_BYT_SSP2_PHY_ADDR	0xff2a2000

#ifndef CONFIG_X86_64
#define MEMCPY_TOIO memcpy_toio
#else
#define MEMCPY_TOIO memcpy32_toio
#endif

enum sst_ram_type {
	SST_BYT_IRAM	= 1,
	SST_BYT_DRAM	= 2,
	SST_BYT_CACHE	= 3,
};

struct dma_block_info {
	enum sst_ram_type	type;	/* IRAM/DRAM */
	u32			size;	/* Bytes */
	u32			ram_offset; /* Offset in I/DRAM */
	u32			rsvd;	/* Reserved field */
};

struct fw_header {
	unsigned char signature[SST_BYT_FW_SIGNATURE_SIZE];
	u32 file_size; /* size of fw minus this header */
	u32 modules; /*  # of modules */
	u32 file_format; /* version of header format */
	u32 reserved[4];
};

struct sst_byt_fw_module_header {
	unsigned char signature[SST_BYT_FW_SIGNATURE_SIZE];
	u32 mod_size; /* size of module */
	u32 blocks; /* # of blocks */
	u32 type; /* codec type, pp lib */
	u32 entry_point;
};

/**
 * memcpy32_toio: Copy using writel commands
 *
 * This is needed because the hardware does not support
 * 64-bit moveq insructions while writing to PCI MMIO
 */
void memcpy32_toio(void *dst, const void *src, int count)
{
	int i;
	const u32 *src_32 = src;
	u32 *dst_32 = dst;

	for (i = 0; i < count/sizeof(u32); i++)
		writel(*src_32++, dst_32++);
}

static inline int sst_validate_elf(struct sst_dsp *dsp, struct sst_fw *sst_fw)
{
	Elf32_Ehdr *elf;

	elf = (Elf32_Ehdr *)sst_fw->dma_buf;

	if ((elf->e_ident[0] != 0x7F) || (elf->e_ident[1] != 'E') ||
	    (elf->e_ident[2] != 'L') || (elf->e_ident[3] != 'F')) {
		dev_err(dsp->dev, "ELF Header Not found! %d\n", sst_fw->size);
		return -EINVAL;
	}

	dev_dbg(dsp->dev, "Valid ELF Header...%d\n", sst_fw->size);
	return 0;
}

static int sst_byt_parse_elf_module(struct sst_dsp *dsp, struct sst_fw *fw,
				Elf32_Ehdr *elf, Elf32_Phdr *pr)
{
	struct sst_module *mod;
	struct sst_module_data block_data;
	struct sst_module_template template;
	int modsize;

	if (pr->p_filesz == 0)
		return 0;

	if ((pr->p_paddr >= SST_BYT_IRAM_PHY_START) &&
	    (pr->p_paddr < SST_BYT_IRAM_PHY_END)) {

		block_data.offset = dsp->addr.iram_offset +
			(pr->p_paddr - SST_BYT_IRAM_PHY_START);

		printk(KERN_ERR "PHY IRAM 0x%x iram off 0x%x offset 0x%x\n",
			pr->p_paddr, dsp->addr.iram_offset, block_data.offset);
		block_data.type = SST_MEM_IRAM;
	} else if ((pr->p_paddr >= SST_BYT_DRAM_PHY_START) &&
		 (pr->p_paddr < SST_BYT_DRAM_PHY_END)) {

		block_data.offset = dsp->addr.dram_offset +
			(pr->p_paddr - SST_BYT_DRAM_PHY_START);

		printk(KERN_ERR "PHY DRAM 0x%x dram off 0x%x offset 0x%x\n",
			pr->p_paddr, dsp->addr.dram_offset, block_data.offset);
		block_data.type = SST_MEM_DRAM;
	} else if ((pr->p_paddr >= SST_BYT_IMR_VIRT_START) &&
		 (pr->p_paddr < SST_BYT_IMR_VIRT_END)) {
		block_data.offset = (pr->p_paddr - SST_BYT_IMR_VIRT_START);

		printk(KERN_ERR "VIRT IRAM 0x%x iram off 0x%x offset 0x%x\n",
			pr->p_paddr, 0, block_data.offset);
		block_data.type = SST_MEM_IRAM;

	} else {
		dev_err(dsp->dev, "wrong ram type 0x%x at 0x%x\n",
			pr->p_type, pr->p_paddr);
		return -EINVAL;
	}

	memset(&template, 0, sizeof(template));
	template.id = block_data.type;
	template.entry = 0;//module->entry_point;
	template.p.type = SST_MEM_DRAM;
	template.p.data_type = SST_DATA_P;
	template.s.type = SST_MEM_DRAM;
	template.s.data_type = SST_DATA_S;

	mod = sst_module_new(fw, &template, NULL);
	if (mod == NULL)
		return -ENOMEM;


	block_data.size = pr->p_filesz;
	modsize = block_data.size % 4;
	if (modsize)
		block_data.size += modsize; 
	block_data.data_type = SST_DATA_M;
	block_data.data = (void *)elf + pr->p_offset;
	printk(KERN_ERR "  -> copy to offset 0x%x size 0x%x\n",
		block_data.offset, block_data.size);
	sst_module_insert_fixed_block(mod, &block_data);

	return 0;
}

struct sst_ssp_platform_cfg {
	u8 ssp_cfg_sst;
	u8 port_number;
	u8 is_master;
	u8 pack_mode;
	u8 num_slots_per_frame;
	u8 num_bits_per_slot;
	u8 active_tx_map;
	u8 active_rx_map;
	u8 ssp_frame_format;
	u8 frame_polarity;
	u8 serial_bitrate_clk_mode;
	u8 frame_sync_width;
	u8 dma_handshake_interface_tx;
	u8 dma_handshake_interface_rx;
	u8 network_mode;
	u8 start_delay;
	u32 ssp_base_add;
} __packed;

#define SST_MAX_SSP_PORTS 4
#define SST_MAX_DMA 2

struct sst_board_config_data {
	struct sst_ssp_platform_cfg ssp_platform_data[SST_MAX_SSP_PORTS];
	u8 active_ssp_ports;
	u8 platform_id;
	u8 board_id;
	u8 ihf_num_chan;
	u32 osc_clk_freq;
} __packed;


struct sst_platform_config_data {
	u32 sst_sram_buff_base;
	u32 sst_dma_base[SST_MAX_DMA];
} __packed;

struct sst_fill_config {
	u32 sign;
	struct sst_board_config_data sst_bdata;
	struct sst_platform_config_data sst_pdata;
	u32 shim_phy_add;
	u32 mailbox_add;
} __packed;

static const struct sst_platform_config_data sst_byt_pdata = {
	.sst_sram_buff_base	= 0xffffffff,
	.sst_dma_base[0]	= SST_BYT_DMA0_PHY_ADDR,
	.sst_dma_base[1]	= SST_BYT_DMA1_PHY_ADDR,
};

static const struct sst_board_config_data sst_byt_rvp_bdata = {
	.active_ssp_ports = 1,
	.platform_id = 3,
	.board_id = 1,
	.ihf_num_chan = 2,
	.osc_clk_freq = 25000000,
	.ssp_platform_data = {
		[0] = {
			.ssp_cfg_sst = 1,
			.port_number = 2,
			.is_master = 1,
			.pack_mode = 1,
			.num_slots_per_frame = 2,
			.num_bits_per_slot = 24,
			.active_tx_map = 3,
			.active_rx_map = 3,
			.ssp_frame_format = 3,
			.frame_polarity = 1,
			.serial_bitrate_clk_mode = 0,
			.frame_sync_width = 24,
			.dma_handshake_interface_tx = 5,
			.dma_handshake_interface_rx = 4,
			.network_mode = 0,
			.start_delay = 1,
			.ssp_base_add = SST_BYT_SSP2_PHY_ADDR,
		},
	},
};

#define SST_CONFIG_SSP_SIGN 0x7ffe8001
#define SST_BYT_SHIM_PHY_ADDR	0xff340000
#define SST_BYT_MBOX_PHY_ADDR	0xff344000

void sst_fill_config(struct sst_dsp *sst, unsigned int offset)
{
	struct sst_fill_config sst_config;

	sst_config.sign = SST_CONFIG_SSP_SIGN;
	memcpy(&sst_config.sst_bdata, &sst_byt_rvp_bdata, sizeof(struct sst_board_config_data));
	memcpy(&sst_config.sst_pdata, &sst_byt_pdata, sizeof(struct sst_platform_config_data));
	sst_config.shim_phy_add = SST_BYT_SHIM_PHY_ADDR;
	sst_config.mailbox_add = SST_BYT_MBOX_PHY_ADDR;
	MEMCPY_TOIO(sst->addr.dram + offset, &sst_config, sizeof(sst_config));

}

#define MRFLD_FW_VIRTUAL_BASE 0xC0000000
#define MRFLD_FW_DDR_BASE_OFFSET 0x0
#define MRFLD_FW_FEATURE_BASE_OFFSET 0x4
#define MRFLD_FW_BSS_RESET_BIT 0

/*
 * Writing the DDR physical base to DCCM offset
 * so that FW can use it to setup TLB
 */
static void sst_dccm_config_write(void __iomem *dram_base, unsigned int ddr_base)
{
	void __iomem *addr;
	u32 bss_reset = 0;

	addr = (void __iomem *)(dram_base + MRFLD_FW_DDR_BASE_OFFSET);
	MEMCPY_TOIO(addr, (void *)&ddr_base, sizeof(u32));
	bss_reset |= (1 << MRFLD_FW_BSS_RESET_BIT);
	addr = (void __iomem *)(dram_base + MRFLD_FW_FEATURE_BASE_OFFSET);
	MEMCPY_TOIO(addr, &bss_reset, sizeof(u32));
	pr_debug("%s: config written to DCCM\n", __func__);
}

void sst_post_download_byt(struct sst_dsp *sst)
{
	sst_dccm_config_write(sst->addr.dram, sst->addr.lpe_base);
	sst_fill_config(sst, 2 * sizeof(u32));
}

static int
sst_parse_elf_fw_memcpy(struct sst_dsp *sst, struct sst_fw *sst_fw)
{
	Elf32_Ehdr *elf;
	Elf32_Phdr *pr;
	int i = 0;

	elf = (Elf32_Ehdr *)sst_fw->dma_buf;
	pr = (Elf32_Phdr *) (sst_fw->dma_buf + elf->e_phoff);

	while (i < elf->e_phnum) {
		if (pr[i].p_type == PT_LOAD)
			sst_byt_parse_elf_module(sst, sst_fw, elf, &pr[i]);
		i++;
	}
	return 0;
}

static int sst_byt_parse_module(struct sst_dsp *dsp, struct sst_fw *fw,
				struct sst_byt_fw_module_header *module)
{
	struct dma_block_info *block;
	struct sst_module *mod;
	struct sst_module_data block_data;
	struct sst_module_template template;
	int count;

	memset(&template, 0, sizeof(template));
	template.id = module->type;
	template.entry = module->entry_point;
	template.p.type = SST_MEM_DRAM;
	template.p.data_type = SST_DATA_P;
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
		case SST_BYT_IRAM:
			block_data.offset = block->ram_offset +
					    dsp->addr.iram_offset;
			block_data.type = SST_MEM_IRAM;
			break;
		case SST_BYT_DRAM:
			block_data.offset = block->ram_offset +
					    dsp->addr.dram_offset;
			block_data.type = SST_MEM_DRAM;
			break;
		case SST_BYT_CACHE:
			block_data.offset = block->ram_offset +
					    (dsp->addr.fw_ext - dsp->addr.lpe);
			block_data.type = SST_MEM_CACHE;
			break;
		default:
			dev_err(dsp->dev, "wrong ram type 0x%x in block0x%x\n",
				block->type, count);
			return -EINVAL;
		}

		block_data.size = block->size;
		block_data.data_type = SST_DATA_M;
		block_data.data = (void *)block + sizeof(*block);

		sst_module_insert_fixed_block(mod, &block_data);

		block = (void *)block + sizeof(*block) + block->size;
	}
	return 0;
}

static int sst_byt_parse_fw_image(struct sst_fw *sst_fw)
{
	struct fw_header *header;
	struct sst_byt_fw_module_header *module;
	struct sst_dsp *dsp = sst_fw->dsp;
	int ret, count;

	/* Read the header information from the data pointer */
	header = (struct fw_header *)sst_fw->dma_buf;

	/* verify FW */
	if ((strncmp(header->signature, SST_BYT_FW_SIGN, 4) != 0) ||
	    (sst_fw->size != header->file_size + sizeof(*header))) {
		/* Invalid FW signature */
		dev_err(dsp->dev, "Invalid FW sign/filesize mismatch\n");
		return -EINVAL;
	}

	dev_dbg(dsp->dev,
		"header sign=%4s size=0x%x modules=0x%x fmt=0x%x size=%zu\n",
		header->signature, header->file_size, header->modules,
		header->file_format, sizeof(*header));

	module = (void *)sst_fw->dma_buf + sizeof(*header);
	for (count = 0; count < header->modules; count++) {
		/* module */
		ret = sst_byt_parse_module(dsp, sst_fw, module);
		if (ret < 0) {
			dev_err(dsp->dev, "invalid module %d\n", count);
			return ret;
		}
		module = (void *)module + sizeof(*module) + module->mod_size;
	}

	return 0;
}

static int sst_byt_parse_fw_elf_image(struct sst_fw *sst_fw)
{
	struct sst_dsp *dsp = sst_fw->dsp;
	int ret;

	/* verify FW */
	ret = sst_validate_elf(dsp, sst_fw);
	if (ret < 0) {
		dev_err(dsp->dev, "invalid fw format\n");
		return ret;
	}

	/* prepare for memcpy */
	sst_parse_elf_fw_memcpy(dsp, sst_fw);

	/* setup config and mailbox */
	sst_post_download_byt(dsp);

	return 0;
}

void sst_byt_dump_shim(struct sst_dsp *sst)
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
	u64 isrx;
	irqreturn_t ret = IRQ_NONE;

	spin_lock(&sst->spinlock);

	isrx = sst_dsp_shim_read64_unlocked(sst, SST_ISRX);

	if (isrx & SST_ISRX_DONE) {
		/* ADSP has processed the message request from IA */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IPCX,
						    SST_BYT_IPCX_DONE, 0);
		ret = IRQ_WAKE_THREAD;
	}
	if (isrx & SST_BYT_ISRX_REQUEST) {
		/* mask message request from ADSP and do processing later */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IMRX,
						    SST_BYT_IMRX_REQUEST,
						    SST_BYT_IMRX_REQUEST);
		ret = IRQ_WAKE_THREAD;
	}

	spin_unlock(&sst->spinlock);

	return ret;
}

static void sst_byt_boot(struct sst_dsp *sst)
{
	int tries = 10;

	/*
	 * save the physical address of extended firmware block in the first
	 * 4 bytes of the mailbox
	 */
	memcpy_toio(sst->addr.lpe + SST_BYT_MAILBOX_OFFSET,
	       &sst->pdata->fw_base, sizeof(u32));

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
	/* set reset vector and stall */
	sst_dsp_shim_update_bits64(sst, SST_CSR,
		SST_BYT_CSR_VECTOR_SEL | SST_BYT_CSR_STALL,
		SST_BYT_CSR_VECTOR_SEL | SST_BYT_CSR_STALL);

	udelay(10);

	/* put DSP into reset */
	sst_dsp_shim_update_bits64(sst, SST_CSR,
		SST_BYT_CSR_RST, SST_BYT_CSR_RST);

	/* dummy read to make sure clock is ungated */
	sst_dsp_shim_read64_unlocked(sst, SST_IPCD);
	udelay(10);

	/* take DSP out of reset and keep stalled for FW loading */
	sst_dsp_shim_update_bits64(sst, SST_CSR, SST_BYT_CSR_RST, 0);
}

struct sst_adsp_memregion {
	u32 start;
	u32 end;
	int blocks;
	enum sst_mem_type type;
};

/* BYT test stuff */
static const struct sst_adsp_memregion byt_region[] = {
	{0x00000, 0x1fffff, 1, SST_MEM_CACHE}, /* DDR 2MB */
	{0xC0000, 0x100000, 1, SST_MEM_IRAM}, /* I-SRAM - 8 * 32kB */
	{0x100000, 0x140000, 1, SST_MEM_DRAM}, /* D-SRAM0 - 8 * 32kB */
};

static int sst_byt_resource_map(struct sst_dsp *sst, struct sst_pdata *pdata)
{
	sst->addr.lpe_base = pdata->lpe_base;
printk(KERN_ERR "lpe %p 0x%x\n", pdata->lpe_base, pdata->lpe_size);
	sst->addr.lpe = ioremap(pdata->lpe_base, pdata->lpe_size);
	if (!sst->addr.lpe)
		return -ENODEV;
printk(KERN_ERR "pci %p 0x%x\n", pdata->pcicfg_base, pdata->pcicfg_size);
	/* ADSP PCI MMIO config space */
	sst->addr.pci_cfg = ioremap(pdata->pcicfg_base, pdata->pcicfg_size);
	if (!sst->addr.pci_cfg) {
		iounmap(sst->addr.lpe);
		return -ENODEV;
	}
printk(KERN_ERR "mailbox %p 0x%x\n", pdata->fw_base, pdata->fw_size);
	/* SST Extended FW allocation */
	sst->addr.fw_ext = ioremap(pdata->fw_base, pdata->fw_size);
	if (!sst->addr.fw_ext) {
		iounmap(sst->addr.pci_cfg);
		iounmap(sst->addr.lpe);
		return -ENODEV;
	}
printk(KERN_ERR "dram %p 0x%x\n", pdata->dram_base, pdata->dram_size);
	sst->addr.dram = ioremap(pdata->dram_base, pdata->dram_size);
	if (!sst->addr.dram) {
		iounmap(sst->addr.fw_ext);
		iounmap(sst->addr.pci_cfg);
		iounmap(sst->addr.lpe);
		return -ENODEV;
	}
printk(KERN_ERR "iram %p 0x%x\n", pdata->iram_base, pdata->iram_size);
	sst->addr.iram = ioremap(pdata->iram_base, pdata->iram_size);
	if (!sst->addr.iram) {
		iounmap(sst->addr.dram);
		iounmap(sst->addr.fw_ext);
		iounmap(sst->addr.pci_cfg);
		iounmap(sst->addr.lpe);
		return -ENODEV;
	}

	/* SST Shim */
	sst->addr.shim = sst->addr.lpe + sst->addr.shim_offset;

	sst_dsp_mailbox_init(sst, SST_BYT_MAILBOX_OFFSET + 0x204,
			     SST_BYT_IPC_MAX_PAYLOAD_SIZE,
			     SST_BYT_MAILBOX_OFFSET,
			     SST_BYT_IPC_MAX_PAYLOAD_SIZE);

	sst->irq = pdata->irq;

	return 0;
}

static int byt_enable_shim(struct sst_dsp *sst)
{
	/* enable shim - do dummy read */
	writel(0, sst->addr.pci_cfg + 0x84);
	dev_err(sst->dev, "PMCS read 0x%x\n", readl(sst->addr.pci_cfg + 0x84));

	/* make sure that ADSP shim is enabled */
	mdelay(11);

	/* enable Interrupt from both sides */
	sst_dsp_shim_update_bits64(sst, SST_IMRX, 0x3, 0x0);
	sst_dsp_shim_update_bits64(sst, SST_IMRD, 0x3, 0x0);

	sst_dsp_shim_update_bits64(sst, 0x10, 0x20, 0x0); // unMask SSP2
	sst_dsp_shim_update_bits64(sst, 0x78, 0x7, 0x5); // 200MHz

	sst_byt_dump_shim(sst);
	return 0;
}

int sst_byt_d0(struct sst_dsp *sst)
{
	return byt_enable_shim(sst);
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

	sst_byt_d0(sst);

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	/* enable Interrupt from both sides */
	sst_dsp_shim_update_bits64(sst, SST_IMRX, 0x3, 0x0);
	sst_dsp_shim_update_bits64(sst, SST_IMRD, 0x3, 0x0);

	/* register DSP memory blocks - ideally we should get this from ACPI */
	for (i = 0; i < region_count; i++) {
		offset = region[i].start;
		size = ((region[i].end - region[i].start) / region[i].blocks) + 1;

		/* register individual memory blocks */
		for (j = 0; j < region[i].blocks; j++) {
printk(KERN_ERR "register block %d offset 0x%x size 0x%x\n", j, offset, size);
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
	iounmap(sst->addr.fw_ext);
}

struct sst_ops sst_byt_ops = {
	.reset = sst_byt_reset,
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
	.parse_fw = sst_byt_parse_fw_elf_image,
	.dump = sst_byt_dump_shim,
};
