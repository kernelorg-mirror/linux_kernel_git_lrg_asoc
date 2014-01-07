/*
 * Intel SST Firmware Loader
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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/firmware.h>
#include <linux/export.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/pci.h>

/* supported DMA engine drivers */
#include <linux/dw_dmac.h>

#include <asm/page.h>
#include <asm/pgtable.h>

#include "sst-dsp.h"
#include "sst-dsp-priv.h"

#define	SST_DMA_RESOURCES 2

struct sst_dma {
	struct sst_dsp *sst;

	struct platform_device *dma_dev;
	struct resource dma_resource[SST_DMA_RESOURCES];
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *ch;
};

static void sst_dma_transfer_complete(void *arg)
{
	struct sst_dsp *sst = (struct sst_dsp *)arg;

	dev_dbg(sst->dev, "SST DMA: callback\n");
}

int sst_dsp_dma_copy(struct sst_dsp *sst, dma_addr_t src_addr,
	dma_addr_t dest_addr, size_t size)
{
	struct dma_async_tx_descriptor *desc;
	struct sst_dma *dma = sst->dma;

	if (dma->ch == NULL) {
		dev_err(sst->dev, "DMA no channel\n");
		return -ENODEV;
	}

	dev_dbg(sst->dev, "DMA: src: 0x%lx dest 0x%lx size %zu\n",
		(unsigned long)src_addr, (unsigned long)dest_addr, size);

	desc = dma->ch->device->device_prep_dma_memcpy(dma->ch, dest_addr,
		src_addr, size, DMA_CTRL_ACK);
	if (!desc){
		dev_err(sst->dev, "dma prep memcpy error\n");
		return -EINVAL;
	}

	desc->callback = sst_dma_transfer_complete;
	desc->callback_param = sst;

	desc->tx_submit(desc);
	dma_wait_for_async_tx(desc);

	return 0;
}
EXPORT_SYMBOL_GPL(sst_dsp_dma_copy);

static bool dma_chan_filter(struct dma_chan *chan, void *param)
{
	struct sst_dsp *dsp = (struct sst_dsp *)param;
	struct sst_dma *dma = dsp->dma;

	/* only accept channels from this device */
	if (chan->device->dev != &dma->dma_dev->dev)
		return false;

	/* todo: add chan_id testing */
	return true;
}

int sst_dsp_dma_get_channel(struct sst_dsp *dsp, int chan_id)
{
	struct sst_dma *dma = dsp->dma;
	struct dma_slave_config slave;
	dma_cap_mask_t mask;
	int ret;

	/* The Intel MID DMA engine driver needs the slave config set but
	 * Synopsis DMA engine driver safely ignores the slave config */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_MEMCPY, mask);

	dma->ch = dma_request_channel(mask, dma_chan_filter, dsp);
	if (dma->ch == NULL) {
		dev_err(dsp->dev, "dma_request_channel failed\n");
		return -EIO;
	}

	memset(&slave, 0, sizeof(slave));
	slave.direction = DMA_MEM_TO_DEV;
	slave.src_addr_width =
		slave.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	slave.src_maxburst = slave.dst_maxburst = dsp->pdata->dma_burst;

	ret = dmaengine_slave_config(dma->ch, &slave);
	if (ret) {
		dev_err(dsp->dev, "unable to set slave config %d\n", ret);
		dma_release_channel(dma->ch);
		dma->ch = NULL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sst_dsp_dma_get_channel);

void sst_dsp_dma_put_channel(struct sst_dsp *dsp)
{
	struct sst_dma *dma = dsp->dma;

	dma_release_channel(dma->ch);
	dma->ch = NULL;
}
EXPORT_SYMBOL_GPL(sst_dsp_dma_put_channel);

static struct dw_dma_platform_data dw_pdata = {
	.no_hclk = true,
	.chan_allocation_order = CHAN_ALLOCATION_ASCENDING,
	.chan_priority = CHAN_PRIORITY_ASCENDING,
};

