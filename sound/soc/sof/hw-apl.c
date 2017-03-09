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
 * Hardware interface for audio DSP on Apollolake.
 */

#define DEBUG

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/firmware.h>
#include <linux/device.h>
#include <linux/pci.h>

#include "sof-priv.h"
#include "ops.h"
#include "intel.h"

/* controller reset timrout in ms */
#define APL_CTRL_RESET_TIMEOUT		100

#define APL_HDA_BAR			0
#define APL_DSP_BAR			1
#define APL_SPIB_BAR			2
#define APL_DRSM_BAR			3

/* PCI registers */
#define PCI_TCSEL			0x44
#define PCI_CGCTL			0x48

/* PCI_CGCTL bits */
#define PCI_CGCTL_MISCBDCGE_MASK	(1 << 6)

/* Legacy HDA registers and bits used - widths are variable - TODO: check*/
#define HDA_GCAP			0x0
#define HDA_GCTL			0x8
#define HDA_LLCH			0x14
#define HDA_INTCTL			0x20
#define HDA_INTSTS			0x24

/* HDA_GCTL register bist */
#define HDA_GCTL_RESET			(1 << 0)	

/* HDA_INCTL and HDA_INTSTS regs */
#define HDA_INT_GLOBAL_EN		(1 << 31)
#define HDA_INT_CTRL_EN			(1 << 30)
#define HDA_INT_ALL_STREAM		0xff

/*
 * Debug
 */


static const struct snd_sof_debugfs_map apl_debugfs[] = {

};

static void apl_dump(struct snd_sof_dev *sdev, u32 flags)
{

}

/*
 * Register IO
 */

static void apl_write(struct snd_sof_dev *sdev, void __iomem *addr,
	u32 value)
{
	writel(value, addr);
}

static u32 apl_read(struct snd_sof_dev *sdev, void __iomem *addr)
{
	return readl(addr);
}

static void apl_write64(struct snd_sof_dev *sdev, void __iomem *addr,
	u64 value)
{
	memcpy_toio(addr, &value, sizeof(value));
}

static u64 apl_read64(struct snd_sof_dev *sdev, void __iomem *addr)
{
	u64 val;

	memcpy_fromio(&val, addr, sizeof(val));
	return val;
}

/*
 * Memory copy.
 */

static void apl_block_write(struct snd_sof_dev *sdev,
	volatile void __iomem *dest, const void *src, size_t size)
{
	unsigned i, trail = size % 4, count = size - trail;

	/* copy word by word */
	for (i = 0; i < count; i += 4)
		writel(*(u32 *)(src + i), dest + i);

	/* trailing bytes */
	for (; i < count + trail; i++)
		writeb(*(u8 *)(src + i), dest + i);

}

static void apl_block_read(struct snd_sof_dev *sdev, void *dest,
	const volatile void __iomem *src, size_t size)
{
	unsigned i, trail = size % 4, count = size - trail;

	/* copy word by word */
	for (i = 0; i < count; i += 4)
		*(u32 *)(dest + i) = readl(src + i);

	/* trailing bytes */
	for (; i < count + trail; i++)
		*(char *)(dest + i) = readb(src + i);
}


/*
 * IPC Mailbox IO
 */

static void apl_mailbox_write(struct snd_sof_dev *sdev, void *message,
	void __iomem *dest, size_t bytes)
{
	memcpy_toio(dest, message, bytes);
}

static void apl_mailbox_read(struct snd_sof_dev *sdev, void *message,
	void __iomem *src, size_t bytes)
{
	memcpy_fromio(message, src, bytes);
}

/*
 * IPC Doorbell IRQ handler and thread.
 */

static irqreturn_t apl_irq_handler(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *) context;
	//u64 isr;
	int ret = IRQ_NONE;

	spin_lock(&sdev->spinlock);

	

	spin_unlock(&sdev->spinlock);
	return ret;
}

