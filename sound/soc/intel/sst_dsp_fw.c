/*
 *  sst_dsp_fw.c - Intel SST FW Loader
 *
 *  Copyright (C) 2013	Intel Corp
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

#define DEBUG

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/export.h>
#include <linux/device.h>

#include "sst_dsp.h"
#include "sst_dsp_priv.h"

struct sst_sg_list {
	struct scatterlist *src;
	struct scatterlist *dst;
	int list_len;
};

static void module_remove_text(struct sst_module *module,
	struct sst_bmap *bmap);
static void module_remove_data(struct sst_module *module,
	struct sst_bmap *bmap);

#if 0
/**
 * sst_parse_module - Parse audio FW modules
 *
 * @module: FW module header
 *
 * Count the length for scattergather list
 * and create the scattergather list of same length
 * returns error or 0 if module sizes are proper
 */
static int sst_parse_module(struct fw_module_header *module,
				struct sst_sg_list *sg_list)
{
	struct dma_block_info *block;
	u32 count;
	unsigned long ram;
	int retval, sg_len = 0;
	struct scatterlist *sg_src, *sg_dst;

	pr_debug("module sign %s size %x blocks %x type %x\n",
			module->signature, module->mod_size,
			module->blocks, module->type);
	pr_debug("module entrypoint 0x%x\n", module->entry_point);

	block = (void *)module + sizeof(*module);

	for (count = 0; count < module->blocks; count++) {
		sg_len += (block->size) / SST_MAX_DMA_LEN;
		if ((block->size) % SST_MAX_DMA_LEN)
			sg_len = sg_len + 1;
		block = (void *)block + sizeof(*block) + block->size;
	}

	sg_src = kzalloc(sizeof(*sg_src)*(sg_len), GFP_KERNEL);
	if (NULL == sg_src)
		return -ENOMEM;
	sg_init_table(sg_src, sg_len);
	sg_dst = kzalloc(sizeof(*sg_dst)*(sg_len), GFP_KERNEL);
	if (NULL == sg_dst) {
		kfree(sg_src);
		return -ENOMEM;
	}
	sg_init_table(sg_dst, sg_len);

	sg_list->src = sg_src;
	sg_list->dst = sg_dst;
	sg_list->list_len = sg_len;

	block = (void *)module + sizeof(*module);
	for (count = 0; count < module->blocks; count++) {
		if (block->size <= 0) {
			pr_err("block size invalid\n");
			return -EINVAL;
		}
		switch (block->type) {
		case SST_IRAM:
			ram = sst_drv_ctx->iram_base;
			break;
		case SST_DRAM:
			ram = sst_drv_ctx->dram_base;
			break;
		default:
			pr_err("wrong ram type0x%x in block0x%x\n",
					block->type, count);
			return -EINVAL;
		}
		/*converting from physical to virtual because
		scattergather list works on virtual pointers*/
		ram = (int) phys_to_virt(ram);
		retval = sst_fill_sglist(ram, block, &sg_src, &sg_dst);
		if (retval) {
			kfree(sg_src);
			kfree(sg_dst);
			sg_src = NULL;
			sg_dst = NULL;
			return retval;
		}
		block = (void *)block + sizeof(*block) + block->size;
	}
	return 0;
}

static bool chan_filter(struct dma_chan *chan, void *param)
{
	struct sst_dma *dma = (struct sst_dma *)param;
	bool ret = false;

	/* we only need MID_DMAC1 as that can access DSP RAMs*/
	if (chan->device->dev == &dma->dmac->dev)
		ret = true;

	return ret;
}

static int sst_alloc_dma_chan(struct sst_dma *dma)
{
	dma_cap_mask_t mask;
	struct intel_mid_dma_slave *slave = &dma->slave;
	int retval;

	pr_debug("%s\n", __func__);
	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	if (sst_drv_ctx->pci_id == SST_CLV_PCI_ID)
		dma->dmac = pci_get_device(PCI_VENDOR_ID_INTEL,
						PCI_DMAC_CLV_ID, NULL);
	else
		dma->dmac = pci_get_device(PCI_VENDOR_ID_INTEL,
						PCI_DMAC_MFLD_ID, NULL);

	if (!dma->dmac) {
		pr_err("Can't find DMAC\n");
		return -ENODEV;
	}
	dma->ch = dma_request_channel(mask, chan_filter, dma);
	if (!dma->ch) {
		pr_err("unable to request dma channel\n");
		return -EIO;
	}

	slave->dma_slave.direction = DMA_FROM_DEVICE;
	slave->hs_mode = 0;
	slave->cfg_mode = LNW_DMA_MEM_TO_MEM;
	slave->dma_slave.src_addr_width = slave->dma_slave.dst_addr_width =
						DMA_SLAVE_BUSWIDTH_4_BYTES;
	slave->dma_slave.src_maxburst = slave->dma_slave.dst_maxburst =
							LNW_DMA_MSIZE_16;

	retval = dmaengine_slave_config(dma->ch, &slave->dma_slave);
	if (retval) {
		pr_err("unable to set slave config, err %d\n", retval);
		dma_release_channel(dma->ch);
		return -EIO;
	}
	return retval;
}

