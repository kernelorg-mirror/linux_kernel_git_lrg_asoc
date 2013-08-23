/*
 *  sst_dsp.c - Intel SST DSP driver
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

#include "sst_dsp.h"
#include "sst_dsp_priv.h"

#define CREATE_TRACE_POINTS
#include <trace/events/sst.h>

u32 sst_dsp_shim_read(struct sst_dsp *sst, u32 offset);
void sst_dsp_shim_write(struct sst_dsp *sst, u32 offset, u32 value);

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

static inline void _sst_memcpy_toio_64(volatile __iomem u64 *dest,
	u64 *src, size_t bytes)
{
	int i, lwords = bytes >> 3;

	for (i = 0; i < lwords; i++)
		writeq(src[i], dest + i);
}

static inline void _sst_memcpy_fromio_64(u64 *dest,
	volatile __iomem u64 *src, size_t bytes)
{
	int i, lwords = bytes >> 3;

	for (i = 0; i < lwords; i++)
		dest[i] = readq(src + i);
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

void sst_memcpy_toio_64(struct sst_dsp *sst, void *dest, void *src,
	size_t bytes)
{
	_sst_memcpy_toio_64(dest, src, bytes);
}

void sst_memcpy_fromio_64(struct sst_dsp *sst, void *dest, void *src,
	size_t bytes)
{
	_sst_memcpy_fromio_64(dest, src, bytes);
}

/* Public API */
void sst_dsp_shim_write(struct sst_dsp *sst, u32 offset, u32 value)
{
	spin_lock(&sst->spinlock);
	sst->ops->write(sst->addr.shim, offset, value);
	spin_unlock(&sst->spinlock);
}
EXPORT_SYMBOL(sst_dsp_shim_write);

u32 sst_dsp_shim_read(struct sst_dsp *sst, u32 offset)
{
	u32 val;

	spin_lock(&sst->spinlock);
	val = sst->ops->read(sst->addr.shim, offset);
	spin_unlock(&sst->spinlock);

	return val;
}
EXPORT_SYMBOL(sst_dsp_shim_read);

void sst_dsp_shim_write64(struct sst_dsp *sst, u32 offset, u64 value)
{
	spin_lock(&sst->spinlock);
	sst->ops->write64(sst->addr.shim, offset, value);
	spin_unlock(&sst->spinlock);
}
EXPORT_SYMBOL(sst_dsp_shim_write64);

u64 sst_dsp_shim_read64(struct sst_dsp *sst, u32 offset)
{
	u64 val;

	spin_lock(&sst->spinlock);
	val = sst->ops->read64(sst->addr.shim, offset);
	spin_unlock(&sst->spinlock);

	return val;
}
EXPORT_SYMBOL(sst_dsp_shim_read64);

void sst_dsp_shim_write_unlocked(struct sst_dsp *sst, u32 offset, u32 value)
{
	sst->ops->write(sst->addr.shim, offset, value);
}
EXPORT_SYMBOL(sst_dsp_shim_write_unlocked);

u32 sst_dsp_shim_read_unlocked(struct sst_dsp *sst, u32 offset)
{
	return sst->ops->read(sst->addr.shim, offset);
}
EXPORT_SYMBOL(sst_dsp_shim_read_unlocked);

void sst_dsp_shim_write64_unlocked(struct sst_dsp *sst, u32 offset, u64 value)
{
	sst->ops->write64(sst->addr.shim, offset, value);
}
EXPORT_SYMBOL(sst_dsp_shim_write64_unlocked);

u64 sst_dsp_shim_read64_unlocked(struct sst_dsp *sst, u32 offset)
{
	return sst->ops->read64(sst->addr.shim, offset);
}
EXPORT_SYMBOL(sst_dsp_shim_read64_unlocked);

int sst_dsp_shim_update_bits(struct sst_dsp *sst, u32 offset,
				u32 mask, u32 value)
{
	bool change;
	u32 old, new;

	spin_lock(&sst->spinlock);
	old = sst_dsp_shim_read_unlocked(sst, offset);

	new = (old & (~mask)) | (value & mask);

	change = (old != new);
	if (change)
		sst_dsp_shim_write_unlocked(sst, offset, new);

	spin_unlock(&sst->spinlock);
	return change;
}
EXPORT_SYMBOL(sst_dsp_shim_update_bits);

