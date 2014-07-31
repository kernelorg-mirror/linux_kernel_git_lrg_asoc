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
#include <linux/clkdev.h>
#include <linux/clk-provider.h>

#include <asm/page.h>
#include <asm/pgtable.h>

#include "sst-dsp.h"
#include "sst-dsp-priv.h"

#define SST_DMA_RESOURCES	2
#define SST_DSP_DMA_MAX_BURST	0x3

#define SST_HSW_MASK_DMA_ADDR_DSP 0xfff00000

struct sst_dma {
	struct sst_dsp *sst;

	struct platform_device *dma_dev;
	struct resource dma_resource[SST_DMA_RESOURCES];
	struct dma_async_tx_descriptor *desc;
	struct dma_chan *ch;
};
static void sst_memcpy32(volatile void __iomem *dest, void *src, u32 bytes)
{
	u32 i;

	/* copy one 32 bit word at a time as 64 bit access is not supported */
	for (i = 0; i < bytes; i += 4)
		memcpy_toio(dest + i, src + i, 4);
}

static void sst_dma_transfer_complete(void *arg)
{
	struct sst_dsp *sst = (struct sst_dsp *)arg;

	dev_dbg(sst->dev, "DMA: callback\n");
}

static int sst_dsp_dma_copy(struct sst_dsp *sst, dma_addr_t dest_addr,
	dma_addr_t src_addr, size_t size)
{
	struct dma_async_tx_descriptor *desc;
	struct sst_dma *dma = sst->dma;

	if (dma->ch == NULL) {
		dev_err(sst->dev, "error: no DMA channel\n");
		return -ENODEV;
	}

	dev_dbg(sst->dev, "DMA: src: 0x%lx dest 0x%lx size %zu\n",
		(unsigned long)src_addr, (unsigned long)dest_addr, size);

	desc = dma->ch->device->device_prep_dma_memcpy(dma->ch, dest_addr,
		src_addr, size, DMA_CTRL_ACK);
	if (!desc){
		dev_err(sst->dev, "error: dma prep memcpy failed\n");
		return -EINVAL;
	}

	desc->callback = sst_dma_transfer_complete;
	desc->callback_param = sst;

	desc->tx_submit(desc);
	dma_wait_for_async_tx(desc);

	return 0;
}

/* copy to DSP */
int sst_dsp_dma_copyto(struct sst_dsp *sst, dma_addr_t dest_addr,
	dma_addr_t src_addr, size_t size)
{
	return sst_dsp_dma_copy(sst, dest_addr | SST_HSW_MASK_DMA_ADDR_DSP,
			src_addr, size);
}
EXPORT_SYMBOL_GPL(sst_dsp_dma_copyto);

/* copy from DSP */
int sst_dsp_dma_copyfrom(struct sst_dsp *sst, dma_addr_t dest_addr,
	dma_addr_t src_addr, size_t size)
{
	return sst_dsp_dma_copy(sst, dest_addr,
		src_addr | SST_HSW_MASK_DMA_ADDR_DSP, size);
}
EXPORT_SYMBOL_GPL(sst_dsp_dma_copyfrom);

/* remove module from memory - callers hold locks */
static void block_list_remove(struct sst_dsp *dsp,
	struct list_head *block_list)
{
	struct sst_mem_block *block, *tmp;
	int err;

	/* disable each block  */
	list_for_each_entry(block, block_list, in_use_list) {

		if (block->ops && block->ops->disable) {
			err = block->ops->disable(block);
			if (err < 0)
				dev_err(dsp->dev,
					"error: cant disable block %d:%d\n",
					block->type, block->index);
		}
	}

	/* mark each block as free */
	list_for_each_entry_safe(block, tmp, block_list, in_use_list) {
		list_del(&block->in_use_list);
		list_move(&block->list, &dsp->free_block_list);
		dev_dbg(dsp->dev, "block freed %d:%d at offset 0x%x\n",
			block->type, block->index, block->offset);
	}
}