static irqreturn_t apl_irq_thread(int irq, void *context)
{
	//struct snd_sof_dev *sdev = (struct snd_sof_dev *) context;


	return IRQ_HANDLED;
}

#if 0
static void apl_notify(struct snd_sof_dev *dsp)
{
	snd_sof_dsp_update_bits64(dsp, SHIM_IPCD,
		SHIM_BYT_IPCD_BUSY | SHIM_BYT_IPCD_DONE,
		SHIM_BYT_IPCD_DONE);
}
#endif

static bool apl_is_dsp_busy(struct snd_sof_dev *sdev)
{
	return 0;
}


static int apl_tx_msg(struct snd_sof_dev *sdev, struct snd_sof_ipc_msg *msg)
{

	return 0;
}

/*
 * DSP control.
 */

static int apl_run(struct snd_sof_dev *sdev)
{


	return 0;
}

static int apl_reset(struct snd_sof_dev *sdev)
{

	return 0;
}

static int apl_link_reset(struct snd_sof_dev *sdev)
{
	unsigned long timeout;

	/* reset the HDA controller */
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, HDA_GCTL, HDA_GCTL_RESET, 0);

	/* wait for reset */
	timeout = jiffies + msecs_to_jiffies(APL_CTRL_RESET_TIMEOUT);
	while (time_before(jiffies, timeout)) {
		usleep_range(500, 1000);
		if ((snd_sof_dsp_read(sdev, APL_HDA_BAR, HDA_GCTL) & HDA_GCTL_RESET) == 0)
			goto clear;
	}

	/* reset failed */	
	return -EIO;

clear:
	/* now take controller out of reset */
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, HDA_GCTL, 0, HDA_GCTL_RESET);

	/* wait for controller to be ready */
	timeout = jiffies + msecs_to_jiffies(APL_CTRL_RESET_TIMEOUT);
	while (time_before(jiffies, timeout)) {
		usleep_range(500, 1000);
		if (snd_sof_dsp_read(sdev, APL_HDA_BAR, HDA_GCTL) == 0)
			return 0;
	}

	/* reset failed */
	return -EIO;
}

#define HDA_MAX_CAPS		10
#define HDA_CAP_ID_OFF		16
#define HDA_CAP_ID_MASK		(0xFFF << HDA_CAP_ID_OFF)
#define HDA_CAP_NEXT_MASK	0xFFFF

#define HDA_PP_CAP_ID			0x3
#define HDA_REG_PP_PPCH			0x10
#define HDA_REG_PP_PPCTL		0x04
#define HDA_PPCTL_PIE			(1<<31)
#define HDA_PPCTL_GPROCEN		(1<<30)

#define HDA_SPIB_CAP_ID			0x4
#define HDA_DRSM_CAP_ID			0x5

#define HDA_SPIB_BASE			0x08
#define HDA_SPIB_INTERVAL		0x08
#define HDA_SPIB_SPIB			0x00
#define HDA_SPIB_MAXFIFO		0x04

#define HDA_PPHC_BASE			0x10
#define HDA_PPHC_INTERVAL		0x10

#define HDA_PPLC_BASE			0x10
#define HDA_PPLC_MULTI			0x10
#define HDA_PPLC_INTERVAL		0x10

#define HDA_DRSM_BASE			0x08
/* Interval used to calculate the iterating register offset */
#define HDA_DRSM_INTERVAL		0x08