int sst_dsp_shim_update_bits64(struct sst_dsp *sst, u32 offset,
				u64 mask, u64 value)
{
	bool change;
	u64 old, new;

	spin_lock(&sst->spinlock);
	old = sst_dsp_shim_read64_unlocked(sst, offset);

	new = (old & (~mask)) | (value & mask);

	change = (old != new);
	if (change)
		sst_dsp_shim_write64_unlocked(sst, offset, new);

	spin_unlock(&sst->spinlock);
	return change;
}
EXPORT_SYMBOL(sst_dsp_shim_update_bits64);

int sst_dsp_shim_update_bits_unlocked(struct sst_dsp *sst, u32 offset,
				u32 mask, u32 value)
{
	bool change;
	unsigned int old, new;
	u32 ret;

	ret = sst_dsp_shim_read_unlocked(sst, offset);

	old = ret;
	new = (old & (~mask)) | (value & mask);

	change = (old != new);
	if (change)
		sst_dsp_shim_write_unlocked(sst, offset, new);

	return change;
}
EXPORT_SYMBOL(sst_dsp_shim_update_bits_unlocked);

int sst_dsp_shim_update_bits64_unlocked(struct sst_dsp *sst, u32 offset,
				u64 mask, u64 value)
{
	bool change;
	u64 old, new;

	old = sst_dsp_shim_read64_unlocked(sst, offset);

	new = (old & (~mask)) | (value & mask);

	change = (old != new);
	if (change)
		sst_dsp_shim_write64_unlocked(sst, offset, new);

	return change;
}
EXPORT_SYMBOL(sst_dsp_shim_update_bits64_unlocked);

void sst_dsp_dump(struct sst_dsp *sst)
{
	sst->ops->dump(sst);
}
EXPORT_SYMBOL(sst_dsp_dump);

void sst_dsp_reset(struct sst_dsp *sst)
{
	sst->ops->reset(sst);
}
EXPORT_SYMBOL(sst_dsp_reset);

int sst_dsp_boot(struct sst_dsp *sst)
{
	sst->ops->boot(sst);
	return 0;
}
EXPORT_SYMBOL(sst_dsp_boot);

void sst_dsp_ipc_msg_tx(struct sst_dsp *dsp, u32 msg)
{
	sst_dsp_shim_write(dsp, SST_IPCX, msg | SST_IPCX_BUSY);
	trace_sst_ipc_msg_tx(msg);
}
EXPORT_SYMBOL_GPL(sst_dsp_ipc_msg_tx);

u32 sst_dsp_ipc_msg_rx(struct sst_dsp *dsp)
{
	u32 msg;

	msg = sst_dsp_shim_read(dsp, SST_IPCX);
	trace_sst_ipc_msg_rx(msg);

	return msg;
}
EXPORT_SYMBOL_GPL(sst_dsp_ipc_msg_rx);

void sst_dsp_dram_write(struct sst_dsp *sst, void *src, u32 dest_offset,
	size_t bytes)
{
	sst->ops->dram_write(sst, sst->addr.dram + dest_offset, src, bytes);
}
EXPORT_SYMBOL(sst_dsp_dram_write);

void sst_dsp_dram_read(struct sst_dsp *sst, void *dest, u32 src_offset,
	size_t bytes)
{
	sst->ops->dram_read(sst, dest, sst->addr.dram + src_offset, bytes);
}
EXPORT_SYMBOL(sst_dsp_dram_read);

void sst_dsp_iram_write(struct sst_dsp *sst, void *src, u32 dest_offset,
	size_t bytes)
{
	sst->ops->iram_write(sst, sst->addr.iram + dest_offset, src, bytes);
}
EXPORT_SYMBOL(sst_dsp_iram_write);

void sst_dsp_iram_read(struct sst_dsp *sst, void *dest, u32 src_offset,
	size_t bytes)
{
	sst->ops->iram_read(sst, dest, sst->addr.iram + src_offset, bytes);
}
EXPORT_SYMBOL(sst_dsp_iram_read);