static void sst_dma_transfer_complete(void *arg)
{
	sst_drv_ctx  = (struct intel_sst_drv *)arg;
	pr_debug(" sst_dma_transfer_complete\n");
	if (sst_drv_ctx->dma_info_blk.on == true) {
		sst_drv_ctx->dma_info_blk.on = false;
		sst_drv_ctx->dma_info_blk.condition = true;
		wake_up(&sst_drv_ctx->wait_queue);
	}
}

/**
 * sst_parse_fw_image - parse and load FW
 *
 * @sst_fw: pointer to audio fw
 *
 * This function is called to verify and parse the FW image and save the parsed
 * image in a list for DMA
 */
static int sst_parse_fw_image(const void *sst_fw_in_mem, unsigned long size,
				struct sst_sg_list *sg_list)
{
	struct fw_header *header;
	u32 count;
	int ret_val;
	struct fw_module_header *module;

	pr_debug("%s\n", __func__);
	/* Read the header information from the data pointer */
	header = (struct fw_header *)sst_fw_in_mem;
	pr_debug("header sign=%s size=%x modules=%x fmt=%x size=%x\n",
			header->signature, header->file_size, header->modules,
			header->file_format, sizeof(*header));
	/* verify FW */
	if ((strncmp(header->signature, SST_FW_SIGN, 4) != 0) ||
		(size != header->file_size + sizeof(*header))) {
		/* Invalid FW signature */
		pr_err("InvalidFW sign/filesize mismatch\n");
		return -EINVAL;
	}

	module = (void *)sst_fw_in_mem + sizeof(*header);
	for (count = 0; count < header->modules; count++) {
		/* module */
		ret_val = sst_parse_module(module, sg_list);
		if (ret_val)
			return ret_val;
		module = (void *)module + sizeof(*module) + module->mod_size ;
	}

	return 0;
}

/*
 * sst_request_fw - requests audio fw from kernel and saves a copy
 *
 * This function requests the SST FW from the kernel, parses it and
 * saves a copy in the driver context
 */
int sst_request_fw(void)
{
	int retval = 0;
	char name[20];

#if 0
#ifndef MRFLD_TEST_ON_MFLD
	snprintf(name, sizeof(name), "%s%04x%s", "fw_sst_",
				sst_drv_ctx->pci_id, ".bin");
#else
	snprintf(name, sizeof(name), "%s%04x%s", "fw_sst_",
				sst_drv_ctx->pci_id, "_mt.bin");
#endif
#endif

	snprintf(name, sizeof(name), "%s%s", "IntcADSP",
				".bin");

	pr_debug("Requesting FW %s now...\n", name);
	retval = request_firmware(&sst_drv_ctx->fw, name,
				 &sst_drv_ctx->pci->dev);
	if (retval) {
		pr_err("request fw failed %d\n", retval);
		return retval;
	}
#if 0
#ifndef MRFLD_TEST_ON_MFLD
	if (sst_drv_ctx->pci_id == SST_MRFLD_PCI_ID)
#endif
	retval = sst_validate_fw_elf(sst_drv_ctx->fw);
	if (retval != 0) {
		pr_err("FW image invalid...\n");
		goto end_release;
	}
#endif
	pr_debug("Loaded FW size 0x%x\n", sst_drv_ctx->fw->size);
	sst_drv_ctx->fw_in_mem = kzalloc(sst_drv_ctx->fw->size, GFP_KERNEL);
	if (!sst_drv_ctx->fw_in_mem) {
		pr_err("%s unable to allocate memory\n", __func__);
		retval = -ENOMEM;
		goto end_release;
	}

	memcpy(sst_drv_ctx->fw_in_mem, sst_drv_ctx->fw->data,
			sst_drv_ctx->fw->size);

#ifndef MRFLD_TEST_ON_MFLD
	if (sst_drv_ctx->pci_id != SST_MRFLD_PCI_ID) {
		pr_debug("sst parse fw image\n");
		retval = sst_parse_fw_image(sst_drv_ctx->fw_in_mem,
					sst_drv_ctx->fw->size,
					&sst_drv_ctx->fw_sg_list);
		if (retval) {
			kfree(sst_drv_ctx->fw_in_mem);
			goto end_release;
		}
	}
#endif
end_release:
#if 0
	release_firmware(sst_drv_ctx->fw);
	sst_drv_ctx->fw = NULL;
#endif
	return retval;
}

