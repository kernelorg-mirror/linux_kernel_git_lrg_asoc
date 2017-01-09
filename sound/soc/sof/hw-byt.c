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

#include "../intel/common/sst-dsp.h"
#include "../intel/common/sst-dsp-priv.h"
#include "../haswell/sst-haswell-ipc.h"

#include <trace/events/hswadsp.h>

#define SST_BYT_IRAM_OFFSET	0xC0000
#define SST_BYT_DRAM_OFFSET	0x100000
#define SST_BYT_SHIM_OFFSET	0x140000

#define SST_BYT_SHIM_OFFSET	0x140000
#define SST_BYT_DSP_DRAM_OFFSET	0x28000
#define SST_BYT_DSP_IRAM_OFFSET	0x00000

#define SST_SHIM_PM_REG		0x84

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

static void byt_notify(struct sst_dsp *dsp)
{
	sst_dsp_shim_update_bits64(dsp, SST_IPCD,
		SST_BYT_IPCD_BUSY | SST_BYT_IPCD_DONE,
		SST_BYT_IPCD_DONE);
}

static bool byt_is_dsp_busy(struct sst_dsp *dsp)
{
	u64 ipcx;

	ipcx = sst_dsp_shim_read64_unlocked(dsp, SST_IPCX);
	return (ipcx & (SST_BYT_IPCX_BUSY | SST_BYT_IPCX_DONE));
}

static void byt_shim_dbg(struct sst_generic_ipc *ipc, const char *text)
{
	struct sst_dsp *sst = ipc->dsp;
	u64 ipcd, ipcx ,isrd, imrx, isrx, imrd;
	int i;

	ipcx = sst_dsp_shim_read64_unlocked(sst, SST_IPCX);
	ipcd = sst_dsp_shim_read64_unlocked(sst, SST_IPCD);
	isrx = sst_dsp_shim_read64_unlocked(sst, SST_ISRX);
	isrd = sst_dsp_shim_read64_unlocked(sst, SST_ISRD);
	imrx = sst_dsp_shim_read64_unlocked(sst, SST_IMRX);
	imrd = sst_dsp_shim_read64_unlocked(sst, SST_IMRD);

	dev_err(ipc->dev,
		"ipc: --%s--\n ipcx 0x%16llx\n ipcd 0x%16llx\n"
		" imrx 0x%16llx\n imrd 0x%16llx\n"
		" isrx 0x%16llx\n isrd 0x%16llx\n",
		text, ipcx, ipcd, imrx, imrd, isrx, isrd);

	for (i = 0; i < 0xff; i+=8 ) {
		dev_err(ipc->dev, "shim 0x%2.2x value 0x%16.16llx\n",i, 
			sst_dsp_shim_read64_unlocked(sst, i));
	}

	for (i = 10; i < 30; i++) {
		dev_err(sst->dev, "mbox: %d value 0x%8.8x\n", i,
			readl(sst->addr.lpe + i * 4 + 0x144000 + 0x900));
	}
}

static void byt_tx_msg(struct sst_generic_ipc *ipc, struct ipc_message *msg)
{
	/* send the message */
	sst_dsp_outbox_write(ipc->dsp, msg->tx_data, msg->tx_size);
	sst_dsp_ipc_msg64_tx(ipc->dsp, msg->header);
}

static irqreturn_t byt_irq_thread(int irq, void *context)
{
	struct sst_dsp *sst = (struct sst_dsp *) context;
	struct sst_hsw *hsw = sst_dsp_get_thread_context(sst);
	struct sst_generic_ipc *ipc = &hsw->ipc;
	u64 ipcx, ipcd;
	unsigned long flags;

	spin_lock_irqsave(&sst->spinlock, flags);

	ipcx = sst_dsp_ipc_msg64_rx(hsw->dsp);
	ipcd = sst_dsp_shim_read64_unlocked(sst, SST_IPCD);

	/* reply message from DSP */
	if (ipcx & SST_BYT_IPCX_DONE) {

		/* Handle Immediate reply from DSP Core */
		hsw_process_reply(hsw, ipcx);

		/* clear DONE bit - tell DSP we have completed */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IPCX,
			SST_BYT_IPCX_DONE, 0);

		/* unmask Done interrupt */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IMRX,
			SST_IMRX_DONE, 0);
	}

	/* new message from DSP */
	if (ipcd & SST_BYT_IPCD_BUSY) {

		/* Handle Notification and Delayed reply from DSP Core */
		hsw_process_notification(hsw, ipcd);

		/* clear BUSY bit and set DONE bit - accept new messages */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IPCD,
			SST_BYT_IPCD_BUSY | SST_BYT_IPCD_DONE,
			SST_BYT_IPCD_DONE);

		/* unmask busy interrupt */
		sst_dsp_shim_update_bits64_unlocked(sst, SST_IMRX,
			SST_IMRX_BUSY, 0);
	}

	spin_unlock_irqrestore(&sst->spinlock, flags);

	/* continue to send any remaining messages... */
	queue_kthread_work(&ipc->kworker, &ipc->kwork);

	return IRQ_HANDLED;
}

static const struct sst_debugfs_map debugfs_byt[] = {
	{"dmac0", 0x98000, 0x420},
	{"dmac1", 0x9c000, 0x420},
	{"ssp0", 0xa0000, 0x100},
	{"ssp1", 0xa1000, 0x100},
	{"ssp2", 0xa2000, 0x100},
	{"iram", 0xc0000, 80 * 1024},
	{"dram", 0x100000, 160 * 1024},
	{"shim", 0x140000, 0x100},
	{"mbox", 0x144000, 0x1000},
};

static struct sst_dsp_device byt_dev = {
	.thread = byt_irq_thread,
	.ops = &sst_baytrail_ops,
};

struct sst_ops sst_baytrail_ops = {
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
	.parse_fw = hsw_parse_fw_image,
};
