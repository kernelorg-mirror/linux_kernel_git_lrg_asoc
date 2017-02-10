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

/*
 * Hardware interface for audio DSP on Byatrail, Braswell and Cherrytrail.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <trace/events/hswadsp.h>
#include <linux/device.h>

#include "sof.h"
#include "io.h"
#include "intel.h"

/* DSP memories */
#define BYT_IRAM_OFFSET		0x0C0000
#define BYT_IRAM_SIZE		(80 * 1024)
#define BYT_DRAM_OFFSET		0x100000
#define BYT_DRAM_SIZE		(160 * 1024)
#define BYT_SHIM_OFFSET		0x140000
#define BYT_SHIM_SIZE		0x100
#define BYT_MBOX_OFFSET		0x144000
#define BYT_MBOX_SIZE		0x1000

/* DSP peripherals */
#define BYT_DMAC0_OFFSET	0x098000
#define BYT_DMAC1_OFFSET	0x09c000
#define BYT_DMAC_SIZE		0x420
#define BYT_SSP0_OFFSET		0x0a0000
#define BYT_SSP1_OFFSET		0x0a1000
#define BYT_SSP2_OFFSET		0x0a2000
#define BYT_SSP_SIZE		0x100


/*
 * Debug
 */

#define BYT_MBOX_DUMP_SIZE	0x30

static const struct snd_sof_debugfs_map byt_debugfs[] = {
	{"dmac0", BYT_DMAC0_OFFSET, BYT_DMAC_SIZE},
	{"dmac1", BYT_DMAC1_OFFSET, BYT_DMAC_SIZE},
	{"ssp0", BYT_SSP0_OFFSET, BYT_SSP_SIZE},
	{"ssp1", BYT_SSP1_OFFSET, BYT_SSP_SIZE},
	{"ssp2", BYT_SSP2_OFFSET, BYT_SSP_SIZE},
	{"iram", BYT_IRAM_OFFSET, BYT_IRAM_SIZE},
	{"dram", BYT_DRAM_OFFSET, BYT_DRAM_SIZE},
	{"shim", BYT_SHIM_OFFSET, BYT_SHIM_SIZE},
	{"mbox", BYT_MBOX_OFFSET, BYT_MBOX_SIZE},
};

static void byt_dump(struct snd_sof_dev *sdev, u32 flags)
{
	int i;

	if (flags & SOF_DBG_REGS) {
		for (i = 0; i < BYT_SHIM_SIZE; i+=8 ) {
			dev_dbg(sdev->dev, "shim 0x%2.2x value 0x%16.16llx\n",
				i, snd_sof_dsp_read64(sdev, i));
		}
	}

	if (flags & SOF_DBG_MBOX) {
		for (i = 0; i < BYT_MBOX_DUMP_SIZE; i++) {
			dev_dbg(sdev->dev, "mbox: %d value 0x%8.8x\n", i,
				readl(sdev->dsp_base + i * 4 + BYT_MBOX_OFFSET));
		}
	}
}

/*
 * Register IO
 */

static void byt_write(struct snd_sof_dev *sdev, void __iomem *addr,
	u32 value)
{
	writel(value, addr);
}

static u32 byt_read(struct snd_sof_dev *sdev, void __iomem *addr)
{
	return readl(addr);
}

static void byt_write64(struct snd_sof_dev *sdev, void __iomem *addr,
	u64 value)
{
	memcpy_toio(addr, &value, sizeof(value));
}

static u64 byt_read64(struct snd_sof_dev *sdev, void __iomem *addr)
{
	u64 val;

	memcpy_fromio(&val, addr, sizeof(val));
	return val;
}

/*
 * Memory copy.
 */

static void byt_block_write(struct snd_sof_dev *sdev,
	volatile u32 __iomem *dest, u32 *src, size_t bytes)
{
	unsigned i, words = bytes >> 2;

	for (i = 0; i < words; i++)
		writel(src[i], dest + i);
}

