/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

static void hsw_notify(struct sst_dsp *dsp)
{
	sst_dsp_shim_update_bits(dsp, SST_IPCD,
		SST_IPCD_BUSY | SST_IPCD_DONE, SST_IPCD_DONE);
}

static bool hsw_is_dsp_busy(struct sst_dsp *dsp)
{
	u32 ipcx;

	ipcx = sst_dsp_shim_read64_unlocked(dsp, SST_IPCX);
	return (ipcx & (SST_IPCX_BUSY | SST_IPCX_DONE));
}


static void hsw_shim_dbg(struct sst_generic_ipc *ipc, const char *text)
{
	struct sst_dsp *sst = ipc->dsp;
	u32 ipcd, ipcx ,isrd, imrx, isrx, imrd;
	int i;

	ipcx = sst_dsp_shim_read_unlocked(sst, SST_IPCX);
	ipcd = sst_dsp_shim_read_unlocked(sst, SST_IPCD);
	isrx = sst_dsp_shim_read_unlocked(sst, SST_ISRX);
	isrd = sst_dsp_shim_read_unlocked(sst, SST_ISRD);
	imrx = sst_dsp_shim_read_unlocked(sst, SST_IMRX);
	imrd = sst_dsp_shim_read_unlocked(sst, SST_IMRD);

	dev_err(ipc->dev,
		"ipc: --%s--\n ipcx 0x%8x\n ipcd 0x%8x\n"
		" imrx 0x%8x\n imrd 0x%8x\n"
		" isrx 0x%8x\n isrd 0x%8x\n",
		text, ipcx, ipcd, imrx, imrd, isrx, isrd);

	for (i = 0; i < 0xff; i+=8 ) {
		dev_err(ipc->dev, "shim 0x%2.2x value 0x%16.16llx\n",i,
			sst_dsp_shim_read64_unlocked(sst, i));
	}

	for (i = 0xa0000; i < 0xa0100; i+=4) {
		dev_err(sst->dev, "iram: 0x%x value 0x%8.8x\n", i - 0xa0000,
			readl(sst->addr.lpe + i));
	}

	for (i = 0x0; i < 0x100; i+=4) {
		dev_err(sst->dev, "dram: 0x%x value 0x%8.8x\n", i,
			readl(sst->addr.lpe + i));
	}

	for (i = 0x00; i < 0xff; i+=4) {
		dev_err(sst->dev, "pci: 0x%x value 0x%8.8x\n", i,
			readl(sst->addr.pci_cfg + i));
	}

	//TODO: need correct mailbox offset
	for (i = 0; i < 30; i++) {
		dev_err(sst->dev, "mbox: 0x%x value 0x%8.8x\n", i,
			readl(sst->addr.lpe + i * 4 + 0x9e000));
	}
}

static void hsw_tx_msg(struct sst_generic_ipc *ipc, struct ipc_message *msg)
{
	/* send the message */
	sst_dsp_outbox_write(ipc->dsp, msg->tx_data, msg->tx_size);
	sst_dsp_ipc_msg_tx(ipc->dsp, msg->header);
}

static irqreturn_t hsw_irq_thread(int irq, void *context)
{
	struct sst_dsp *sst = (struct sst_dsp *) context;
	struct sst_hsw *hsw = sst_dsp_get_thread_context(sst);
	struct sst_generic_ipc *ipc = &hsw->ipc;
	u32 ipcx, ipcd;
	unsigned long flags;

	spin_lock_irqsave(&sst->spinlock, flags);

	ipcx = sst_dsp_ipc_msg_rx(hsw->dsp);
	ipcd = sst_dsp_shim_read_unlocked(sst, SST_IPCD);

	/* reply message from DSP */
	if (ipcx & SST_IPCX_DONE) {

		/* Handle Immediate reply from DSP Core */
		hsw_process_reply(hsw, ipcx);

		/* clear DONE bit - tell DSP we have completed */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IPCX,
			SST_IPCX_DONE, 0);

		/* unmask Done interrupt */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IMRX,
			SST_IMRX_DONE, 0);
	}

	/* new message from DSP */
	if (ipcd & SST_IPCD_BUSY) {

		/* Handle Notification and Delayed reply from DSP Core */
		hsw_process_notification(hsw, ipcd);

		/* clear BUSY bit and set DONE bit - accept new messages */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IPCD,
			SST_IPCD_BUSY | SST_IPCD_DONE, SST_IPCD_DONE);

		/* unmask busy interrupt */
		sst_dsp_shim_update_bits_unlocked(sst, SST_IMRX,
			SST_IMRX_BUSY, 0);
	}

	spin_unlock_irqrestore(&sst->spinlock, flags);

	/* continue to send any remaining messages... */
	queue_kthread_work(&ipc->kworker, &ipc->kwork);

	return IRQ_HANDLED;
}

static const struct sst_debugfs_map debugfs_bdw[] = {
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

static struct sst_dsp_device hsw_dev = {
	.thread = hsw_irq_thread,
	.ops = &haswell_ops,
};

/* haswell ops */
struct snd_sof_dsp_ops snd_sof_hsw_ops = {


};
EXPORT_SYMBOL(snd_sof_hsw_ops);


/* broadwell ops */
struct snd_sof_dsp_ops snd_sof_bdw_ops = {

};
EXPORT_SYMBOL(snd_sof_bdw_ops);