int sst_dsp_dma_new(struct sst_dsp *sst)
{
	struct sst_pdata *sst_pdata = sst->pdata;
	struct sst_dma *dma;
	const char *dma_dev_name;
	size_t dma_pdata_size;
	int ret;
	void *dma_pdata;

	switch (sst->pdata->dma_engine) {
	case SST_DMA_TYPE_DW:
		dma_pdata = &dw_pdata;
		dma_pdata_size = sizeof(dw_pdata);
		dma_dev_name = "dw_dmac";
		break;
	case SST_DMA_TYPE_MID:
		dma_pdata = NULL;
		dma_pdata_size = 0;
		dma_dev_name = "Intel MID DMA";
		break;
	default:
		dev_err(sst->dev, "error: invalid DMA engine %d\n",
			sst->pdata->dma_engine);
		return -EINVAL;
	}

	dma = kzalloc(sizeof(struct sst_dma), GFP_KERNEL);
	if (!dma)
		return -ENOMEM;

	dma->sst = sst;
	sst->dma = dma;

	dma->dma_resource[0].start = sst->addr.ram_base +
					sst_pdata->dma_base;
	dma->dma_resource[0].end   = sst->addr.ram_base +
					sst_pdata->dma_base +
					sst_pdata->dma_size;
	dma->dma_resource[0].flags = IORESOURCE_MEM;
	dma->dma_resource[1].start = sst_pdata->irq;
	dma->dma_resource[1].end = sst_pdata->irq;
	dma->dma_resource[1].flags = IORESOURCE_IRQ;

	dma->dma_dev = platform_device_register_resndata(sst->dev,
		dma_dev_name, -1, dma->dma_resource, 2,
		dma_pdata, dma_pdata_size);

	//dma->dma_dev =
	//	platform_device_register_simple(sst_pdata->dma_engine,
	//		-1, dma->dma_resource, 2);
	if (dma->dma_dev == NULL) {
		dev_err(sst->dev, "platform_device_register_simple failed\n");
		ret = -ENODEV;
		goto err;
	}

	//sst->fw_use_dma = true;
	return 0;

err:
	kfree(dma);
	return ret;
}
EXPORT_SYMBOL(sst_dsp_dma_new);

void sst_dma_free(struct sst_dma *dma)
{
	if (dma->ch)
		dma_release_channel(dma->ch);
	platform_device_unregister(dma->dma_dev);
	kfree(dma);
}
EXPORT_SYMBOL(sst_dma_free);

static int dma_test(struct sst_dsp *dsp)
{
	void *buf1, *buf2;
	dma_addr_t dbuf1, dbuf2;
	int i, size = 4096;

	buf1 = dma_alloc_coherent(dsp->dev, size, &dbuf1, GFP_DMA);
	if (buf1 == NULL) {
		dev_err(dsp->dev, "dma_alloc_coherent1 test failed\n");
		return -ENOMEM;
	}

	buf2 = dma_alloc_coherent(dsp->dev, size, &dbuf2, GFP_DMA);
	if (buf2 == NULL) {
		dev_err(dsp->dev, "dma_alloc_coherent2 test failed\n");
		return -ENOMEM;
	}

	memset(buf1, 0x1, size);

	sst_dsp_dma_get_channel(dsp, 0);

	sst_dsp_dma_copy(dsp, dbuf1, dbuf2, size);

	for (i = 0; i < size; i++) {
		char *c = buf2 + i;
		if (*c != 0x1) {
			printk(KERN_ERR "DMA failed at %d got 0x%x\n", i, *c);
			goto out;
		}
	}

out:
	sst_dsp_dma_put_channel(dsp);

	dma_free_coherent(dsp->dev, size, buf1, dbuf1);
	dma_free_coherent(dsp->dev, size, buf2, dbuf2);

	return 0;
}