/* prepare the memory block to receive data from host - callers hold locks */
static int block_list_prepare(struct sst_dsp *dsp,
	struct list_head *block_list)
{
	struct sst_mem_block *block;
	int ret = 0;

	/* enable each block so that's it'e ready for data */
	list_for_each_entry(block, block_list, in_use_list) {

		if (block->ops && block->ops->enable) {
			ret = block->ops->enable(block);
			if (ret < 0) {
				dev_err(dsp->dev,
					"error: cant disable block %d:%d\n",
					block->type, block->index);
				goto err;
			}
		}
	}
	return ret;

err:
	list_for_each_entry(block, block_list, in_use_list) {
		if (block->ops && block->ops->disable)
			block->ops->disable(block);
	}
	return ret;
}

static bool dma_chan_filter(struct dma_chan *chan, void *param)
{
	struct sst_dsp *dsp = (struct sst_dsp *)param;
	struct sst_dma *dma = dsp->dma;

	/* only accept channels from this device */
	if (chan->device->dev != &dma->dma_dev->dev) {
		//dev_err(dma->sst->dev, "DMA: (chan->device->dev=0x%p != dev=0x%p)\n",
		//		chan->device->dev, &dma->dma_dev->dev);
		return false;
	}

	/* todo: add chan_id testing */
	return true;
}