static void byt_block_read(struct snd_sof_dev *sdev, u32 *dest,
	volatile u32 __iomem *src, size_t bytes)
{
	unsigned i, words = bytes >> 2;

	for (i = 0; i < words; i++)
		dest[i] = readl(src + i);
}

/*
 * IPC Mailbox IO
 */

static void byt_mailbox_write(struct snd_sof_dev *sdev, void *message,
	void __iomem *dest, size_t bytes)
{
	memcpy_toio(dest, message, bytes);
}

static void byt_mailbox_read(struct snd_sof_dev *sdev, void *message,
	void __iomem *src, size_t bytes)
{
	memcpy_fromio(message, src, bytes);
}

/*
 * IPC Doorbell IRQ handler and thread.
 */

static irqreturn_t byt_irq_handler(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *) context;
	u64 isr;
	int ret = IRQ_NONE;

	spin_lock(&sdev->spinlock);

	/* Interrupt arrived, check src */
	isr = snd_sof_dsp_read64(sdev, SHIM_ISRX);
	if (isr & SHIM_ISRX_DONE) {

		/* Mask Done interrupt before return */
		snd_sof_dsp_update_bits64_unlocked(sdev, SHIM_IMRX,
			SHIM_IMRX_DONE, SHIM_IMRX_DONE);
		ret = IRQ_WAKE_THREAD;
	}

	if (isr & SHIM_ISRX_BUSY) {

		/* Mask Busy interrupt before return */
		snd_sof_dsp_update_bits64_unlocked(sdev, SHIM_IMRX,
			SHIM_IMRX_BUSY, SHIM_IMRX_BUSY);
		ret = IRQ_WAKE_THREAD;
	}

	spin_unlock(&sdev->spinlock);
	return ret;
}

static irqreturn_t byt_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *) context;
	u64 ipcx, ipcd;
	unsigned long flags;

	spin_lock_irqsave(&sdev->spinlock, flags);

	ipcx = snd_sof_dsp_read64(sdev, SHIM_IPCX);
	ipcd = snd_sof_dsp_read64(sdev, SHIM_IPCD);

	/* reply message from DSP */
	if (ipcx & SHIM_BYT_IPCX_DONE) {

		/* Handle Immediate reply from DSP Core */
		snd_sof_ipc_process_reply(sdev, ipcx);

		/* clear DONE bit - tell DSP we have completed */
		snd_sof_dsp_update_bits64_unlocked(sdev, SHIM_IPCX,
			SHIM_BYT_IPCX_DONE, 0);

		/* unmask Done interrupt */
		snd_sof_dsp_update_bits64_unlocked(sdev, SHIM_IMRX,
			SHIM_IMRX_DONE, 0);
	}

	/* new message from DSP */
	if (ipcd & SHIM_BYT_IPCD_BUSY) {

		/* Handle Notification and Delayed reply from DSP Core */
		snd_sof_ipc_process_notification(sdev, ipcd);

		/* clear BUSY bit and set DONE bit - accept new messages */
		snd_sof_dsp_update_bits64_unlocked(sdev, SHIM_IPCD,
			SHIM_BYT_IPCD_BUSY | SHIM_BYT_IPCD_DONE,
			SHIM_BYT_IPCD_DONE);

		/* unmask busy interrupt */
		snd_sof_dsp_update_bits64_unlocked(sdev, SHIM_IMRX,
			SHIM_IMRX_BUSY, 0);
	}

	spin_unlock_irqrestore(&sdev->spinlock, flags);

	/* continue to send any remaining messages... */
	snd_sof_ipc_process_msgs(sdev);

	return IRQ_HANDLED;
}

#if 0
static void byt_notify(struct snd_sof_dev *dsp)
{
	snd_sof_dsp_update_bits64(dsp, SHIM_IPCD,
		SHIM_BYT_IPCD_BUSY | SHIM_BYT_IPCD_DONE,
		SHIM_BYT_IPCD_DONE);
}
#endif

static bool byt_is_dsp_busy(struct snd_sof_dev *sdev)
{
	u64 ipcx;

	ipcx = snd_sof_dsp_read64(sdev, SHIM_IPCX);
	return (ipcx & (SHIM_BYT_IPCX_BUSY | SHIM_BYT_IPCX_DONE));
}