struct sst_fw *sst_fw_new(struct sst_dsp *dsp, 
	const struct firmware *fw, void *private)
{
	struct sst_fw *sst_fw;
	int err;

	if (!dsp->ops->parse_fw)
		return NULL;

	sst_fw = kzalloc(sizeof(*sst_fw), GFP_KERNEL);
	if (sst_fw == NULL) {
		dev_err(dsp->dev, "sst_fw kzalloc failed\n");
		return NULL;
	}

	sst_fw->dsp = dsp;
	sst_fw->private = private;
	sst_fw->size = fw->size;

	err = dma_coerce_mask_and_coherent(dsp->dev, DMA_BIT_MASK(32));
	if (err < 0) {
		kfree(sst_fw);
		return NULL;
	}

	dma_test(dsp);

	sst_fw->dma_buf = dma_alloc_coherent(dsp->dev, sst_fw->size,
				&sst_fw->dmable_fw_paddr, GFP_DMA);
	if (!sst_fw->dma_buf) {
		dev_err(dsp->dev, "dma_alloc_coherent failed\n");
		kfree(sst_fw);
		return NULL;
	}

	memcpy((void *)sst_fw->dma_buf, (void *)fw->data, fw->size);
	release_firmware(fw);

	if (dsp->fw_use_dma) {
		err = sst_dsp_dma_get_channel(dsp, 0);
		if (err < 0)
			goto chan_err;
	}

	/* call core specific FW paser to load FW data into DSP */
	err = dsp->ops->parse_fw(sst_fw);
	if (err < 0) {
		dev_err(dsp->dev, "parse fw failed %d\n", err);
		goto parse_err;
	}

	if (dsp->fw_use_dma)
		sst_dsp_dma_put_channel(dsp);

	mutex_lock(&dsp->mutex);
	list_add(&sst_fw->list, &dsp->fw_list);
	mutex_unlock(&dsp->mutex);

	return sst_fw;

parse_err:
	if (dsp->fw_use_dma)
		sst_dsp_dma_put_channel(dsp);
chan_err:
	dma_free_coherent(dsp->dev, sst_fw->size,
				sst_fw->dma_buf,
				sst_fw->dmable_fw_paddr);
	kfree(sst_fw);
	return NULL;
}
EXPORT_SYMBOL_GPL(sst_fw_new);

void sst_fw_free(struct sst_fw *sst_fw)
{
	struct sst_dsp *dsp = sst_fw->dsp;

	mutex_lock(&dsp->mutex);
	list_del(&sst_fw->list);
	mutex_unlock(&dsp->mutex);

	dma_free_coherent(dsp->dev, sst_fw->size, sst_fw->dma_buf,
			sst_fw->dmable_fw_paddr);
	kfree(sst_fw);
}
EXPORT_SYMBOL_GPL(sst_fw_free);

void sst_fw_free_all(struct sst_dsp *dsp)
{
	struct sst_fw *sst_fw, *t;

	mutex_lock(&dsp->mutex);
	list_for_each_entry_safe(sst_fw, t, &dsp->fw_list, list) {

		list_del(&sst_fw->list);
		dma_free_coherent(dsp->dev, sst_fw->size, sst_fw->dma_buf,
			sst_fw->dmable_fw_paddr);
		kfree(sst_fw);
	}
	mutex_unlock(&dsp->mutex);
}
EXPORT_SYMBOL_GPL(sst_fw_free_all);

/* create new module object */
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

	memcpy(&sst_module->s, &template->s, sizeof(struct sst_module_data));
	memcpy(&sst_module->p, &template->p, sizeof(struct sst_module_data));

	INIT_LIST_HEAD(&sst_module->block_list);

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

/* allocate contiguous free DSP blocks for this module */
static int alloc_contiguous_blocks(struct sst_mem_block *parent,
	struct sst_module *module, struct sst_module_data *data,
	u32 next_offset, int size)
{
	struct sst_dsp *dsp = module->dsp;
	struct sst_mem_block *block, *tmp;
	int ret;

	/* find first free blocks that can hold text */
	list_for_each_entry_safe(block, tmp, &dsp->free_block_list, list) {
		/* ignore data blocks */
		if (block->type != data->type)
			continue;
		/* is block next after parent ? */
		if (next_offset == block->offset) {
			if (size > block->size) {
				/* need more blocks */
				ret = alloc_contiguous_blocks(block, module,
					data, block->offset + block->size,
					size - block->size);
				if (ret < 0)
					return ret;
			}

			/* add block */
			block->data_type = data->data_type;
			block->in_use = block->size;
			list_move(&block->list, &dsp->used_block_list);
			list_add(&block->module_list, &module->block_list);
			return 0;
		}
	}
	return -ENOMEM;
}

/* allocate free DSP blocks for drv managed module data */
static int module_alloc_drv_managed_blocks(struct sst_module *module,
	struct sst_module_data *data)
{
	struct sst_dsp *dsp = module->dsp;
	struct sst_mem_block *block, *tmp;
	struct sst_mem_block *partial_block;
	u32 diff;
	int ret = 0;