static int apl_get_caps(struct snd_sof_dev *sdev)
{
	u32 cap, offset, feature;
	int ret = -ENODEV, count = 0;

	offset = snd_sof_dsp_read(sdev, APL_HDA_BAR, HDA_LLCH);

	do {
		cap = snd_sof_dsp_read(sdev, APL_HDA_BAR, offset);

		dev_vdbg(sdev->dev, "checking for capabilities at offset 0x%x\n",
			offset & HDA_CAP_NEXT_MASK);

		feature = (cap & HDA_CAP_ID_MASK) >> HDA_CAP_ID_OFF;

		switch (feature) {
		case HDA_PP_CAP_ID:
			dev_dbg(sdev->dev, "found DSP capability at 0x%x\n", offset);
			sdev->bar[APL_DSP_BAR] = sdev->bar[APL_HDA_BAR] + offset;
			ret = 0;
			break;
		case HDA_SPIB_CAP_ID:
			dev_dbg(sdev->dev, "found SPIB capability at 0x%x\n", offset);
			sdev->bar[APL_SPIB_BAR] = sdev->bar[APL_HDA_BAR] + offset;
			break;
		case HDA_DRSM_CAP_ID:
			dev_dbg(sdev->dev, "found DRSM capability at 0x%x\n", offset);
			sdev->bar[APL_DRSM_BAR] = sdev->bar[APL_HDA_BAR] + offset;
			break;
		default:
			dev_vdbg(sdev->dev, "found capability %d at 0x%x\n",
				feature, offset);
			break;
		}		

		offset = cap & HDA_CAP_NEXT_MASK;
	} while (count++ <= HDA_MAX_CAPS && offset);


	return ret;
}

static int apl_stream_init(struct snd_sof_dev *sdev)
{
	struct snd_sof_hda_dev *hdev = &sdev->hda;
	struct snd_sof_hda_stream *stream;
	int i, num_playback, num_capture, num_total;
	u32 gcap;

	gcap = snd_sof_dsp_read(sdev, APL_HDA_BAR, HDA_GCAP);
	dev_dbg(sdev->dev, "hda global caps = 0x%x\n", gcap);

	/* get stream count from GCAP */
	num_capture = (gcap >> 8) & 0x0f;
	num_playback = (gcap >> 12) & 0x0f;
	num_total = num_playback + num_capture;

	dev_dbg(sdev->dev, "detected %d playback and %d capture streams\n",
		num_playback, num_capture);

	if (num_playback >= SOF_HDA_PLAYBACK_STREAMS) {
		dev_err(sdev->dev, "error: too many playback streams %d\n",
			num_playback);
		return -EINVAL;
	}
	if (num_capture >= SOF_HDA_CAPTURE_STREAMS) {
		dev_err(sdev->dev, "error: too many capture streams %d\n",
			num_playback);
		return -EINVAL;
	}

	/* create playback streams */
	for (i = 0; i < num_playback; i++) {
		stream = &hdev->pstream[i];

		/* we always have DSP support */
		stream->pphc_addr = sdev->bar[APL_DSP_BAR] + HDA_PPHC_BASE +
				HDA_PPHC_INTERVAL * i;

		stream->pplc_addr = sdev->bar[APL_DSP_BAR] + HDA_PPLC_BASE +
				HDA_PPLC_MULTI * num_total +
				HDA_PPLC_INTERVAL * i;

		/* do we support SPIB */
		if (sdev->bar[APL_SPIB_BAR]) {
			stream->spib_addr = sdev->bar[APL_SPIB_BAR] +
				HDA_SPIB_BASE + HDA_SPIB_INTERVAL * i +
				HDA_SPIB_SPIB;

			stream->fifo_addr = sdev->bar[APL_SPIB_BAR] +
				HDA_SPIB_BASE + HDA_SPIB_INTERVAL * i +
				HDA_SPIB_MAXFIFO;
		}

		/* do we support DRSM */
		if (sdev->bar[APL_DRSM_BAR])
			stream->drsm_addr = sdev->bar[APL_DRSM_BAR] +
				HDA_DRSM_BASE + HDA_DRSM_INTERVAL * i;

	}

	/* create capture streams */
	for (i = num_playback; i < num_total; i++) {
		stream = &hdev->cstream[i - num_playback];

		stream->pphc_addr = sdev->bar[APL_DSP_BAR] + HDA_PPHC_BASE +
				HDA_PPHC_INTERVAL * i;

		stream->pplc_addr = sdev->bar[APL_DSP_BAR] + HDA_PPLC_BASE +
				HDA_PPLC_MULTI * num_total +
				HDA_PPLC_INTERVAL * i;

		/* do we support SPIB */
		if (sdev->bar[APL_SPIB_BAR]) {
			stream->spib_addr = sdev->bar[APL_SPIB_BAR] +
				HDA_SPIB_BASE + HDA_SPIB_INTERVAL * i +
				HDA_SPIB_SPIB;

			stream->fifo_addr = sdev->bar[APL_SPIB_BAR] +
				HDA_SPIB_BASE + HDA_SPIB_INTERVAL * i +
				HDA_SPIB_MAXFIFO;
		}

		/* do we support DRSM */
		if (sdev->bar[APL_DRSM_BAR])
			stream->drsm_addr = sdev->bar[APL_DRSM_BAR] +
				HDA_DRSM_BASE + HDA_DRSM_INTERVAL * i;
	}

	return 0;
}