int sst_dsp_mailbox_init(struct sst_dsp *sst, u32 inbox_offset, size_t inbox_size,
	u32 outbox_offset, size_t outbox_size)
{
	sst->mailbox.in_base = sst->addr.dram + inbox_offset;
	sst->mailbox.out_base = sst->addr.dram + outbox_offset;
	sst->mailbox.in_size = inbox_size;
	sst->mailbox.out_size = outbox_size;
	return 0;
}
EXPORT_SYMBOL(sst_dsp_mailbox_init);

void sst_dsp_outbox_write(struct sst_dsp *sst, void *message, size_t bytes)
{
	int i;

	trace_sst_ipc_outbox_write(bytes);

	sst->ops->dram_write(sst, sst->mailbox.out_base, message, bytes);

	for (i = 0; i < bytes; i += 4)
		trace_sst_ipc_outbox_wdata(i, *(uint32_t *)(message + i));
}
EXPORT_SYMBOL(sst_dsp_outbox_write);

void sst_dsp_outbox_read(struct sst_dsp *sst, void *message, size_t bytes)
{
	int i;

	trace_sst_ipc_outbox_read(bytes);

	sst->ops->dram_read(sst, message, sst->mailbox.out_base, bytes);

	for (i = 0; i < bytes; i += 4)
		trace_sst_ipc_outbox_rdata(i, *(uint32_t *)(message + i));
}
EXPORT_SYMBOL(sst_dsp_outbox_read);

void sst_dsp_inbox_write(struct sst_dsp *sst, void *message, size_t bytes)
{
	int i;

	trace_sst_ipc_inbox_write(bytes);

	sst->ops->dram_write(sst, sst->mailbox.in_base, message, bytes);

	for (i = 0; i < bytes; i += 4)
		trace_sst_ipc_inbox_wdata(i, *(uint32_t *)(message + i));
}
EXPORT_SYMBOL(sst_dsp_inbox_write);

void sst_dsp_inbox_read(struct sst_dsp *sst, void *message, size_t bytes)
{
	int i;

	trace_sst_ipc_inbox_read(bytes);

	sst->ops->dram_read(sst, message, sst->mailbox.in_base, bytes);

	for (i = 0; i < bytes; i += 4)
		trace_sst_ipc_inbox_rdata(i, *(uint32_t *)(message + i));
}
EXPORT_SYMBOL(sst_dsp_inbox_read);

void *sst_dsp_get_thread_context(struct sst_dsp *sst)
{
	return sst->thread_context;
}
EXPORT_SYMBOL(sst_dsp_get_thread_context);

struct sst_dsp *sst_dsp_new(struct device *dev,
	struct sst_dsp_device *sst_dev, struct sst_pdata *pdata)
{
	struct sst_dsp *sst;
	int err;

	dev_dbg(dev, "initialising audio DSP id 0x%x\n", sst_dev->id);

	sst = kzalloc(sizeof(*sst), GFP_KERNEL);
	if (sst == NULL)
		return NULL;

	spin_lock_init(&sst->spinlock);
	sst->dev = dev;
	sst->thread_context = sst_dev->thread_context;
	sst->sst_dev = sst_dev;

	/* Init SST hardware and set core specific ops */
	switch (sst_dev->id) {
	case SST_DEV_ID_HSWULT:
		sst->ops = &hswult_ops;

		/* Initialise Haswell SST */
		err = sst->ops->init(sst, pdata);
		if (err < 0) {
			kfree(sst);
			return NULL;
		}
		break;
	// TODO Add other cores like MDLD, MRFLD
	default:
		dev_err(dev, "unknown SST device %d\n", sst_dev->id);
		kfree(sst);
		return NULL;
        }

	/* Register the ISR */
	err = request_threaded_irq(sst->irq, sst->ops->irq_handler,
		sst_dev->thread, IRQF_SHARED, "AudioDSP", sst);
	if (err) {
		if (sst->ops->free)
			sst->ops->free(sst);
		kfree(sst);
		return NULL;
	}

	// TODO register debugfs info for regdump

	return sst;
}
EXPORT_SYMBOL(sst_dsp_new);

void sst_dsp_free(struct sst_dsp *sst)
{
	free_irq(sst->irq, sst);
	if (sst->ops->free)
		sst->ops->free(sst);
	kfree(sst);
}
EXPORT_SYMBOL(sst_dsp_free);

/* Module information */
MODULE_AUTHOR("Names here");
MODULE_DESCRIPTION("Intel SST");
MODULE_LICENSE("GPL v2");