int sst_dsp_dma_get_channel(struct sst_dsp *dsp, int chan_id)
{
	struct sst_dma *dma = dsp->dma;
	struct dma_slave_config slave;
	dma_cap_mask_t mask;
	int ret;

	dw_adsp_get(dma->dma_dev);

	/* The Intel MID DMA engine driver needs the slave config set but
	 * Synopsis DMA engine driver safely ignores the slave config */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_MEMCPY, mask);

	dma->ch = dma_request_channel(mask, dma_chan_filter, dsp);
	if (dma->ch == NULL) {
		dev_err(dsp->dev, "error: DMA request channel failed\n");
		dw_adsp_put(dma->dma_dev);
		return -EIO;
	}

	memset(&slave, 0, sizeof(slave));
	slave.direction = DMA_MEM_TO_DEV;
	slave.src_addr_width =
		slave.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	slave.src_maxburst = slave.dst_maxburst = SST_DSP_DMA_MAX_BURST;

	ret = dmaengine_slave_config(dma->ch, &slave);
	if (ret) {
		dev_err(dsp->dev, "error: unable to set DMA slave config %d\n",
			ret);
		dma_release_channel(dma->ch);
		dw_adsp_put(dma->dma_dev);
		dma->ch = NULL;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(sst_dsp_dma_get_channel);

void sst_dsp_dma_put_channel(struct sst_dsp *dsp)
{
	struct sst_dma *dma = dsp->dma;

	if (!dma->ch)
		return;

	dma_release_channel(dma->ch);
	dw_adsp_put(dma->dma_dev);
	dma->ch = NULL;
}
EXPORT_SYMBOL_GPL(sst_dsp_dma_put_channel);

/* platform data for DesignWare DMA Engine */
static struct dw_dma_platform_data dw_pdata = {
	.chan_allocation_order = CHAN_ALLOCATION_ASCENDING,
	.chan_priority = CHAN_PRIORITY_ASCENDING,
};

int sst_dma_new(struct sst_dsp *sst)
{
	struct sst_pdata *sst_pdata = sst->pdata;
	struct sst_dma *dma;
	const char *dma_dev_name;
	size_t dma_pdata_size;
	void *dma_pdata;
	bool need_dummy_clk = false;
	int ret = 0;

	/* configure the correct platform data for whatever DMA engine
	* is attached to the ADSP IP. */
	switch (sst->pdata->dma_engine) {
	case SST_DMA_TYPE_DW:
		dw_pdata.adsp_dev = sst->dma_dev;
		dw_pdata.need_adsp_mask = true;
		dw_pdata.adsp_mask = DMA_BIT_MASK(31);
		dma_pdata = &dw_pdata;
		dma_pdata_size = sizeof(dw_pdata);
		dma_dev_name = "dw_dmac";
		need_dummy_clk = true;
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

	dma = devm_kzalloc(sst->dev, sizeof(struct sst_dma), GFP_KERNEL);
	if (!dma)
		return -ENOMEM;

	dma->sst = sst;
	dma->dma_resource[0].start = sst->addr.lpe_base +
					sst_pdata->dma_base;
	dma->dma_resource[0].end   = sst->addr.lpe_base +
					sst_pdata->dma_base +
					sst_pdata->dma_size;
	dma->dma_resource[0].flags = IORESOURCE_MEM;
	dma->dma_resource[1].start = sst_pdata->irq;
	dma->dma_resource[1].end = sst_pdata->irq;
	dma->dma_resource[1].flags = IORESOURCE_IRQ;

	/* now register DMA engine device */
	dma->dma_dev = dw_adsp_register(sst->dev, dma_dev_name, dma->dma_resource, 2,
		dma_pdata, dma_pdata_size, need_dummy_clk);

	if (dma->dma_dev == NULL) {
		dev_err(sst->dev, "error: DMA device register failed\n");
		ret = -ENODEV;
		goto err_dma_dev;
	}

	dw_adsp_put(dma->dma_dev);
	sst->dma = dma;
	sst->fw_use_dma = true;
	return 0;

err_dma_dev:
	devm_kfree(sst->dev, dma);
	return ret;
}
EXPORT_SYMBOL(sst_dma_new);

void sst_dma_free(struct sst_dma *dma)
{

	if (dma == NULL)
		return;

	if (dma->ch)
		dma_release_channel(dma->ch);

	//if (dma->dma_dev)
	//	dw_adsp_unregister(dma->dma_dev);

}
EXPORT_SYMBOL(sst_dma_free);

/* create new generic firmware object */
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
	sst_fw->private = private;
	sst_fw->size = fw->size;

	/* allocate DMA buffer to store FW data */
	sst_fw->dma_buf = dma_alloc_coherent(dsp->dma_dev, sst_fw->size,
				&sst_fw->dmable_fw_paddr, GFP_DMA | GFP_KERNEL);
	if (!sst_fw->dma_buf) {
		dev_err(dsp->dev, "error: DMA alloc failed\n");
		kfree(sst_fw);
		return NULL;
	}

	/* copy FW data to DMA-able memory */
	memcpy((void *)sst_fw->dma_buf, (void *)fw->data, fw->size);

	if (dsp->fw_use_dma) {
		err = sst_dsp_dma_get_channel(dsp, 0);
		if (err < 0)
			goto chan_err;
	}

	/* call core specific FW paser to load FW data into DSP */
	err = dsp->ops->parse_fw(sst_fw);
	if (err < 0) {
		dev_err(dsp->dev, "error: parse fw failed %d\n", err);
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
	dma_free_coherent(dsp->dma_dev, sst_fw->size,
				sst_fw->dma_buf,
				sst_fw->dmable_fw_paddr);
	sst_fw->dma_buf = NULL;
	kfree(sst_fw);
	return NULL;
}
EXPORT_SYMBOL_GPL(sst_fw_new);

int sst_fw_reload(struct sst_fw *sst_fw)
{
	struct sst_dsp *dsp = sst_fw->dsp;
	int ret;

	dev_dbg(dsp->dev, "reloading firmware\n");

	/* call core specific FW paser to load FW data into DSP */
	ret = dsp->ops->parse_fw(sst_fw);
	if (ret < 0)
		dev_err(dsp->dev, "error: parse fw failed %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(sst_fw_reload);

void sst_fw_unload(struct sst_fw *sst_fw)
{
        struct sst_dsp *dsp = sst_fw->dsp;
        struct sst_module *module, *tmp;

        dev_dbg(dsp->dev, "unloading firmware\n");

        mutex_lock(&dsp->mutex);
        list_for_each_entry_safe(module, tmp, &dsp->module_list, list) {
                if (module->sst_fw == sst_fw) {
                        block_list_remove(dsp, &module->block_list);
                        list_del(&module->list);
                        kfree(module);
                }
        }

        mutex_unlock(&dsp->mutex);
}
EXPORT_SYMBOL_GPL(sst_fw_unload);

/* free single firmware object */
void sst_fw_free(struct sst_fw *sst_fw)
{
	struct sst_dsp *dsp = sst_fw->dsp;

	dev_dbg(dsp->dev, "freeing firmware\n");

	mutex_lock(&dsp->mutex);
	list_del(&sst_fw->list);
	mutex_unlock(&dsp->mutex);

	if (sst_fw->dma_buf)
		dma_free_coherent(dsp->dma_dev, sst_fw->size, sst_fw->dma_buf,
			sst_fw->dmable_fw_paddr);
	kfree(sst_fw);
}
EXPORT_SYMBOL_GPL(sst_fw_free);

/* free all firmware objects */
void sst_fw_free_all(struct sst_dsp *dsp)
{
	struct sst_fw *sst_fw, *t;

	dev_dbg(dsp->dev, "freeing all firmware\n");

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

/* create a new SST generic module from FW template */
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

	INIT_LIST_HEAD(&sst_module->block_list);
	INIT_LIST_HEAD(&sst_module->runtime_list);

	mutex_lock(&dsp->mutex);
	list_add(&sst_module->list, &dsp->module_list);
	mutex_unlock(&dsp->mutex);

	return sst_module;
}
EXPORT_SYMBOL_GPL(sst_module_new);

/* free firmware module and remove from available list */
void sst_module_free(struct sst_module *sst_module)
{
	struct sst_dsp *dsp = sst_module->dsp;

	mutex_lock(&dsp->mutex);
	list_del(&sst_module->list);
	mutex_unlock(&dsp->mutex);

	kfree(sst_module);
}
EXPORT_SYMBOL_GPL(sst_module_free);

struct sst_module_runtime *sst_module_runtime_new(struct sst_module *module,
	int id, void *private)
{
	struct sst_dsp *dsp = module->dsp;
	struct sst_module_runtime *runtime;

	runtime = kzalloc(sizeof(*runtime), GFP_KERNEL);
	if (runtime == NULL)
		return NULL;

	runtime->id = id;
	runtime->dsp = dsp;
	runtime->module = module;
	INIT_LIST_HEAD(&runtime->block_list);

	mutex_lock(&dsp->mutex);
	list_add(&runtime->list, &module->runtime_list);
	mutex_unlock(&dsp->mutex);

	return runtime;
}
EXPORT_SYMBOL_GPL(sst_module_runtime_new);

void sst_module_runtime_free(struct sst_module_runtime *runtime)
{
	struct sst_dsp *dsp = runtime->dsp;

	mutex_lock(&dsp->mutex);
	list_del(&runtime->list);
	mutex_unlock(&dsp->mutex);

	kfree(runtime);
}
EXPORT_SYMBOL_GPL(sst_module_runtime_free);

static struct sst_mem_block *find_block(struct sst_dsp *dsp,
	struct sst_block_allocator *ba)
{
	struct sst_mem_block *block;

	list_for_each_entry(block, &dsp->free_block_list, list) {
		if (block->type == ba->type && block->offset == ba->offset)
			return block;
	}

	return NULL;
}

/* Block allocator must be on block boundary */
static int block_alloc_contiguous(struct sst_dsp *dsp,
	struct sst_block_allocator *ba, struct list_head *block_list)
{
	struct list_head tmp = LIST_HEAD_INIT(tmp);
	struct sst_mem_block *block;
	u32 block_start = 0xffffffff;

	while (ba->size > 0) {
		block = find_block(dsp, ba);
		if (!block) {
			list_splice(&tmp, &dsp->free_block_list);
			return -ENOMEM;
		}

		list_move_tail(&block->list, &tmp);
		ba->offset += block->size;
		ba->size -= block->size;
	}

	list_for_each_entry(block, &tmp, list) {
		if (block->offset < block_start)
			block_start = block->offset;
		list_add(&block->in_use_list, block_list);

		dev_dbg(dsp->dev, "block allocated %d:%d at offset 0x%x\n",
			block->type, block->index, block->offset);

	}

	list_splice(&tmp, &dsp->used_block_list);
	return 0;
}

/* allocate free DSP blocks for module data - callers hold locks */
static int block_alloc(struct sst_dsp *dsp, struct sst_block_allocator *ba,
	struct list_head *block_list)
{
	struct sst_mem_block *block, *tmp;
	int ret = 0;

	if (ba->size == 0)
		return 0;

	/* find first free whole blocks that can hold module */
	list_for_each_entry_safe(block, tmp, &dsp->free_block_list, list) {

		/* ignore blocks with wrong type */
		if (block->type != ba->type)
			continue;

		if (ba->size > block->size)
			continue;

		ba->offset = block->offset;
		block->bytes_used = ba->size % block->size;
		list_add(&block->in_use_list, block_list);
		list_move(&block->list, &dsp->used_block_list);
		dev_dbg(dsp->dev, "block allocated %d:%d at offset 0x%x\n",
			block->type, block->index, block->offset);
		return 0;
	}

	/* then find free multiple blocks that can hold module */
	list_for_each_entry_safe(block, tmp, &dsp->free_block_list, list) {

		/* ignore blocks with wrong type */
		if (block->type != ba->type)
			continue;

		/* do we span > 1 blocks */
		if (ba->size > block->size) {
			ret = block_alloc_contiguous(dsp, ba, block_list);
			if (ret == 0)
				return ret;
		}
	}

	/* not enough free block space */
	return -ENOMEM;
}

int sst_alloc_blocks(struct sst_dsp *dsp, struct sst_block_allocator *ba,
	struct list_head *block_list)
{
	int ret;

	//ba->num_blocks = 0;

	dev_dbg(dsp->dev, "block request %d bytes at offset 0x%x type %d\n",
		ba->size, ba->offset, ba->type);

	mutex_lock(&dsp->mutex);

	ret = block_alloc(dsp, ba, block_list);
	if (ret < 0) {
		dev_err(dsp->dev, "error: can't alloc blocks %d\n", ret);
		goto out;
	}

	/* prepare DSP blocks for module usage */
	ret = block_list_prepare(dsp, block_list);
	if (ret < 0)
		dev_err(dsp->dev, "error: prepare failed\n");

out:
	mutex_unlock(&dsp->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_alloc_blocks);

int sst_free_blocks(struct sst_dsp *dsp, struct list_head *block_list)
{
	mutex_lock(&dsp->mutex);
	block_list_remove(dsp, block_list);
	mutex_unlock(&dsp->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_free_blocks);

/* allocate memory blocks for static module addresses - callers hold locks */
static int block_alloc_fixed(struct sst_dsp *dsp, struct sst_block_allocator *ba,
	struct list_head *block_list)
{
	struct sst_mem_block *block, *tmp;
	u32 end = ba->offset + ba->size, block_end;
	int err;

	/* only IRAM/DRAM blocks are managed */
	if (ba->type != SST_MEM_IRAM && ba->type != SST_MEM_DRAM)
		return 0;

	/* are blocks already attached to this module */
	list_for_each_entry_safe(block, tmp, block_list, in_use_list) {

		/* ignore blocks with wrong type */
		if (block->type != ba->type)
			continue;

		block_end = block->offset + block->size;

		/* find block that holds section */
		if (ba->offset >= block->offset && end < block_end)
			return 0;

		/* does block span more than 1 section */
		if (ba->offset >= block->offset && ba->offset < block_end) {

			ba->size -= block->size;
			ba->offset = block_end;
			err = block_alloc_contiguous(dsp, ba, block_list);
			if (err < 0)
				return -ENOMEM;

			/* module already owns blocks */
			return 0;
		}
	}

	/* find first free blocks that can hold section in free list */
	list_for_each_entry_safe(block, tmp, &dsp->free_block_list, list) {
		block_end = block->offset + block->size;

		/* ignore blocks with wrong type */
		if (block->type != ba->type)
			continue;

		/* find block that holds section */
		if (ba->offset >= block->offset && end < block_end) {

			/* add block */
			list_move(&block->list, &dsp->used_block_list);
			list_add(&block->in_use_list, block_list);
			dev_dbg(dsp->dev, "block allocated %d:%d at offset 0x%x\n",
				block->type, block->index, block->offset);
			return 0;
		}

		/* does block span more than 1 section */
		if (ba->offset >= block->offset && ba->offset < block_end) {

			ba->offset = block->offset;
			err = block_alloc_contiguous(dsp, ba, block_list);
			if (err < 0)
				return -ENOMEM;

			return 0;
		}
	}

	return -ENOMEM;
}

/* Load fixed module data into DSP memory blocks */
int sst_module_alloc_blocks(struct sst_module *module)
{
	struct sst_dsp *dsp = module->dsp;
	struct sst_fw *sst_fw = module->sst_fw;
	struct sst_block_allocator ba;
	int ret;

	ba.size = module->size;
	ba.type = module->type;
	ba.offset = module->offset;
//	ba.num_blocks = 0;

	dev_dbg(dsp->dev, "block request 0x%x bytes at offset 0x%x type %d\n",
		ba.size, ba.offset, ba.type);

	mutex_lock(&dsp->mutex);

	/* alloc blocks that includes this section */
	ret = block_alloc_fixed(dsp, &ba, &module->block_list);
	if (ret < 0) {
		dev_err(dsp->dev,
			"error: no free blocks for section at offset 0x%x size 0x%x\n",
			module->offset, module->size);
		mutex_unlock(&dsp->mutex);
		return -ENOMEM;
	}

	/* prepare DSP blocks for module copy */
	ret = block_list_prepare(dsp, &module->block_list);
	if (ret < 0) {
		dev_err(dsp->dev, "error: fw module prepare failed\n");
		goto err;
	}

	/* copy partial module data to blocks */
	if (dsp->fw_use_dma) {
		ret = sst_dsp_dma_copyto(dsp,
			dsp->addr.lpe_base + module->offset,
			sst_fw->dmable_fw_paddr + module->data_offset,
			module->size);
		if (ret < 0) {
			dev_err(dsp->dev, "error: module copy failed\n");
			goto err;
		}
	} else
		sst_memcpy32(dsp->addr.lpe + module->offset, module->data,
			module->size);

	mutex_unlock(&dsp->mutex);
	return ret;

err:
	block_list_remove(dsp, &module->block_list);
	mutex_unlock(&dsp->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_module_alloc_blocks);

/* Unload entire module from DSP memory */
int sst_module_free_blocks(struct sst_module *module)
{
	struct sst_dsp *dsp = module->dsp;

	mutex_lock(&dsp->mutex);
	block_list_remove(dsp, &module->block_list);
	mutex_unlock(&dsp->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_module_free_blocks);

int sst_module_runtime_alloc_blocks(struct sst_module_runtime *runtime)
{
	struct sst_dsp *dsp = runtime->dsp;
	struct sst_module *module = runtime->module;
	struct sst_block_allocator ba;
	int ret;

	ba.size = module->persistent_size;
	ba.type = SST_MEM_DRAM;
	ba.offset = module->offset;
//	ba.num_blocks = 0;

	dev_dbg(dsp->dev, "block request %d bytes at offset 0x%x type %d\n",
		ba.size, ba.offset, ba.type);

	mutex_lock(&dsp->mutex);

	/* alloc blocks that includes this section */
	ret = block_alloc_fixed(dsp, &ba, &runtime->block_list);
	if (ret < 0) {
		dev_err(dsp->dev,
			"error: no free blocks for runtime module size 0x%x\n",
			module->persistent_size);
		mutex_unlock(&dsp->mutex);
		return -ENOMEM;
	}

	/* prepare DSP blocks for module copy */
	ret = block_list_prepare(dsp, &runtime->block_list);
	if (ret < 0) {
		dev_err(dsp->dev, "error: runtime block prepare failed\n");
		goto err;
	}

	mutex_unlock(&dsp->mutex);
	return ret;

err:
	block_list_remove(dsp, &module->block_list);
	mutex_unlock(&dsp->mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(sst_module_runtime_alloc_blocks);

int sst_module_runtime_free_blocks(struct sst_module_runtime *runtime)
{
	struct sst_dsp *dsp = runtime->dsp;

	mutex_lock(&dsp->mutex);
	block_list_remove(dsp, &runtime->block_list);
	mutex_unlock(&dsp->mutex);
	return 0;
}
EXPORT_SYMBOL_GPL(sst_module_runtime_free_blocks);

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

/* allocate scratch buffer blocks */
int sst_block_alloc_scratch(struct sst_dsp *dsp)
{
	struct sst_module *module;
	struct sst_block_allocator ba;
	u32 scratch = 0;
	int ret;

	mutex_lock(&dsp->mutex);

	/* calculate required scratch size */
	list_for_each_entry(module, &dsp->module_list, list) {
		if (scratch < module->scratch_size)
			scratch = module->scratch_size;
	}

	dev_dbg(dsp->dev, "scratch buffer required is %d bytes\n", scratch);

	ba.size = scratch;
	ba.type = SST_MEM_DRAM;
	ba.offset = 0;

	dev_dbg(dsp->dev, "block request %d bytes at offset 0x%x type %d\n",
		ba.size, ba.offset, ba.type);

	/* allocate blocks for module scratch buffers */
	dev_dbg(dsp->dev, "allocating scratch blocks\n");
	ret = block_alloc(dsp, &ba, &dsp->scratch_block_list);
	if (ret < 0) {
		dev_err(dsp->dev, "error: can't alloc scratch blocks\n");
		return ret;
	}

	/* assign the same offset of scratch to each module */
	list_for_each_entry(module, &dsp->module_list, list)
		dsp->scratch_offset = ba.offset;

	mutex_unlock(&dsp->mutex);
	return scratch;
}
EXPORT_SYMBOL_GPL(sst_block_alloc_scratch);

/* free all scratch blocks */
void sst_block_free_scratch(struct sst_dsp *dsp)
{
	mutex_lock(&dsp->mutex);
	block_list_remove(dsp, &dsp->scratch_block_list);
	mutex_unlock(&dsp->mutex);
}
EXPORT_SYMBOL_GPL(sst_block_free_scratch);

/* get a module from it's unique ID */
struct sst_module *sst_module_get_from_id(struct sst_dsp *dsp, u32 id)
{
	struct sst_module *module;

	mutex_lock(&dsp->mutex);

	list_for_each_entry(module, &dsp->module_list, list) {
		if (module->id == id) {
			mutex_unlock(&dsp->mutex);
			return module;
		}
	}

	mutex_unlock(&dsp->mutex);
	return NULL;
}
EXPORT_SYMBOL_GPL(sst_module_get_from_id);

struct sst_module_runtime *sst_module_runtime_get_from_id(
	struct sst_module *module, u32 id)
{
	struct sst_module_runtime *runtime;
	struct sst_dsp *dsp = module->dsp;

	mutex_lock(&dsp->mutex);

	list_for_each_entry(runtime, &module->runtime_list, list) {
		if (runtime->id == id) {
			mutex_unlock(&dsp->mutex);
			return runtime;
		}
	}

	mutex_unlock(&dsp->mutex);
	return NULL;
}
EXPORT_SYMBOL_GPL(sst_module_runtime_get_from_id);

/* returns block address in DSP address space */
u32 sst_dsp_get_offset(struct sst_dsp *dsp, u32 offset,
	enum sst_mem_type type)
{
	switch (type) {
	case SST_MEM_IRAM:
		return offset - dsp->addr.iram_offset +
			dsp->addr.dsp_iram_offset;
	case SST_MEM_DRAM:
		return offset - dsp->addr.dram_offset +
			dsp->addr.dsp_dram_offset;
	default:
		return 0;
	}
}
EXPORT_SYMBOL_GPL(sst_dsp_get_offset);