	if (data->size == 0)
		return 0;

	/* find first free blocks that can hold text */
	list_for_each_entry_safe(block, tmp, &dsp->free_block_list, list) {
		/* ignore blocks with wrong type */
		if (block->type != data->type)
			continue;
		if (block->size < data->size) {
			/* need more blocks */
			ret = alloc_contiguous_blocks(block, module, data,
				block->offset + block->size,
				data->size - block->size);
		}
		if (!ret) {
			/* add block */
			data->offset = block->offset;
			block->data_type = data->data_type;
			block->in_use = data->size % block->size;
			list_add(&block->module_list, &module->block_list);
			list_move(&block->list, &dsp->used_block_list);
			return 0;
		}
	}

	diff = block->size;
	partial_block = 0;
	list_for_each_entry_safe(block, tmp, &dsp->used_block_list, list) {
		if (block->type != data->type)
			continue;
		if (block->data_type != data->data_type)
			continue;

		if (block->size - block->in_use >= data->size
				&& block->size - block->in_use - data->size < diff) {
			diff = block->size - block->in_use - data->size;
			partial_block = block;
		}
	}
	if (partial_block) {
		data->offset = partial_block->offset + partial_block->in_use;
		partial_block->in_use += data->size;
		list_add(&partial_block->module_list, &module->block_list);

		return 0;
	}

	return -ENOMEM;
}


static void sst_memcpy32(void *dest, void *src, int bytes)
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

static void sst_validate32(void *dest, void *src, int bytes)
{
	u32 *src32 = src, *dest32 = dest;
	int size, i, count = 0;

	size = bytes >> 2;

	printk(KERN_ERR "memcpy: src: %p, dstn %p (pa: %lx)\n",
		src, dest, (long unsigned int)virt_to_phys(dest));

	/* copy word at a time */
	for (i = 0; i < size; i++) {
		if (*dest32 != *src32) {
			if (count++ == 0)
				printk(KERN_ERR "!! expected 0x%x got 0x%x at"
				" offset 0x%x\n", *dest32, *src32, i);
		}

		dest32++;
		src32++;
	}
	printk("block failed 0x%x times\n", count);
}

/* remove module from DSP memory */
static void module_remove(struct sst_module *module)
{
	struct sst_mem_block *block, *tmp;
	struct sst_dsp *dsp = module->dsp;
	int err;

	/* disable each block  */
	list_for_each_entry(block, &module->block_list, module_list) {

		if (block->ops && block->ops->disable) {
			err = block->ops->disable(block);
			if (err < 0)
				dev_err(dsp->dev, "failed to disable block\n");
		}
	}

	/* mark each block as free */
	list_for_each_entry_safe(block, tmp, &module->block_list, module_list) {
		list_del(&block->module_list);
		list_move(&block->list, &dsp->free_block_list);
	}
}

/* prepare the DSP memory block to receive data from host */
static int module_prepare_block(struct sst_module *module)
{
	struct sst_mem_block *block;
	int ret = 0;

	/* enable each block so that's it'e ready for module P/S data */
	list_for_each_entry(block, &module->block_list, module_list) {

		if (block->ops && block->ops->enable)
			ret = block->ops->enable(block);
			if (ret < 0)
				goto err;
	}
	return ret;

err:
	list_for_each_entry(block, &module->block_list, module_list) {
		if (block->ops && block->ops->disable)
			block->ops->disable(block);
	}
	return ret;
}