int sst_dma_firmware(struct sst_dma *dma, struct sst_sg_list *sg_list)
{
	int retval = 0;
	enum dma_ctrl_flags flag = DMA_CTRL_ACK;
	struct scatterlist *sg_src_list, *sg_dst_list;
	int length;
	pr_debug(" sst_dma_firmware\n");

	sg_src_list = sg_list->src;
	sg_dst_list = sg_list->dst;
	length = sg_list->list_len;
	sst_drv_ctx->desc = dma->ch->device->device_prep_dma_sg(dma->ch,
					sg_dst_list, length,
					sg_src_list, length, flag);
	if (!sst_drv_ctx->desc)
		return -EFAULT;
	sst_drv_ctx->desc->callback = sst_dma_transfer_complete;
	sst_drv_ctx->desc->callback_param = sst_drv_ctx;

	sst_drv_ctx->dma_info_blk.condition = false;
	sst_drv_ctx->dma_info_blk.ret_code = 0;
	sst_drv_ctx->dma_info_blk.on = true;

	sst_drv_ctx->desc->tx_submit(sst_drv_ctx->desc);
	retval = sst_wait_timeout(sst_drv_ctx, &sst_drv_ctx->dma_info_blk);
	if (retval)
		pr_err("sst_dma_firmware..timeout!\n");

	return retval;
}

void sst_dma_free_resources(struct sst_dma *dma)
{
	pr_debug("entry:%s\n", __func__);

	dma_release_channel(dma->ch);
}

static void memcopy_and_validate32(void *dest, void *src, int bytes)
{
	u32 *src32 = src, *dest32 = dest;
	int size, i;

	size = bytes >> 2;

	/* copy word at a time */
	for (i = 0; i < size; i++) {
		*dest32 = *src32;
		dest32++;
		src32++;
	}

	/* validate word at a time */
	src32 = src;
	dest32 = dest;
	for (i = 0; i < size; i++) {
		if (*dest32 != *src32) {
			pr_err("  failed at %p. Got 0x%x expected 0x%x\n",
				dest32, *dest32, *src32);
			break;
		}
		dest32++;
		src32++;
	}
	pr_debug("  block copy validated\n");
}

static struct fw_module_header *module2;

static void validate32(void *dest, void *src, int bytes)
{
	u32 *src32 = src, *dest32 = dest;
	int size, i;

	size = bytes >> 2;

	for (i = 0; i < size; i++) {
		if (*dest32 != *src32) {
			pr_err("  failed at %p. Got 0x%x expected 0x%x\n",
				dest32, *dest32, *src32);
			break;
		}
		dest32++;
		src32++;
	}
	pr_debug("  block validated\n");
}

int sst_validate(void)
{
	struct dma_block_info *block;
	u32 count;
	void __iomem *ram;

	block = (void *)module2 + sizeof(*module2);

	for (count = 0; count < module2->blocks; count++) {
		if (block->size <= 0) {
			pr_err("block size invalid\n");
			return -EINVAL;
		}
		switch (block->type) {
		case SST_IRAM:
			ram = sst_drv_ctx->iram;
			break;
		case SST_DRAM:
			ram = sst_drv_ctx->dram;
			break;
		default:
			pr_err("wrong ram type0x%x in block0x%x\n",
					block->type, count);
			return -EINVAL;
		}
		pr_debug("validate block %d type 0x%x size 0x%x ==> ram %p offset 0x%x\n",
				count, block->type, block->size, ram, block->ram_offset);

		memcopy_and_validate32(ram + block->ram_offset,
				(void *)block + sizeof(*block), block->size);
		block = (void *)block + sizeof(*block) + block->size;
	}
	return 0;
}

/**
 * sst_parse_module - Parse audio FW modules
 *
 * @module: FW module header
 *
 * Parses modules that need to be placed in SST IRAM and DRAM
 * returns error or 0 if module sizes are proper
 */