/*
 * Probe and remove.
 */

/*
 * We dont need to do a full HDA codec probe as external HDA codec mode is
 * considered legacy and will not be supported under SOF. HDMI/DP HDA will
 * be supported in the DSP.
 */ 
static int apl_probe(struct snd_sof_dev *sdev)
{
	struct pci_dev *pci = sdev->pci;
	int ret = 0;

	/* HDA base */
	sdev->bar[APL_HDA_BAR] = pci_ioremap_bar(pci, APL_HDA_BAR);
	if (sdev->bar[APL_HDA_BAR] == NULL) {
		dev_err(&pci->dev, "ioremap error\n");
		return -ENXIO;
	}
#if 0
	/* DSP base */
	sdev->bar[APL_DSP_BAR] = pci_ioremap_bar(pci, APL_DSP_BAR);
	if (sdev->bar[APL_DSP_BAR] == NULL) {
		dev_err(&pci->dev, "ioremap error\n");
		ret = -ENXIO;
		goto remap_err;
	}
#endif

	pci_set_master(pci);
	synchronize_irq(pci->irq);

	/* allow 64bit DMA address if supported by H/W */
	if (!dma_set_mask(&pci->dev, DMA_BIT_MASK(64))) {
		dma_set_coherent_mask(&pci->dev, DMA_BIT_MASK(64));
	} else {
		dma_set_mask(&pci->dev, DMA_BIT_MASK(32));
		dma_set_coherent_mask(&pci->dev, DMA_BIT_MASK(32));
	}

	/*
	 * Clear bits 0-2 of PCI register TCSEL (at offset 0x44)
	 * TCSEL == Traffic Class Select Register, which sets PCI express QOS
	 * Ensuring these bits are 0 clears playback static on some HD Audio
	 * codecs.
	 * The PCI register TCSEL is defined in the Intel manuals.
	 */
	snd_sof_pci_update_bits(sdev, PCI_TCSEL, 0x07, 0);

	/*
	 * While performing reset, controller may not come back properly causing
	 * issues, so recommendation is to set CGCTL.MISCBDCGE to 0 then do reset
	 * (init chip) and then again set CGCTL.MISCBDCGE to 1
	 */
	snd_sof_pci_update_bits(sdev, PCI_CGCTL,
		PCI_CGCTL_MISCBDCGE_MASK, PCI_CGCTL_MISCBDCGE_MASK);
	
	ret = apl_link_reset(sdev);
	if (ret < 0) {
		dev_err(&pci->dev, "error: failed to reset HDA controller\n");
		goto reset_err;
	}

	/* clear interrupts */
	snd_sof_dsp_write(sdev, APL_HDA_BAR, HDA_INTSTS,
		HDA_INT_CTRL_EN | HDA_INT_ALL_STREAM);

	/* enable CIE and GIE interrupts */
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, HDA_INTCTL,
		0, HDA_INT_CTRL_EN | HDA_INT_GLOBAL_EN);

	/* re-enable CGCTL.MISCBDCGE after rest */
	snd_sof_pci_update_bits(sdev, PCI_CGCTL,
		PCI_CGCTL_MISCBDCGE_MASK, 0);

	device_disable_async_suspend(&pci->dev);

	/* get controller capabilities */
	ret = apl_get_caps(sdev);
	if (ret < 0) {
		dev_err(&pci->dev, "error: failed to find DSP capability\n");
		goto reset_err;
	}

	/* init streams */
	ret = apl_stream_init(sdev);
	if (ret < 0) {
		dev_err(&pci->dev, "error: failed to init streams\n");
		goto reset_err;
	}

	/* enable DSP features */
	snd_sof_dsp_update_bits(sdev, APL_DSP_BAR, HDA_REG_PP_PPCTL,
		0, HDA_PPCTL_GPROCEN);

	/* enable DSP IRQ */
	snd_sof_dsp_update_bits(sdev, APL_DSP_BAR, HDA_REG_PP_PPCTL, 0,
		HDA_PPCTL_PIE);

	// At this point DSP should be ready for code loading and firmware boot

	return 0;