static int byt_tx_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{
	u64 cmd = msg->header;

	/* send the message */
	byt_mailbox_write(sdev, sdev->outbox.base, msg->data, msg->size);
	snd_sof_dsp_write64(sdev, SHIM_IPCX, cmd);

	return 0;
}

/*
 * DSP control.
 */

static int byt_run(struct snd_sof_dev *sdev)
{
	int tries = 10;

	/* release stall and wait to unstall */
	snd_sof_dsp_update_bits64(sdev, SHIM_CSR, SHIM_BYT_CSR_STALL, 0x0);
	while (tries--) {
		if (!(snd_sof_dsp_read64(sdev, SHIM_CSR) &
		      SHIM_BYT_CSR_PWAITMODE))
			break;
		msleep(100);
	}
	if (tries < 0) {
		dev_err(sdev->dev, "unable to run DSP firmware\n");
		byt_dump(sdev, SOF_DBG_REGS | SOF_DBG_MBOX);
		return -ENODEV;
	}

	return 0;
}

static int byt_reset(struct snd_sof_dev *sdev)
{
	/* put DSP into reset, set reset vector and stall */
	snd_sof_dsp_update_bits64(sdev, SHIM_CSR,
		SHIM_BYT_CSR_RST | SHIM_BYT_CSR_VECTOR_SEL | SHIM_BYT_CSR_STALL,
		SHIM_BYT_CSR_RST | SHIM_BYT_CSR_VECTOR_SEL | SHIM_BYT_CSR_STALL);

	udelay(10);

	/* take DSP out of reset and keep stalled for FW loading */
	snd_sof_dsp_update_bits64(sdev, SHIM_CSR, SHIM_BYT_CSR_RST, 0);

	return 0;
}

/* baytrail ops */
struct snd_sof_dsp_ops byt_dsp_ops = {

	/* DSP core boot / reset */
	.run		= byt_run,
	.reset		= byt_reset,

	/* Register IO */
	.write		= byt_write,
	.read		= byt_read,
	.write64	= byt_write64,
	.read64		= byt_read64,

	/* Block IO */
	.block_read	= byt_block_read,
	.block_write	= byt_block_write,

	/* doorbell */
	.irq_handler	= byt_irq_handler,
	.irq_thread	= byt_irq_thread,

	/* mailbox */
	.mailbox_read	= byt_mailbox_read,
	.mailbox_write	= byt_mailbox_write,

	/* ipc */
	.tx_msg		= byt_tx_msg,
	//int (*rx_msg)(struct snd_sof_dev *sof_dev, struct sof_ipc_msg *msg);

	/* debug */
	.debug_map	= byt_debugfs,
	.debug_map_count	= ARRAY_SIZE(byt_debugfs),
	.dbg_dump	= byt_dump,

};
EXPORT_SYMBOL(byt_dsp_ops);

/* cherrytrail and braswell ops */
struct snd_sof_dsp_ops cht_dsp_ops = {

	/* DSP core boot / reset */
	.run		= byt_run,
	.reset		= byt_reset,

	/* Register IO */
	.write		= byt_write,
	.read		= byt_read,
	.write64	= byt_write64,
	.read64		= byt_read64,

	/* Block IO */
	.block_read	= byt_block_read,
	.block_write	= byt_block_write,

	/* doorbell */
	.irq_handler	= byt_irq_handler,
	.irq_thread	= byt_irq_thread,

	/* mailbox */
	.mailbox_read	= byt_mailbox_read,
	.mailbox_write	= byt_mailbox_write,

	/* ipc */
	.tx_msg		= byt_tx_msg,
	//int (*rx_msg)(struct snd_sof_dev *sof_dev, struct sof_ipc_msg *msg);

	/* debug */
	.debug_map	= byt_debugfs,
	.debug_map_count	= ARRAY_SIZE(byt_debugfs),
	.dbg_dump	= byt_dump,

};
EXPORT_SYMBOL(cht_dsp_ops);