int sst_parse_module2(struct fw_module_header *module)
{
	struct dma_block_info *block;
	u32 count;
	void __iomem *ram;

	pr_debug("module sign %s size %x blocks %x type %x\n",
			module->signature, module->mod_size,
			module->blocks, module->type);
	pr_debug("module entrypoint 0x%x\n", module->entry_point);

	block = (void *)module + sizeof(*module);

	for (count = 0; count < module->blocks; count++) {
		if (block->size <= 0) {
			pr_err("block size invalid\n");
			return -EINVAL;
		}
		switch (block->type) {
		case SST_IRAM:
			ram = sst_drv_ctx->iram;
			break;
		case SST_DRAM:
			ram = sst_drv_ctx->dram;
			break;
		default:
			pr_err("wrong ram type0x%x in block0x%x\n",
					block->type, count);
			return -EINVAL;
		}
		pr_debug("Copy block %d type 0x%x size 0x%x ==> ram %p offset 0x%x\n",
				count, block->type, block->size, ram, block->ram_offset);
#if 0
		memcpy_toio(ram + block->ram_offset,
				(void *)block + sizeof(*block), block->size);
#else
		memcopy_and_validate32(ram + block->ram_offset,
				(void *)block + sizeof(*block), block->size);
#endif
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
static int sst_parse_fw_image2(const struct firmware *sst_fw)
{
	struct fw_header *header;
	u32 count;
	int ret_val;
	struct fw_module_header *module;

	BUG_ON(!sst_fw);

	/* Read the header information from the data pointer */
	header = (struct fw_header *)sst_fw->data;

	/* verify FW */
	if ((strncmp(header->signature, SST_FW_SIGN, 4) != 0) ||
			(sst_fw->size != header->file_size + sizeof(*header))) {
		/* Invalid FW signature */
		pr_err("Invalid FW sign/filesize mismatch\n");
		return -EINVAL;
	}
	pr_debug("header sign=%s size=%x modules=%x fmt=%x size=%x\n",
			header->signature, header->file_size, header->modules,
			header->file_format, sizeof(*header));
	module = (void *)sst_fw->data + sizeof(*header);
	for (count = 0; count < header->modules; count++) {
		/* module */
		ret_val = sst_parse_module2(module);
		if (ret_val)
			return ret_val;
		module = (void *)module + sizeof(*module) + module->mod_size ;
	}

	return 0;
}

/**
 * sst_load_fw - function to load FW into DSP
 *
 * @fw: Pointer to driver loaded FW
 * @context: driver context
 *
 * Transfers the FW to DSP using DMA
 */
int sst_load_fw(const void *fw_in_mem, void *context)
{
	int ret_val = 0;
	int tmp;

	pr_debug("load_fw called\n");
	BUG_ON(!fw_in_mem);

	printk(KERN_INFO "Function %s %d Dump shim registers before download\n", __func__, __LINE__);
	dump_sst_shim2(sst_drv_ctx);
	ret_val = sst_drv_ctx->ops->reset();
	if (ret_val)
		return ret_val;

	/* FIXME: this needs to be revised when the elf fw downloading is
	 * implemented. right now !elf means dma
	 * also when we do configurable memcpy */
	if (sst_drv_ctx->info.use_elf == true) {
		if (sst_drv_ctx->pci_id != SST_HSWULT_PCI_ID) {
			printk(KERN_INFO "Function %s %d\n", __func__, __LINE__);
			//sst_download_elf_fw(sst_drv_ctx, fw_in_mem);
		} else {
			printk(KERN_INFO "Function %s %d\n", __func__, __LINE__);
			ret_val = sst_parse_fw_image2(sst_drv_ctx->fw);
			if (ret_val)
				return ret_val;
		}
	} else {
		/* get a dmac channel */
		printk(KERN_INFO "Function %s %d\n", __func__, __LINE__);
		sst_alloc_dma_chan(&sst_drv_ctx->dma);
		 /* allocate desc for transfer and submit */
		ret_val = sst_dma_firmware(&sst_drv_ctx->dma,
					&sst_drv_ctx->fw_sg_list);
		if (ret_val)
			goto free_dma;
	}
	sst_set_fw_state_locked(sst_drv_ctx, SST_FW_LOADED);
	/* bring sst out of reset */
	ret_val = sst_drv_ctx->ops->start();

	tmp = readw(sst_drv_ctx->pci_cfg + 0x84);
	pr_debug("PCI: PMCS = 0x%x\n", tmp);

	if (ret_val && sst_drv_ctx->info.use_elf == false)
		goto free_dma;

	printk(KERN_INFO "Function %s %d\n", __func__, __LINE__);
	pr_debug("fw loaded successful!!!\n");
free_dma:
	if (sst_drv_ctx->info.use_elf == false)
		sst_dma_free_resources(&sst_drv_ctx->dma);
	return ret_val;
}

// TODO: new functions that expose public API
#endif

struct sst_fw *sst_fw_new(struct sst_dsp *dsp, 
	const struct firmware *fw, void *private)
{
	struct sst_fw *sst_fw;
	int err;

	if (!dsp->ops->parse_fw)
		return NULL;

	sst_fw = kzalloc(sizeof(*sst_fw), GFP_KERNEL);
	if (sst_fw == NULL)
		return NULL;

	sst_fw->dsp = dsp;
	sst_fw->fw = fw;
	sst_fw->private = private;

	err = dsp->ops->parse_fw(dsp, fw);
	if (err < 0) {
		dev_err(dsp->dev, "parse fw failed %d\n", err);
		kfree(sst_fw);
		return NULL;
	}

	mutex_lock(&dsp->mutex);
	list_add(&sst_fw->list, &dsp->fw_list); 
	mutex_unlock(&dsp->mutex);

	return sst_fw;
}
EXPORT_SYMBOL_GPL(sst_fw_new);

void sst_fw_free(struct sst_fw *sst_fw)
{
	struct sst_dsp *dsp = sst_fw->dsp;

	mutex_lock(&dsp->mutex);
	list_del(&sst_fw->list); 
	mutex_unlock(&dsp->mutex);

	kfree(sst_fw);
}
EXPORT_SYMBOL_GPL(sst_fw_free);

#if 0
int sst_fw_load(struct sst_dsp *dsp, const char *fw_name, int use_dma)
{
	const struct firmware *fw;
	int ret;

	if (!dsp->ops->fw_parse_image)
		return -EINVAL;

	dev_dbg(dsp->dev, "requesting FW %s\n", fw_name);
	ret = request_firmware(&fw, fw_name, dsp->dev);
	if (ret < 0) {
		dev_err(dsp->dev, "request fw failed %d\n", ret);
		return ret;
	}

	sst_dsp_reset(dsp);

	//TODO: Check whether DMA works here
	ret = dsp->fw_parse_image(dsp, use_dma);
	if (ret < 0) {
		dev_err(dsp->dev, "parse fw failed %d\n", ret);
		release_firmware(fw);
	}

	return ret;
}
EXPORT_SYMBOL(sst_fw_load);

void sst_fw_free(struct sst_dsp *dsp)
{
	struct sst_fw *sst_fw;
	mutex_lock(&dsp->mutex);


	release_firmware(dsp->fw);

	mutex_unlock(&dsp->mutex);
}
EXPORT_SYMBOL(sst_fw_free);
#endif

struct sst_module *sst_module_new(struct sst_fw *sst_fw,
	struct sst_module_template *template, void *private)
{
	struct sst_dsp *dsp = sst_fw->dsp;
	struct sst_module *sst_module;

	sst_module = kzalloc(sizeof(*sst_module), GFP_KERNEL);
	if (sst_module == NULL)
		return NULL;

	sst_module->id = template->id;
	sst_module->dsp = dsp;
	sst_module->sst_fw = sst_fw;
	sst_module->data = template->data;
	sst_module->text = template->text;
	sst_module->text_size = template->text_size;
	sst_module->data_size = template->data_size;
	sst_module->text_offset = template->text_offset;
	sst_module->data_offset = template->data_offset;
	sst_module->private = private;
	INIT_LIST_HEAD(&sst_module->tblock_list);
	INIT_LIST_HEAD(&sst_module->dblock_list);

	mutex_lock(&dsp->mutex);
	list_add(&sst_module->list, &dsp->module_list);
	mutex_unlock(&dsp->mutex);

	return sst_module;
}
EXPORT_SYMBOL_GPL(sst_module_new);

void sst_module_free(struct sst_module *sst_module)
{
	struct sst_dsp *dsp = sst_module->dsp;

	mutex_lock(&dsp->mutex);
	list_del(&sst_module->list); 
	mutex_unlock(&dsp->mutex);

	kfree(sst_module);
}
EXPORT_SYMBOL_GPL(sst_module_free);

static int get_contiguous_tblocks(struct sst_mem_block *parent, 
	struct sst_bmap *bmap, struct sst_module *module, int size)
{
	struct sst_mem_block *block, *tmp;
	u32 next_offset = parent->offset + parent->size;
	int ret;

	/* find first free blocks that can hold text */
	list_for_each_entry_safe(block, tmp, &bmap->free_block_list, map_list) {

		/* ignore blocks before parent*/
		if (block->offset < parent->offset)
			continue;

		/* ignore data blocks */
		if (block->type == SST_MEM_DRAM)
			continue;

		/* is block next after parent ? */
		if (next_offset == block->offset) {

			if (size > module->text_size) {
				/* need more blocks */
				ret = get_contiguous_tblocks(block, bmap,
					module, size - module->text_size);
				if (ret < 0)
					return ret;
			}

			/* add block */
			list_add(&block->module_tlist, &module->tblock_list);
			list_move(&block->map_list, &bmap->used_block_list);
			return 0;
		}	
	}
	return -ENOMEM;
}

static int module_get_tblocks(struct sst_module *module, struct sst_bmap *bmap)
{
	struct sst_mem_block *block, *tmp;
	int ret;

	/* find first free blocks that can hold text */
	list_for_each_entry_safe(block, tmp, &bmap->free_block_list, map_list) {

		/* ignore data blocks */
		if (block->type == SST_MEM_DRAM)
			continue;

		if (block->size >= module->text_size) {
			/* return block if size fits */
			list_add(&block->module_tlist, &module->tblock_list);
			list_move(&block->map_list, &bmap->used_block_list);
			return 0;
		} else	{
			/* need more blocks */
			ret = get_contiguous_tblocks(block, bmap,
				module, module->text_size - block->size);
			if (ret < 0)
				return ret;

			/* add block */
			list_add(&block->module_tlist, &module->tblock_list);
			list_move(&block->map_list, &bmap->used_block_list);
			return 0;
		}
	}
	return -ENOMEM;
}

static int module_get_fixed_tblocks(struct sst_module *module,
	struct sst_bmap *bmap)
{
	return 0;
}

static int module_copy_text(struct sst_module *module, struct sst_bmap *bmap)
{
	return 0;
}

static int module_insert_text(struct sst_module *module, struct sst_bmap *bmap)
{
	struct sst_mem_block *block;
	struct sst_dsp *dsp = bmap->dsp;
	int ret;

	/* find space for text */
	ret = module_get_tblocks(module, bmap);
	if (ret < 0) {
		dev_err(dsp->dev, "can't find 0x%x IRAM bytes for module %d\n",
			module->text_size, module->id);
		return ret;
	}

	/* enable each block so that's it'e ready for module text */
	list_for_each_entry(block, &module->tblock_list, module_tlist) {

		if (block->ops && block->ops->enable)
			ret = block->ops->enable(block);
			if (ret < 0)
				goto err;
	}

	/* copy module text to blocks */
	ret = module_copy_text(module, bmap);
	if (ret < 0) {
		dev_err(dsp->dev, "module text copy failed\n");
		goto err;
	}

	return ret;

err:
	module_remove_text(module, bmap);
	return ret;
}

static void module_remove_text(struct sst_module *module, struct sst_bmap *bmap)
{
	struct sst_mem_block *block, *tmp;
	struct sst_dsp *dsp = bmap->dsp;
	int err;

	/* disable each block  */
	list_for_each_entry(block, &module->tblock_list, module_tlist) {

		if (block->ops && block->ops->disable) {
			err = block->ops->disable(block);
			if (err < 0)
				dev_err(dsp->dev, "failed to remove block\n");
		}
	}

	/* mark each block as free */
	list_for_each_entry_safe(block, tmp, &module->tblock_list, module_tlist) {
		list_del(&block->module_tlist);
		list_move(&block->map_list, &bmap->free_block_list);
	}
}

static int get_contiguous_dblocks(struct sst_mem_block *parent, 
	struct sst_bmap *bmap, struct sst_module *module, int size)
{
	struct sst_mem_block *block, *tmp;
	u32 next_offset = parent->offset + parent->size;
	int ret;

	/* find first free blocks that can hold text */
	list_for_each_entry_safe(block, tmp, &bmap->free_block_list, map_list) {

		/* ignore blocks before parent*/
		if (block->offset < parent->offset)
			continue;

		/* ignore text blocks */
		if (block->type == SST_MEM_IRAM)
			continue;

		/* is block next after parent ? */
		if (next_offset == block->offset) {

			if (size > module->data_size) {
				/* need more blocks */
				ret = get_contiguous_tblocks(block, bmap,
					module, size - module->data_size);
				if (ret < 0)
					return ret;
			}

			/* add block */
			list_add(&block->module_dlist, &module->dblock_list);
			list_move(&block->map_list, &bmap->used_block_list);
			return 0;
		}	
	}
	return -ENOMEM;
}

/* find a contiguous range of data blocks at any offset */
static int module_get_dblocks(struct sst_module *module, struct sst_bmap *bmap)
{
	struct sst_mem_block *block, *tmp;
	int ret;

	/* find first free blocks that can hold text */
	list_for_each_entry_safe(block, tmp, &bmap->free_block_list, map_list) {

		/* ignore text blocks */
		if (block->type == SST_MEM_IRAM)
			continue;

		if (block->size >= module->data_size) {
			/* return block if size fits */
			list_add(&block->module_dlist, &module->dblock_list);
			list_move(&block->map_list, &bmap->used_block_list);
			return 0;
		} else	{
			/* need more blocks */
			ret = get_contiguous_dblocks(block, bmap,
				module, module->data_size - block->size);
			if (ret < 0)
				return ret;

			/* add block */
			list_add(&block->module_dlist, &module->dblock_list);
			list_move(&block->map_list, &bmap->used_block_list);
			return 0;
		}
	}
	return -ENOMEM;
}

/* find a contiguous range of data blocks at a fixed offset */
static int module_get_fixed_dblocks(struct sst_module *module,
	struct sst_bmap *bmap)
{
	struct sst_mem_block *block, *tmp;
	int ret;
#warning fix
	/* find first free blocks that can hold text */
	list_for_each_entry_safe(block, tmp, &bmap->free_block_list, map_list) {

		/* ignore text blocks */
		if (block->type == SST_MEM_IRAM)
			continue;

		if (block->size >= module->data_size) {
			/* return block if size fits */
			list_add(&block->module_dlist, &module->dblock_list);
			list_move(&block->map_list, &bmap->used_block_list);
			return 0;
		} else	{
			/* need more blocks */
			ret = get_contiguous_dblocks(block, bmap,
				module, module->data_size - block->size);
			if (ret < 0)
				return ret;

			/* add block */
			list_add(&block->module_dlist, &module->dblock_list);
			list_move(&block->map_list, &bmap->used_block_list);
			return 0;
		}
	}
	return 0;
}

static int module_copy_data(struct sst_module *module, struct sst_bmap *bmap)
{
	return 0;
}

static int module_insert_data(struct sst_module *module, struct sst_bmap *bmap)
{
	struct sst_mem_block *block;
	struct sst_dsp *dsp = bmap->dsp;
	int ret;

	/* find space for data */
	ret = module_get_dblocks(module, bmap);
	if (ret < 0) {
		dev_err(dsp->dev, "can't find 0x%x DRAM bytes for module %d\n",
			module->data_size, module->id);
		return ret;
	}

	/* enable each block so that's it'e ready for module data */
	list_for_each_entry(block, &module->dblock_list, module_dlist) {

		if (block->ops && block->ops->enable)
			ret = block->ops->enable(block);
			if (ret < 0)
				goto err;
	}

	/* copy module text to blocks */
	ret = module_copy_data(module, bmap);
	if (ret < 0) {
		dev_err(dsp->dev, "module data copy failed\n");
		goto err;
	}

	return ret;

err:
	module_remove_data(module, bmap);
	return ret;
}

static void module_remove_data(struct sst_module *module, struct sst_bmap *bmap)
{
	struct sst_mem_block *block, *tmp;
	struct sst_dsp *dsp = bmap->dsp;
	int err;

	/* disable each block  */
	list_for_each_entry(block, &module->dblock_list, module_dlist) {

		if (block->ops && block->ops->disable) {
			err = block->ops->disable(block);
			if (err < 0)
				dev_err(dsp->dev, "failed to remove block\n");
		}
	}

	/* mark each block as free */
	list_for_each_entry_safe(block, tmp, &module->dblock_list, module_dlist) {
		list_del(&block->module_dlist);
		list_move(&block->map_list, &bmap->free_block_list);
	}
}

int sst_module_insert(struct sst_module *module, struct sst_bmap *bmap)
{
	struct sst_dsp *dsp = bmap->dsp;
	int ret;

	mutex_lock(&dsp->mutex);

	/* get DSP memory for module text */
	if (module->text_fixed)
		ret = module_get_fixed_tblocks(module, bmap);
	else
		ret = module_get_tblocks(module, bmap);

	if (ret < 0) {
		dev_err(dsp->dev, "cant't find free blocks for module text\n");
		return -ENOMEM;
	}

	/* get DSP memory for module data */
	if (module->data_fixed)
		ret = module_get_fixed_dblocks(module, bmap);
	else
		ret = module_get_dblocks(module, bmap);

	if (ret < 0) {
		dev_err(dsp->dev, "cant't find free blocks for module data\n");
		return -ENOMEM;
	}

	/* insert module data and text */
	ret = module_insert_text(module, bmap);
	if (ret < 0)
		return ret;

	ret = module_insert_data(module, bmap);
	if (ret < 0) {
		module_remove_text(module, bmap);
		return ret;
	}

	mutex_unlock(&dsp->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_module_insert);

int sst_module_remove(struct sst_module *module, struct sst_bmap *bmap)
{
	struct sst_dsp *dsp = module->dsp;

	mutex_lock(&dsp->mutex);
	module_remove_text(module, bmap);
	module_remove_data(module, bmap);
	mutex_unlock(&dsp->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_module_remove);

static int module_get_section_block(struct sst_module *module,
	struct sst_bmap *bmap, u32 offset, u32 size)
{
	struct sst_mem_block *block, *tmp;
	u32 end = offset + size;

	/* are blocks already atteched to the module */
	list_for_each_entry_safe(block, tmp, &module->tblock_list, module_tlist) {

		/* find block that holds section */
		if (block->offset < offset || block->offset + block->size > end)
			continue;

		return 0;	
	}

	/* find first free blocks that can hold section in free list*/
	list_for_each_entry_safe(block, tmp, &bmap->free_block_list, map_list) {

		/* find block that holds section */
		if (block->offset < offset || block->offset + block->size > end)
			continue;

		/* add block */
		list_add(&block->module_tlist, &module->tblock_list);
		list_move(&block->map_list, &bmap->used_block_list);
		return 0;
	
	}

	return -ENOMEM;
}

static int module_insert_section(struct sst_module *module,
	struct sst_bmap *bmap, u32 offset, u32 size, void *data)
{
	struct sst_mem_block *block;
	int ret = -ENODEV;

	/* enable each block so that's it'e ready for module data */
	list_for_each_entry(block, &module->tblock_list, module_tlist) {

		if (block->ops && block->ops->enable)
			ret = block->ops->enable(block);
			if (ret < 0)
				goto err;
	}

	return 0;

err:
	module_remove_data(module, bmap);
	return ret;
}

int sst_module_insert_section(struct sst_module *module, struct sst_bmap *bmap,
	u32 offset, u32 size, void *data)
{
	struct sst_dsp *dsp = bmap->dsp;
	int ret;

	mutex_lock(&dsp->mutex);

	/* get block that includes this section */
	ret = module_get_section_block(module, bmap, offset, size);
	if (ret < 0) {
		dev_err(dsp->dev, "cant't find free blocks for section\n");
		return -ENOMEM;
	}

	/* insert module data and text */
	ret = module_insert_section(module, bmap, offset, size, data);
	if (ret < 0)
		return ret;

	mutex_unlock(&dsp->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_module_insert_section);

struct sst_mem_block *sst_mem_block_register(struct sst_bmap *bmap, u32 offset,
	u32 size, enum sst_mem_type type, struct sst_block_ops *ops,
	void *private)
{
	struct sst_dsp *dsp = bmap->dsp;
	struct sst_mem_block *block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (block == NULL)
		return NULL;

	block->offset = offset;
	block->size = size;
	block->type = type;
	block->dsp = dsp;
	block->private = private;
	block->ops = ops;

	mutex_lock(&dsp->mutex);
	list_add(&block->list, &bmap->free_block_list);
	mutex_unlock(&dsp->mutex);

	return block;
}
EXPORT_SYMBOL_GPL(sst_mem_block_register);

void sst_mem_block_unregister_all(struct sst_bmap *bmap)
{
	struct sst_dsp *dsp = bmap->dsp;
	struct sst_mem_block *block, *tmp;

	mutex_lock(&dsp->mutex);

	/* unregister used blocks */
	list_for_each_entry_safe(block, tmp, &bmap->used_block_list, map_list) {
		list_del(&block->list);
		kfree(block);
	}
	
	list_for_each_entry_safe(block, tmp, &bmap->free_block_list, map_list) {
		list_del(&block->list);
		kfree(block);
	}

	mutex_unlock(&dsp->mutex);
	
}
EXPORT_SYMBOL_GPL(sst_mem_block_unregister_all);

static void sst_memcpy32(struct sst_dsp *dsp, void *dest, void *src,
	int bytes)
{
	u32 *src32 = src, *dest32 = dest;
	int size, i;

	size = bytes >> 2;

	/* copy word at a time */
	for (i = 0; i < size; i++) {
		*dest32 = *src32;
		dest32++;
		src32++;
	}
}

int sst_fw_copy(struct sst_dsp *dsp, void *dest, void *src, int bytes)
{
/* TODO: implement logioc for DMA */
#if 0
	memcpy_toio(ram + block->ram_offset,
		(void *)block + sizeof(*block), block->size);
#else
	/* TODO: use generic SST shim drv copy for this */
	sst_memcpy32(dsp, dest, src, bytes);
#endif
	return 0;
}
EXPORT_SYMBOL_GPL(sst_fw_copy);