reset_err:
remap_err:
	// TODO:
	return ret;
}

static int apl_remove(struct snd_sof_dev *sdev)
{
	/* disable DSP IRQ */
	snd_sof_dsp_update_bits(sdev, APL_DSP_BAR, HDA_REG_PP_PPCTL,
		HDA_PPCTL_PIE, 0);

	/* disable DSP */
	snd_sof_dsp_update_bits(sdev, APL_DSP_BAR, HDA_REG_PP_PPCTL,
		HDA_PPCTL_GPROCEN, 0);

	return 0;
}

/* broxton ops */
struct snd_sof_dsp_ops snd_sof_bxt_ops = {

	/* probe and remove */
	.probe		= apl_probe,
	.remove		= apl_remove,

	/* DSP core boot / reset */
	.run		= apl_run,
	.reset		= apl_reset,

	/* Register IO */
	.write		= apl_write,
	.read		= apl_read,
	.write64	= apl_write64,
	.read64		= apl_read64,

	/* Block IO */
	.block_read	= apl_block_read,
	.block_write	= apl_block_write,

	/* doorbell */
	.irq_handler	= apl_irq_handler,
	.irq_thread	= apl_irq_thread,

	/* mailbox */
	.mailbox_read	= apl_mailbox_read,
	.mailbox_write	= apl_mailbox_write,

	/* ipc */
	.tx_msg		= apl_tx_msg,
	//int (*rx_msg)(struct snd_sof_dev *sof_dev, struct sof_ipc_msg *msg);

	/* debug */
	.debug_map	= apl_debugfs,
	.debug_map_count	= ARRAY_SIZE(apl_debugfs),
	.dbg_dump	= apl_dump,

};
EXPORT_SYMBOL(snd_sof_bxt_ops);

/* appololake ops */
struct snd_sof_dsp_ops snd_sof_apl_ops = {

	/* probe and remove */
	.probe		= apl_probe,
	.remove		= apl_remove,

	/* DSP core boot / reset */
	.run		= apl_run,
	.reset		= apl_reset,

	/* Register IO */
	.write		= apl_write,
	.read		= apl_read,
	.write64	= apl_write64,
	.read64		= apl_read64,

	/* Block IO */
	.block_read	= apl_block_read,
	.block_write	= apl_block_write,

	/* doorbell */
	.irq_handler	= apl_irq_handler,
	.irq_thread	= apl_irq_thread,

	/* mailbox */
	.mailbox_read	= apl_mailbox_read,
	.mailbox_write	= apl_mailbox_write,

	/* ipc */
	.tx_msg		= apl_tx_msg,
	//int (*rx_msg)(struct snd_sof_dev *sof_dev, struct sof_ipc_msg *msg);

	/* debug */
	.debug_map	= apl_debugfs,
	.debug_map_count	= ARRAY_SIZE(apl_debugfs),
	.dbg_dump	= apl_dump,

};
EXPORT_SYMBOL(snd_sof_apl_ops);

MODULE_LICENSE("Dual BSD/GPL");