/* Unload entire module from DSP memory */
int sst_module_remove(struct sst_module *module)
{
	struct sst_dsp *dsp = module->dsp;

	mutex_lock(&dsp->mutex);
	module_remove(module);
	mutex_unlock(&dsp->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_module_remove);

/* allocate DSP memory blocks for partial module sections */
static int module_alloc_fixed_block(struct sst_module *module,
	struct sst_module_data *data)
{
	struct sst_dsp *dsp = module->dsp;
	struct sst_mem_block *block, *tmp;
	u32 end = data->offset + data->size, block_end;
	int err;

	/* are blocks already attached to this module */
	list_for_each_entry_safe(block, tmp, &module->block_list, module_list) {

		/* force compacting mem blocks of the same data_type */
		if (block->data_type != data->data_type)
			continue;

		block_end = block->offset + block->size;

		/* find block that holds section */
		if (data->offset >= block->offset && end < block_end)
			return 0;

		/* does block span more than 1 section */
		if (data->offset >= block->offset && data->offset < block_end) {

			err = alloc_contiguous_blocks(block, module, data,
				block->offset + block->size,
				data->size - block->size + data->offset - block->offset);
			if (err < 0)
				return -ENOMEM;

			/* module already owns blocks */
			return 0;
		}
	}

	/* find first free blocks that can hold section in free list*/
	list_for_each_entry_safe(block, tmp, &dsp->free_block_list, list) {
		block_end = block->offset + block->size;

		/* find block that holds section */
		if (data->offset >= block->offset && end < block_end) {

			/* add block */
			block->data_type = data->data_type;
			list_move(&block->list, &dsp->used_block_list);
			list_add(&block->module_list, &module->block_list);

			return 0;
		}

		/* does block span more than 1 section */
		if (data->offset >= block->offset && data->offset < block_end) {

			err = alloc_contiguous_blocks(block, module, data,
				block->offset + block->size,
				data->size - block->size);
			if (err < 0)
				return -ENOMEM;

			/* add block */
			block->data_type = data->data_type;
			list_move(&block->list, &dsp->used_block_list);
			list_add(&block->module_list, &module->block_list);

			return 0;
		}

	}

	return -ENOMEM;
}

/* Load fixed module data into DSP memory blocks */
int sst_module_insert_fixed_block(struct sst_module *module,
	struct sst_module_data *data)
{
	struct sst_dsp *dsp = module->dsp;
	struct sst_fw *sst_fw = module->sst_fw;
	int ret;

	mutex_lock(&dsp->mutex);

	/* alloc blocks that includes this section */
	ret = module_alloc_fixed_block(module, data);
	if (ret < 0) {
		dev_err(dsp->dev,
			"no free blocks for section at offset 0x%x size 0x%x\n",
			data->offset, data->size);
		mutex_unlock(&dsp->mutex);
		return -ENOMEM;
	}

	/* prepare DSP blocks for module copy */
	ret = module_prepare_block(module);
	if (ret < 0) {
		dev_err(dsp->dev, "module prepare failed\n");
		goto err;
	}

	/* copy partial module data to blocks */
	if (dsp->fw_use_dma) {
		ret = sst_dsp_dma_copy(dsp,
			sst_fw->dmable_fw_paddr + data->data_offset,
			dsp->addr.ram_base + data->offset, data->size);
		if (ret < 0) {
			dev_err(dsp->dev, "module copy failed\n");
			goto err;
		}
		sst_validate32(dsp->addr.ram + data->offset,
			sst_fw->dma_buf + data->data_offset, data->size);
	} else
		sst_memcpy32(dsp->addr.ram + data->offset, data->data, data->size);

	mutex_unlock(&dsp->mutex);
	return ret;

err:
	module_remove(module);
	mutex_unlock(&dsp->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_module_insert_fixed_block);

/* register a DSP memory block for use with FW based modules */
struct sst_mem_block *sst_mem_block_register(struct sst_dsp *dsp, u32 offset,
	u32 size, enum sst_mem_type type, struct sst_block_ops *ops, u32 index,
	void *private)
{
	struct sst_mem_block *block;

	block = kzalloc(sizeof(*block), GFP_KERNEL);
	if (block == NULL)
		return NULL;

	block->offset = offset;
	block->size = size;
	block->index = index;
	block->type = type;
	block->dsp = dsp;
	block->private = private;
	block->ops = ops;

	mutex_lock(&dsp->mutex);
	list_add(&block->list, &dsp->free_block_list);
	mutex_unlock(&dsp->mutex);

	return block;
}
EXPORT_SYMBOL_GPL(sst_mem_block_register);

/* unregister all DSP memory blocks */
void sst_mem_block_unregister_all(struct sst_dsp *dsp)
{
	struct sst_mem_block *block, *tmp;

	mutex_lock(&dsp->mutex);

	/* unregister used blocks */
	list_for_each_entry_safe(block, tmp, &dsp->used_block_list, list) {
		list_del(&block->list);
		kfree(block);
	}

	/* unregister free blocks */
	list_for_each_entry_safe(block, tmp, &dsp->free_block_list, list) {
		list_del(&block->list);
		kfree(block);
	}

	mutex_unlock(&dsp->mutex);
}
EXPORT_SYMBOL_GPL(sst_mem_block_unregister_all);

/* allocate persistent/scratch blocks */
int sst_mem_block_drv_managed_alloc(struct sst_dsp *dsp)
{
	struct sst_module *sst_module;
	struct sst_module scratch_mod;
	struct sst_mem_block *block, *tmp;
	u32 block_size;
	int ret = 0;

	/* calculate scratch size */
	memset(&scratch_mod, 0, sizeof(scratch_mod));
	list_for_each_entry(sst_module, &dsp->module_list, list) {
		scratch_mod.s.size = scratch_mod.s.size > sst_module->s.size
			? scratch_mod.s.size : sst_module->s.size;
	}

	/* do init */
	scratch_mod.dsp = dsp;
	scratch_mod.s.type = SST_MEM_DRAM;
	scratch_mod.s.data_type = SST_DATA_S;

	INIT_LIST_HEAD(&scratch_mod.block_list);
	if (!list_empty(&dsp->free_block_list))
		block = list_first_entry(&dsp->free_block_list, struct sst_mem_block, list);
	else
		block = list_first_entry(&dsp->used_block_list, struct sst_mem_block, list);
	block_size = block->size;

	/* try to allocate mem regions >= block_size */
	if (scratch_mod.s.size >= block_size) {
		ret = module_alloc_drv_managed_blocks(&scratch_mod, &scratch_mod.s);
		if (ret < 0) {
			dev_err(dsp->dev, "module_alloc_blocks for scratch mem failed\n");
			goto err;
		}
	}
	list_for_each_entry(sst_module, &dsp->module_list, list) {
		if (sst_module->p.size >= block_size) {
			ret = module_alloc_drv_managed_blocks(sst_module, &sst_module->p);
			if (ret < 0) {
				dev_err(dsp->dev,
						"module_alloc_blocks for persistent mem failed\n");
				goto err;
			}
		}
	}

	/* allocate remaining mem regions (size < block_size) */
	if (scratch_mod.s.size < block_size) {
		ret = module_alloc_drv_managed_blocks(&scratch_mod, &scratch_mod.s);
		if (ret < 0) {
			dev_err(dsp->dev, "module_alloc_blocks for scratch mem failed\n");
			goto err;
		}
	}
	list_for_each_entry(sst_module, &dsp->module_list, list) {
		if (sst_module->p.size < block_size) {
			ret = module_alloc_drv_managed_blocks(sst_module, &sst_module->p);
			if (ret < 0) {
				dev_err(dsp->dev,
						"module_alloc_blocks for persistent mem failed\n");
				goto err;
			}
		}
	}

	/* assign the same offset of scratch to each module */
	list_for_each_entry(sst_module, &dsp->module_list, list)
		sst_module->s.offset = scratch_mod.s.offset;

	/* do cleanup */
	list_for_each_entry_safe(block, tmp, &scratch_mod.block_list, module_list)
		list_del(&block->module_list);

	return ret;

err:
	sst_mem_block_drv_managed_free(dsp);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_mem_block_drv_managed_alloc);

/* free all persistent/scratch blocks */
void sst_mem_block_drv_managed_free(struct sst_dsp *dsp)
{
	struct sst_mem_block *block, *tmp;

	list_for_each_entry_safe(block, tmp, &dsp->used_block_list, list) {
		if (block->data_type == SST_DATA_P) {
			list_del(&block->module_list);
		}
		if (block->data_type == SST_DATA_P || block->data_type == SST_DATA_S) {
			list_move(&block->list, &dsp->free_block_list);
			if (block->ops && block->ops->disable)
				block->ops->disable(block);
		}
	}
}
EXPORT_SYMBOL_GPL(sst_mem_block_drv_managed_free);

struct sst_module *sst_module_get_config(struct sst_dsp *dsp, u32 id)
{
	struct sst_module *module;

	mutex_lock(&dsp->mutex);

	list_for_each_entry(module, &dsp->module_list, list) {
		if (module->id == id) {
			break;
		}
	}

	mutex_unlock(&dsp->mutex);
	return module;
}
EXPORT_SYMBOL_GPL(sst_module_get_config);
