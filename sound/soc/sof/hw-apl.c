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
#include <linux/pci.h>
#include <sound/hdaudio_ext.h>
#include <sound/sof.h>

#include "sof-priv.h"
#include "ops.h"
#include "intel.h"

#define APL_HDA_BAR			0
#define APL_PP_BAR			1
#define APL_SPIB_BAR			2
#define APL_DRSM_BAR			3
#define APL_DSP_BAR			4

static bool is_apl_core_enable(struct snd_sof_dev *sdev,
	unsigned int core_mask);

/*
 * Debug
 */

static const struct snd_sof_debugfs_map apl_debugfs[] = {
	{"hda", APL_HDA_BAR, 0, 0x4000},
	{"pp", APL_PP_BAR,  0, 0x1000},
	{"dsp", APL_DSP_BAR,  0, 0x10000},
//	{"pci", APL_PCI_BAR, 0, 0x150},
};

static void apl_dump(struct snd_sof_dev *sdev, u32 flags)
{
	u32 reg;
	int i;

	if (flags & SOF_DBG_REGS) {
		for (i = 0; i < 0x40; i += 4 ) {
			dev_dbg(sdev->dev, "hda 0x%2.2x value 0x%8.8x\n",
				i, snd_sof_dsp_read(sdev, APL_HDA_BAR, i));
		}
		for (i = 0; i < 0x40; i += 4 ) {
			dev_dbg(sdev->dev, "dsp 0x%2.2x value 0x%8.8x\n",
				i, snd_sof_dsp_read(sdev, APL_DSP_BAR, i));
		}
		for (i = 0; i < 0x40; i += 4 ) {
			dev_dbg(sdev->dev, "pp 0x%2.2x value 0x%8.8x\n",
				i, snd_sof_dsp_read(sdev, APL_PP_BAR, i));
		}
	}

#if 0
	if (flags & SOF_DBG_MBOX) {
		for (i = MBOX_OFFSET; i < MBOX_OFFSET + MBOX_DUMP_SIZE; i += 4) 
		{
			dev_dbg(sdev->dev, "mbox: 0x%2.2x value 0x%8.8x\n",
				i - MBOX_OFFSET,
				readl(sdev->bar[BYT_DSP_BAR] + i));
		}
	}
#endif

	if (flags & SOF_DBG_PCI) {
		for (i = 0; i < 0x150; i += 4) {
			pci_read_config_dword(sdev->pci, i, &reg);
			dev_dbg(sdev->dev, "pci: 0x%2.2x value 0x%8.8x\n",
				i, reg);
		}
	}
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
	u32 tmp = 0;
	int i, m, n;
	const u8 *src_byte = src;

	m = size / 4;
	n = size % 4;

	/* __iowrite32_copy use 32bit size values so divide by 4 */
	__iowrite32_copy((void *)dest, src, m);

	if (n) {
		for (i = 0; i < n; i++)
			tmp |= (u32)*(src_byte + m * 4 + i) << (i * 8);
		__iowrite32_copy((void *)(dest + m * 4), &tmp, 1);
	}
}

static void apl_block_read(struct snd_sof_dev *sdev, void *dest,
	const volatile void __iomem *src, size_t size)
{
	memcpy_fromio(dest, src, size);
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
 * Interrupts
 */

static void apl_ipc_int_enable(struct snd_sof_dev *sdev)
{
	snd_sof_dsp_update_bits(sdev, APL_DSP_BAR, SKL_ADSP_REG_ADSPIC,
		SKL_ADSPIC_IPC, SKL_ADSPIC_IPC);
}

static void apl_ipc_int_disable(struct snd_sof_dev *sdev)
{
	snd_sof_dsp_update_bits_unlocked(sdev, APL_DSP_BAR,
		SKL_ADSP_REG_ADSPIC, SKL_ADSPIC_IPC, 0);
}

static void apl_ipc_op_int_enable(struct snd_sof_dev *sdev)
{
	/* enable IPC DONE interrupt */
	snd_sof_dsp_update_bits(sdev, APL_DSP_BAR, SKL_ADSP_REG_HIPCCTL,
		SKL_ADSP_REG_HIPCCTL_DONE, SKL_ADSP_REG_HIPCCTL_DONE);

	/* Enable IPC BUSY interrupt */
	snd_sof_dsp_update_bits(sdev, APL_DSP_BAR, SKL_ADSP_REG_HIPCCTL,
		SKL_ADSP_REG_HIPCCTL_BUSY, SKL_ADSP_REG_HIPCCTL_BUSY);
}

static void apl_ipc_op_int_disable(struct snd_sof_dev *sdev)
{
	/* disable IPC DONE interrupt */
	snd_sof_dsp_update_bits_unlocked(sdev, APL_DSP_BAR,
		SKL_ADSP_REG_HIPCCTL, SKL_ADSP_REG_HIPCCTL_DONE, 0);

	/* Disable IPC BUSY interrupt */
	snd_sof_dsp_update_bits_unlocked(sdev, APL_DSP_BAR,
		SKL_ADSP_REG_HIPCCTL, SKL_ADSP_REG_HIPCCTL_BUSY, 0);
}

/*
 * Code loader
 */

static int apl_setup_spib(struct snd_sof_dev *sdev, 
				struct snd_sof_hda_stream *stream, int enable, u32 value)
{
	u32 mask = 0;
	int stream_tag = stream->stream_tag;

	/* enable/disable SPIB for this hdac stream */
	if(!sdev->bar[APL_SPIB_BAR]) {
		dev_err(sdev->dev, "Address of SPB capability is NULL\n");
		return -1; // TODO: Fix return value
	}
	
	mask |= (1 << (stream_tag -1));
	
	/* Enable/Disable SPIB for the stream */
	snd_sof_dsp_update_bits(sdev, APL_SPIB_BAR,
				HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL, 
				mask,
				enable << (stream_tag -1));
				
	/* set the spib value */			
	snd_sof_dsp_write(sdev, APL_SPIB_BAR, HDA_ADSP_REG_CL_SPBFIFO_SPBFCCTL, value);
	
	return 0;
}

static int apl_dsp_cleanup(struct snd_sof_dev *sdev,
		struct snd_dma_buffer *dmab, struct snd_sof_hda_stream *stream)
{
	
	apl_setup_spib(sdev, stream, 0, 0);
	/* TODO: spin lock ?*/
	stream->open = 0;
	stream->running = 0;
	stream->substream = NULL;
	
	/*Reset BDL address*/
	snd_sof_dsp_write(sdev, APL_HDA_BAR, 
				stream->sd_offset + HDA_ADSP_REG_CL_SD_BDLPL,
				0);
	snd_sof_dsp_write(sdev, APL_HDA_BAR, 
				stream->sd_offset + HDA_ADSP_REG_CL_SD_BDLPU,
				0);
	snd_sof_dsp_write(sdev, APL_HDA_BAR, 
				stream->sd_offset + HDA_ADSP_REG_CL_SD_CTL,
				0);
	snd_dma_free_pages(dmab);
	dmab->area = NULL;
	stream->bufsize = 0;
	stream->format_val = 0;
	
	return 0;
}
 
static int apl_trigger(struct snd_sof_dev *sdev, 
			struct snd_sof_hda_stream *stream, int start)
{
	if(start) {
		snd_sof_dsp_write(sdev, APL_HDA_BAR, HDA_INTCTL,
					1 << (stream->stream_tag - 1));
		snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, stream->sd_offset,
					 HDA_SD_CTL_DMA_START |\
					 HDA_CL_DMA_SD_INT_MASK, 
					 HDA_SD_CTL_DMA_START |\
					 HDA_CL_DMA_SD_INT_MASK);
		stream->running = true;
		dev_dbg(sdev->dev, "started stream\n");
	}				
	else {
		snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, stream->sd_offset,
					 HDA_SD_CTL_DMA_START |\
					 HDA_CL_DMA_SD_INT_MASK, 
					 0x0);
		snd_sof_dsp_write(sdev, APL_HDA_BAR, 
					stream->sd_offset + 
					HDA_ADSP_REG_CL_SD_STS,
					HDA_CL_DMA_SD_INT_MASK); /* to be sure */
		stream->running = false; 

		snd_sof_dsp_write(sdev, APL_HDA_BAR, HDA_INTCTL,
					0x0);
	}

	return 0;
}

static int apl_transfer_fw(struct snd_sof_dev *sdev, int stream_tag)
{
	int ret,i;
	struct snd_sof_hda_stream *stream = NULL;
	struct snd_sof_hda_dev *hdev = &sdev->hda;
	
	/* Get stream with stream_tag */
	stream = &hdev->pstream[stream_tag - 1];
	if (!stream)
		return -ENODEV;

	apl_trigger(sdev, stream, true);
	ret = snd_sof_dsp_register_poll(sdev, APL_DSP_BAR, BXT_ADSP_FW_STATUS, 			SKL_FW_STS_MASK, BXT_ROM_INIT, BXT_BASEFW_TIMEOUT) ;

	apl_trigger(sdev, stream, false);
	apl_dsp_cleanup(sdev, &sdev->dmab, stream);

	return ret;
}

/* Code loader helper APIs */
/*
 * set up a BDL entry
 */
static int setup_bdle(struct snd_sof_dev *sdev, struct snd_dma_buffer *dmab,
		      struct snd_sof_hda_stream *stream, __le32 **bdlp,
		      int ofs, int size, int with_ioc)
{
	__le32 *bdl = *bdlp;

	while (size > 0) {
		dma_addr_t addr;
		int chunk;

		if (stream->frags >= MAX_BDL_ENTRIES)
			return -EINVAL;

		addr = snd_sgbuf_get_addr(dmab, ofs);

		/* program the address field of the BDL entry */
		bdl[0] = cpu_to_le32(lower_32_bits(addr)); 
		bdl[1] = cpu_to_le32(upper_32_bits(addr));

		/* program the size field of the BDL entry */
		chunk = snd_sgbuf_get_chunk_size(dmab, ofs, size);
#if 0
		/* one BDLE cannot cross 4K boundary on CTHDA chips */
		if (bus->align_bdle_4k) {
			u32 remain = 0x1000 - (ofs & 0xfff);

			if (chunk > remain)
				chunk = remain;
		}	
#endif
		bdl[2] = cpu_to_le32(chunk);

		/* program the IOC to enable interrupt
		 * only when the whole fragment is processed
		 */
		size -= chunk;
		bdl[3] = (size || !with_ioc) ? 0 : cpu_to_le32(0x01);
		bdl += 4;
		stream->frags++;
		ofs += chunk;
	}
	*bdlp = bdl;
	return ofs;
}

static int apl_prepare(struct snd_sof_dev *sdev, unsigned int format,
			unsigned int size, struct snd_dma_buffer *dmab)
{
	struct snd_sof_hda_stream *stream = NULL;
	struct snd_sof_hda_dev *hdev = &sdev->hda;
	int ret, timeout = 300, i;
	u32 val;
	u32 *bdl;
	struct pci_dev *pci;
	
	/* Get an unused stream */

	for(i = 1; i < SOF_HDA_PLAYBACK_STREAMS; i++) {
		if(!hdev->pstream[i].open) {
			hdev->pstream[i].open = true;
			stream = &hdev->pstream[i];
			break;
		}
	}
	if (!stream)
		return -ENODEV;

	/* Allocate DMA Buffer */
	ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV_SG, NULL, size, dmab);

	if (ret < 0) {
		dev_err(sdev->dev, "Alloc buffer for base fw failed: %x\n", ret);
		return ret;
	}

	stream->format_val = format;
	stream->bufsize = size;
	
	/*Clear Stream and Prepare DSP for FW Loading */
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, stream->sd_offset,
	 			HDA_SD_CTL_DMA_START |\
	 			HDA_CL_DMA_SD_INT_MASK, 0);
	
	/*Stream Reset*/
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, stream->sd_offset, 
				0x1, 0);

	/* Wait for hardware to report that stream is out of reset */
	do {
		val = snd_sof_dsp_read(sdev, APL_HDA_BAR, stream->sd_offset);
		if(!(val & 0x1))
			break;
	}while(--timeout);
	
	if(stream->posbuf)
		stream->posbuf = 0;
	
	/* Reset BDl Address */
	snd_sof_dsp_write(sdev, APL_HDA_BAR, 
				stream->sd_offset + HDA_ADSP_REG_CL_SD_BDLPL, 
				0x0);
	snd_sof_dsp_write(sdev, APL_HDA_BAR, 
				stream->sd_offset + HDA_ADSP_REG_CL_SD_BDLPU, 
				0x0);
				
	stream->frags = 0;		
	
	bdl = (u32 *)stream->bdl.area;
		
	if(setup_bdle(sdev, dmab, stream, &bdl, 0, size, 0) < 0) {
		dev_dbg(sdev->dev, "set up bdle fail\n");
	}
	/* Set up Stream Descriptor for DMA */
		
	/* Program Stream Tag */
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, stream->sd_offset,
				HDA_CL_SD_CTL_STREAM_TAG_MASK, 
				stream->stream_tag <<
				HDA_CL_SD_CTL_STREAM_TAG_SHIFT);
	
	/* Program Cyclic Buffer Length */
	snd_sof_dsp_write(sdev, APL_HDA_BAR, 
				stream->sd_offset + HDA_ADSP_REG_CL_SD_CBL, 
				stream->bufsize);
	
	/* Program Stream Format */ 
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, 
				stream->sd_offset + HDA_ADSP_REG_CL_SD_FORMAT, 
				0xffff0000, stream->format_val << 16);
				
	/* Program Last Valid Index */
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, 
				stream->sd_offset + HDA_ADSP_REG_CL_SD_LVI, 
				0xffff0000, (stream->frags - 1) << 16);
				
	/* Program BDL Address */
	snd_sof_dsp_write(sdev, APL_HDA_BAR, 
				stream->sd_offset + HDA_ADSP_REG_CL_SD_BDLPL, 
				(u32)stream->bdl.addr);
	snd_sof_dsp_write(sdev, APL_HDA_BAR, 
				stream->sd_offset + HDA_ADSP_REG_CL_SD_BDLPU,
				upper_32_bits(stream->bdl.addr));
				
	/* Enable Position Buffer */
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, HDA_ADSP_DPLBASE, 
				HDA_ADSP_DPLBASE_ENABLE, 
				0x1);
				
	/* Set Interrupt Enable bits */
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, stream->sd_offset, 
				HDA_CL_DMA_SD_INT_DESC_ERR |
				HDA_CL_DMA_SD_INT_FIFO_ERR |
				HDA_CL_DMA_SD_INT_COMPLETE,
				HDA_CL_DMA_SD_INT_DESC_ERR |
				HDA_CL_DMA_SD_INT_FIFO_ERR |
				HDA_CL_DMA_SD_INT_COMPLETE);

				
	/* Read FIFOSIZE */
	if(stream->direction == SNDRV_PCM_STREAM_PLAYBACK)
		stream->fifo_size = snd_sof_dsp_read(sdev, APL_HDA_BAR, 
					stream->sd_offset + 
					HDA_ADSP_REG_CL_SD_FIFOSIZE) 
					>> 16;
	else
		stream->fifo_size = 0;

	apl_setup_spib(sdev, stream, 1, size);

	return stream->stream_tag;
}

void apl_cldma_do_irq(struct snd_sof_dev *sdev)
{
	u32 status;

	status = snd_sof_dsp_read(sdev, APL_DSP_BAR, HDA_ADSP_REG_CL_SD_STS);

//	if (status & SKL_CL_DMA_SD_INT_COMPLETE)
//		ctx->cl_dev.wake_status = SKL_CL_DMA_BUF_COMPLETE;
//	else
//		ctx->cl_dev.wake_status = SKL_CL_DMA_ERR;

	//ctx->cl_dev.wait_condition = true;
	//wake_up(&ctx->cl_dev.wait_queue);
}


/*
 * IPC Doorbell IRQ handler and thread.
 */

static irqreturn_t apl_irq_handler(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *) context;
	int ret = IRQ_NONE;

	spin_lock(&sdev->spinlock);

	/* store status */
	sdev->irq_status = snd_sof_dsp_read(sdev, APL_DSP_BAR,
		SKL_ADSP_REG_ADSPIS);

	/* invalid message ? */
	if (sdev->irq_status == 0xffffffff)
		goto out;

	/* IPC message ? */
	if (sdev->irq_status & SKL_ADSPIS_IPC) {
		apl_ipc_int_disable(sdev);
		ret = IRQ_WAKE_THREAD;
	}

	/* code loader ? */
	if (sdev->irq_status & SKL_ADSPIS_CL_DMA) {
		//apl_cldma_int_disable(sdev);
		ret = IRQ_WAKE_THREAD;
	}

out:
	spin_unlock(&sdev->spinlock);
	return ret;
}

static irqreturn_t apl_irq_thread(int irq, void *context)
{
	struct snd_sof_dev *sdev = (struct snd_sof_dev *) context;
	u64 header = 0;
	u32 hipcie, hipct, hipcte;
	irqreturn_t ret = IRQ_NONE;

	/* code loader ? */
	if (sdev->irq_status & SKL_ADSPIS_CL_DMA)
		apl_cldma_do_irq(sdev);

	/* Here we handle IPC interrupts only */
	if (!(sdev->irq_status & SKL_ADSPIS_IPC))
		return ret;

	hipcie = snd_sof_dsp_read(sdev, APL_DSP_BAR, SKL_ADSP_REG_HIPCIE);
	hipct = snd_sof_dsp_read(sdev, APL_DSP_BAR, SKL_ADSP_REG_HIPCT);

	/* reply message from DSP */
	if (hipcie & SKL_ADSP_REG_HIPCIE_DONE) {

		
		snd_sof_dsp_update_bits(sdev, APL_DSP_BAR,
			SKL_ADSP_REG_HIPCCTL, SKL_ADSP_REG_HIPCCTL_DONE, 0);

		/* clear DONE bit - tell DSP we have completed the operation */
		snd_sof_dsp_update_bits(sdev, APL_DSP_BAR, SKL_ADSP_REG_HIPCIE,
			SKL_ADSP_REG_HIPCIE_DONE, SKL_ADSP_REG_HIPCIE_DONE);

		/* unmask Done interrupt */
		snd_sof_dsp_update_bits(sdev, APL_DSP_BAR,
			SKL_ADSP_REG_HIPCCTL, SKL_ADSP_REG_HIPCCTL_DONE,
			SKL_ADSP_REG_HIPCCTL_DONE);

		ret = IRQ_HANDLED;
	}

	/* New message from DSP */
	if (hipct & SKL_ADSP_REG_HIPCT_BUSY) {

		hipcte = snd_sof_dsp_read(sdev, APL_DSP_BAR,
			SKL_ADSP_REG_HIPCTE);
		header = hipct;
		header <<= 32;
		header |= hipcte;

		dev_dbg(sdev->dev, "ipc: firmware response :%llx\n", header);

		if (header) {
			/* Handle Immediate reply from DSP Core */
			snd_sof_ipc_process_reply(sdev, header);
		} else {
			dev_dbg(sdev->dev, "ipc: firmware notification\n");
			snd_sof_ipc_process_notification(sdev, header);
		}

		/* clear  busy interrupt */
		snd_sof_dsp_update_bits(sdev, APL_DSP_BAR, SKL_ADSP_REG_HIPCT,
			SKL_ADSP_REG_HIPCT_BUSY, SKL_ADSP_REG_HIPCT_BUSY);

		ret = IRQ_HANDLED;
	}

	if (ret == IRQ_HANDLED) {
		apl_ipc_int_enable(sdev);
		/* continue to send any remaining messages... */
		snd_sof_ipc_process_msgs(sdev);
	}

	return ret;
}

/*
 * DSP control.
 */

static int
apl_dsp_core_reset_enter(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	u32 adspcs;
	int ret;

	/* set reset bits for cores */
	snd_sof_dsp_update_bits_unlocked(sdev, APL_DSP_BAR,
			SKL_ADSP_REG_ADSPCS, SKL_ADSPCS_CRST_MASK(core_mask),
			SKL_ADSPCS_CRST_MASK(core_mask));

	/* poll with timeout to check if operation successful */
	ret = snd_sof_dsp_register_poll(sdev, APL_DSP_BAR, SKL_ADSP_REG_ADSPCS,
		SKL_ADSPCS_CRST_MASK(core_mask),
		SKL_ADSPCS_CRST_MASK(core_mask), SKL_DSP_RESET_TO);

	adspcs = snd_sof_dsp_read(sdev, APL_DSP_BAR, SKL_ADSP_REG_ADSPCS);
	if ((adspcs & SKL_ADSPCS_CRST_MASK(core_mask)) !=
		SKL_ADSPCS_CRST_MASK(core_mask)) {
		dev_err(sdev->dev, "reset enter failed: core_mask %x adspcs 0x%x\n", 				core_mask, adspcs);
		ret = -EIO;
	}

	return ret;
}

static int apl_dsp_core_reset_leave(struct snd_sof_dev *sdev,
	unsigned int core_mask)
{
	u32 adspcs;
	int ret;

	/* clear reset bits for cores */
	snd_sof_dsp_update_bits_unlocked(sdev, APL_DSP_BAR,
		SKL_ADSP_REG_ADSPCS, SKL_ADSPCS_CRST_MASK(core_mask), 0);

	/* poll with timeout to check if operation successful */
	ret = snd_sof_dsp_register_poll(sdev, APL_DSP_BAR, SKL_ADSP_REG_ADSPCS,
		SKL_ADSPCS_CRST_MASK(core_mask), 0, SKL_DSP_RESET_TO);

	adspcs = snd_sof_dsp_read(sdev, APL_DSP_BAR, SKL_ADSP_REG_ADSPCS);
	if ((adspcs & SKL_ADSPCS_CRST_MASK(core_mask)) != 0) {
		dev_err(sdev->dev, "reset leave failed: core_mask %x adspcs 0x%x\n", core_mask, adspcs);
		ret = -EIO;
	}

	return ret;
}



static int apl_reset_core(struct snd_sof_dev *sdev, unsigned int core_mask)
{
		/* stall core */
	snd_sof_dsp_update_bits_unlocked(sdev, APL_HDA_BAR,
		SKL_ADSP_REG_ADSPCS, SKL_ADSPCS_CSTALL_MASK(core_mask),
		SKL_ADSPCS_CSTALL_MASK(core_mask));

	/* set reset state */
	return apl_dsp_core_reset_enter(sdev, core_mask);
}

static int apl_run_core(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	int ret;

	/* leave reset state */
	ret = apl_dsp_core_reset_leave(sdev, core_mask);
	if (ret < 0)
		return ret;

	/* run core */
	dev_dbg(sdev->dev, "unstall/run core: core_mask = %x\n", core_mask);
	snd_sof_dsp_update_bits_unlocked(sdev, APL_DSP_BAR,
		SKL_ADSP_REG_ADSPCS, SKL_ADSPCS_CSTALL_MASK(core_mask), 0);

	if (!is_apl_core_enable(sdev, core_mask)) {
		apl_reset_core(sdev, core_mask);
		dev_err(sdev->dev, "DSP start core failed: core_mask %x\n",
			core_mask);
		ret = -EIO;
	}

	return ret;
}

/*
 * Power Management.
 */

static int apl_core_power_up(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	u32 adspcs;
	int ret;

	/* update bits */
	snd_sof_dsp_update_bits(sdev, APL_DSP_BAR, SKL_ADSP_REG_ADSPCS,
			SKL_ADSPCS_SPA_MASK(core_mask),
			SKL_ADSPCS_SPA_MASK(core_mask));

	/* poll with timeout to check if operation successful */
	ret = snd_sof_dsp_register_poll(sdev, APL_DSP_BAR, SKL_ADSP_REG_ADSPCS,
		SKL_ADSPCS_CPA_MASK(core_mask), SKL_ADSPCS_CPA_MASK(core_mask),
		SKL_DSP_PU_TO);
	if (ret < 0)
		dev_err(sdev->dev, "error: timout on core powerup\n");

	/* did core power up ? */
	adspcs = snd_sof_dsp_read(sdev, APL_DSP_BAR, SKL_ADSP_REG_ADSPCS);
	if ((adspcs & SKL_ADSPCS_CPA_MASK(core_mask)) !=
		SKL_ADSPCS_CPA_MASK(core_mask)) {
		dev_err(sdev->dev, "error: power up core failed core_mask %xadspcs 0x%x	\n", core_mask, adspcs);
		ret = -EIO;
	}

	return ret;
}

static int apl_core_power_down(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	/* update bits */
	snd_sof_dsp_update_bits_unlocked(sdev, APL_DSP_BAR,
		SKL_ADSP_REG_ADSPCS, SKL_ADSPCS_SPA_MASK(core_mask), 0);

	/* poll with timeout to check if operation successful */
	return snd_sof_dsp_register_poll(sdev, APL_DSP_BAR,
		SKL_ADSP_REG_ADSPCS, SKL_ADSPCS_CPA_MASK(core_mask), 0,
		SKL_DSP_PD_TO);
}

static int apl_set_dsp_D0(struct snd_sof_dev *sdev, unsigned int core_id)
{
	unsigned int core_mask = SKL_DSP_CORE_MASK(core_id);
	int ret = 0;

	/* core 1 also has to be powered on if core 0 is selected */
	if (core_id == 0)
		core_mask |= SKL_DSP_CORE_MASK(1);

	/* power up the cores */
	ret = apl_core_power_up(sdev, core_mask);
	if (ret < 0)
		goto err;

	if (core_id == 0) {
		/* Enable interrupts after SPA is set and before unstall */
		apl_ipc_int_enable(sdev);
		apl_ipc_op_int_enable(sdev);

	}
err:
	return ret;
}

static bool is_apl_core_enable(struct snd_sof_dev *sdev,
	unsigned int core_mask)
{
	int val;
	bool is_enable;

	val = snd_sof_dsp_read(sdev, APL_DSP_BAR, SKL_ADSP_REG_ADSPCS);

	is_enable = ((val & SKL_ADSPCS_CPA_MASK(core_mask)) &&
			(val & SKL_ADSPCS_SPA_MASK(core_mask)) &&
			!(val & SKL_ADSPCS_CRST_MASK(core_mask)) &&
			!(val & SKL_ADSPCS_CSTALL_MASK(core_mask)));

	dev_dbg(sdev->dev, "DSP core(s) enabled? %d : core_mask %x\n",
		is_enable, core_mask);

	return is_enable;
}

static int apl_enable_core(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	int ret;

	/* power up */
	ret = apl_core_power_up(sdev, core_mask);
	if (ret < 0) {
		dev_err(sdev->dev, "dsp core power up failed: core_mask %x\n",
			core_mask);
		return ret;
	}

	return apl_run_core(sdev, core_mask);
}

static int apl_disable_core(struct snd_sof_dev *sdev, unsigned int core_mask)
{
	int ret;

	/* place core in reset prior to power doown */
	ret = apl_reset_core(sdev, core_mask);
	if (ret < 0) {
		dev_err(sdev->dev, "dsp core reset failed: core_mask %x\n",
			core_mask);
		return ret;
	}

	/* power down core*/
	ret = apl_core_power_down(sdev, core_mask);
	if (ret < 0) {
		dev_err(sdev->dev, "dsp core power down fail mask %x: %d\n",
							core_mask, ret);
		return ret;
	}

	/* make sure we are in OFF state */
	if (is_apl_core_enable(sdev, core_mask)) {
		dev_err(sdev->dev, "dsp core disable fail mask %x: %d\n",
							core_mask, ret);
		ret = -EIO;
	}

	return ret;
}

static int apl_set_dsp_D3(struct snd_sof_dev *sdev, unsigned int core_id)
{
	int ret;
	unsigned int core_mask = SKL_DSP_CORE_MASK(core_id);

	ret = apl_disable_core(sdev, core_mask);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to enter D3 core %d\n", ret);
		return ret;
	}

	return 0;
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
 * HDA Operations.
 */

static int apl_link_reset(struct snd_sof_dev *sdev)
{
	unsigned long timeout;
	u32 gctl = 0;

	/* reset the HDA controller */
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, HDA_GCTL, HDA_GCTL_RESET, 0);

	/* wait for reset */
	timeout = jiffies + msecs_to_jiffies(APL_CTRL_RESET_TIMEOUT);
	while (time_before(jiffies, timeout)) {

		usleep_range(500, 1000);
		gctl = snd_sof_dsp_read(sdev, APL_HDA_BAR, HDA_GCTL);
		if ((gctl & HDA_GCTL_RESET) == 0)
			goto clear;
	}

	/* reset failed */
	dev_err(sdev->dev, "error: failed to reset HDA controller gctl 0x%x\n",
		gctl);
	return -EIO;

clear:
	/* now take controller out of reset */
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, HDA_GCTL, HDA_GCTL_RESET,
		HDA_GCTL_RESET);

	/* wait for controller to be ready */
	timeout = jiffies + msecs_to_jiffies(APL_CTRL_RESET_TIMEOUT);
	while (time_before(jiffies, timeout)) {

		usleep_range(500, 1000);
		gctl = snd_sof_dsp_read(sdev, APL_HDA_BAR, HDA_GCTL);
		if ((gctl & HDA_GCTL_RESET) == 1)
			return 0;
	}

	/* reset failed */
	dev_err(sdev->dev, "error: failed to ready HDA controller gctl 0x%x\n",
		gctl);
	return -EIO;
}

static int apl_get_caps(struct snd_sof_dev *sdev)
{
	u32 cap, offset, feature;
	int ret = -ENODEV, count = 0;

	offset = snd_sof_dsp_read(sdev, APL_HDA_BAR, HDA_LLCH);

	do {
		cap = snd_sof_dsp_read(sdev, APL_HDA_BAR, offset);

		dev_dbg(sdev->dev, "checking for capabilities at offset 0x%x\n"
			,offset & HDA_CAP_NEXT_MASK);

		feature = (cap & HDA_CAP_ID_MASK) >> HDA_CAP_ID_OFF;

		switch (feature) {
		case HDA_PP_CAP_ID:
			dev_dbg(sdev->dev, "found DSP capability at 0x%x\n",
				offset);
			sdev->bar[APL_PP_BAR] = sdev->bar[APL_HDA_BAR] +
				offset;
			ret = 0;
			break;
		case HDA_SPIB_CAP_ID:
			dev_dbg(sdev->dev, "found SPIB capability at 0x%x\n",
				offset);
			sdev->bar[APL_SPIB_BAR] = sdev->bar[APL_HDA_BAR] +
				offset;
			break;
		case HDA_DRSM_CAP_ID:
			dev_dbg(sdev->dev, "found DRSM capability at 0x%x\n",
				offset);
			sdev->bar[APL_DRSM_BAR] = sdev->bar[APL_HDA_BAR] +
				offset;
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
	int i, num_playback, num_capture, num_total,ret;
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
		stream->pphc_addr = sdev->bar[APL_PP_BAR] + HDA_PPHC_BASE +
				HDA_PPHC_INTERVAL * i;

		stream->pplc_addr = sdev->bar[APL_PP_BAR] + HDA_PPLC_BASE +
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
				
		stream->sd_offset = 0x20 * i + HDA_ADSP_LOADER_BASE;
		stream->sd_addr = sdev->bar[APL_HDA_BAR] + 
					stream->sd_offset;
		stream->stream_tag = i+1;
		stream->open = false;
		stream->running = false;
		stream->direction = SNDRV_PCM_STREAM_PLAYBACK;
		
		/* Alloc Memory for Stream BDL */ 
		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, NULL, BDL_SIZE, 
						&stream->bdl);
		if(ret < 0) {
			dev_err(sdev->dev, "Failed Stream BDL DMA alloc\n");
			return -ENOMEM;
		}
	}

	/* create capture streams */
	for (i = num_playback; i < num_total; i++) {
		stream = &hdev->cstream[i - num_playback];

		stream->pphc_addr = sdev->bar[APL_PP_BAR] + HDA_PPHC_BASE +
				HDA_PPHC_INTERVAL * i;

		stream->pplc_addr = sdev->bar[APL_PP_BAR] + HDA_PPLC_BASE +
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
		
		stream->sd_offset = 0x20 * i + HDA_ADSP_LOADER_BASE;
		stream->sd_addr = sdev->bar[APL_HDA_BAR] + 
					stream->sd_offset;

		stream->stream_tag = i+1;
		stream->open = false;
		stream->running = false;
		stream->direction = SNDRV_PCM_STREAM_CAPTURE;
		
		/* Alloc Memory for Stream BDL */ 
		ret = snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV, NULL, BDL_SIZE, 
					&stream->bdl);
		if(ret < 0) {
			dev_err(sdev->dev, "Failed Stream BDL DMA alloc\n");
			return -ENOMEM;
		}
	}
	return 0;
}


/*
 * Probe and remove.
 */

/*
 * First boot sequence has some extra steps. Core 0 waits for power
 * status on core 1, so power up core 1 also momentarily, keep it in
 * reset/stall and then turn it off
 */
static int apl_init(struct snd_sof_dev *sdev,
			const void *fwdata, u32 fwsize)
{
	int stream_tag, ret, i;
	u32 hipcie, status;

	// Prepare DMA for code loader use
	stream_tag = apl_prepare(sdev, 0x40, fwsize, &sdev->dmab);


	if (stream_tag <= 0) {
		dev_err(sdev->dev, "Failed to prepare DMA FW loading err: %x\n",
				stream_tag);
		return stream_tag;
	}
 
	memcpy(sdev->dmab.area, fwdata, fwsize);

	/* Step 1: Power up core 0 and core 1 */
	ret = apl_core_power_up(sdev, SKL_DSP_CORE_MASK(0) |
		SKL_DSP_CORE_MASK(1));
	if (ret < 0) {
		dev_err(sdev->dev, "error: dsp core 0/1 power up failed\n");
		goto err;
	}

	/* Step 2: Purge FW request */
	snd_sof_dsp_write(sdev, APL_DSP_BAR, SKL_ADSP_REG_HIPCI,
		SKL_ADSP_REG_HIPCI_BUSY | (BXT_IPC_PURGE_FW | 
		((stream_tag - 1) << 9)));

	/* Step 3: Unset core 0 reset state & unstall/run core 0 */
	ret = apl_run_core(sdev, SKL_DSP_CORE_MASK(0));
	if (ret < 0) {
		dev_err(sdev->dev, "error: dsp core start failed %d\n", ret);
		ret = -EIO;
		goto err;
	}

	/* Step 4: Wait for IPC DONE Bit from ROM */
	for (i = BXT_INIT_TIMEOUT; i > 0; i--) {

		hipcie = snd_sof_dsp_read(sdev, APL_DSP_BAR,
			SKL_ADSP_REG_HIPCIE);

		if (hipcie & SKL_ADSP_REG_HIPCIE_DONE) {
			snd_sof_dsp_update_bits(sdev, APL_DSP_BAR,
					SKL_ADSP_REG_HIPCIE,
					SKL_ADSP_REG_HIPCIE_DONE,
					SKL_ADSP_REG_HIPCIE_DONE);
			goto step5;
		}
		mdelay(1);
	}

	dev_err(sdev->dev, "error: waiting for HIPCIE done, reg: 0x%x\n",
		hipcie);
	goto err;

step5:
	/* Step 5: power down core1 */
	ret = apl_core_power_down(sdev, SKL_DSP_CORE_MASK(1));
	if (ret < 0) {
		dev_err(sdev->dev, "error: dsp core 1 power down failed\n");
		goto err;
	}

	/* Step 6: Enable Interrupt */
	apl_ipc_int_enable(sdev);
	apl_ipc_op_int_enable(sdev);

	/* Step 7: Wait for ROM init */
	for (i = BXT_INIT_TIMEOUT; i > 0; i--) {

		status = snd_sof_dsp_read(sdev, APL_DSP_BAR, BXT_ADSP_FW_STATUS)
		;

		if ((status & SKL_FW_STS_MASK) == SKL_FW_INIT) {
			dev_info(sdev->dev, "ROM loaded, continue FW loading\n")
			;
			goto out;
		}
		mdelay(1);
	}

	dev_err(sdev->dev, "error: timeout for ROM init, HIPCIE: 0x%x status 0x%x\n", hipcie, status);
	ret = -EIO;
	
err:
	apl_dump(sdev, SOF_DBG_REGS | SOF_DBG_PCI);
	//sdev->dsp_ops.cleanup(sdev->dev, &sdev->dmab, stream_tag);
	apl_disable_core(sdev, SKL_DSP_CORE_MASK(0) | SKL_DSP_CORE_MASK(1));
	return ret;
out:
	return stream_tag;
}

/*
 * We dont need to do a full HDA codec probe as external HDA codec mode is
 * considered legacy and will not be supported under SOF. HDMI/DP HDA will
 * be supported in the DSP.
 */ 
static int apl_probe(struct snd_sof_dev *sdev)
{
	struct pci_dev *pci = sdev->pci;
	int ret = 0, stream_tag;
	struct snd_sof_pdata *plat_data;

	/* HDA base */
	sdev->bar[APL_HDA_BAR] = pci_ioremap_bar(pci, APL_HDA_BAR);
	if (sdev->bar[APL_HDA_BAR] == NULL) {
		dev_err(&pci->dev, "ioremap error\n");
		return -ENXIO;
	}

	/* DSP base */
	sdev->bar[APL_DSP_BAR] = pci_ioremap_bar(pci, APL_DSP_BAR);
	if (sdev->bar[APL_DSP_BAR] == NULL) {
		dev_err(&pci->dev, "ioremap error\n");
		ret = -ENXIO;
		goto err;
	}

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
	 * issues, so recommendation is to set CGCTL.MISCBDCGE to 0 then do 
	 * reset (init chip) and then again set CGCTL.MISCBDCGE to 1
	 */
	snd_sof_pci_update_bits(sdev, PCI_CGCTL,
		PCI_CGCTL_MISCBDCGE_MASK, 0);
	
	ret = apl_link_reset(sdev);
	if (ret < 0) {
		dev_err(&pci->dev, "error: failed to reset HDA controller\n");
		goto err;
	}

	/* clear interrupts */
	snd_sof_dsp_write(sdev, APL_HDA_BAR, HDA_INTSTS,
		HDA_INT_CTRL_EN | HDA_INT_ALL_STREAM);

	/* enable CIE and GIE interrupts */
	snd_sof_dsp_update_bits(sdev, APL_HDA_BAR, HDA_INTCTL,
		HDA_INT_CTRL_EN | HDA_INT_GLOBAL_EN,
		HDA_INT_CTRL_EN | HDA_INT_GLOBAL_EN);

	/* register our IRQ */
	sdev->ipc_irq = pci->irq;
	dev_dbg(sdev->dev, "using PCI IRQ %d\n", sdev->ipc_irq);
	ret = request_threaded_irq(sdev->ipc_irq, apl_irq_handler,
		apl_irq_thread, IRQF_SHARED, "AudioDSP", sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to register IRQ %d\n",
			sdev->ipc_irq);
		goto err;		
	}

	/* re-enable CGCTL.MISCBDCGE after rest */
	snd_sof_pci_update_bits(sdev, PCI_CGCTL,
		PCI_CGCTL_MISCBDCGE_MASK, PCI_CGCTL_MISCBDCGE_MASK);

	device_disable_async_suspend(&pci->dev);

	/* get controller capabilities */
	ret = apl_get_caps(sdev);
	if (ret < 0) {
		dev_err(&pci->dev, "error: failed to find DSP capability\n");
		goto irq_err;
	}

	/* init streams */
	ret = apl_stream_init(sdev);
	if (ret < 0) {
		dev_err(&pci->dev, "error: failed to init streams\n");
		goto irq_err;
	}

	/* enable DSP features */
	snd_sof_dsp_update_bits(sdev, APL_PP_BAR, HDA_REG_PP_PPCTL,
		HDA_PPCTL_GPROCEN, HDA_PPCTL_GPROCEN);

	/* enable DSP IRQ */
	snd_sof_dsp_update_bits(sdev, APL_PP_BAR, HDA_REG_PP_PPCTL, 
		HDA_PPCTL_PIE, HDA_PPCTL_PIE);
		
	plat_data = dev_get_platdata(sdev->dev);
		
	ret = request_firmware(&plat_data->fw, 
		plat_data->machine->fw_filename, sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "Request firmware failed %d\n", ret);
		goto err;
	}
	dev_dbg(sdev->dev," Request firmware success %d \n",plat_data->fw->size);
	

	/* check for extended manifest */
	if (plat_data->fw == NULL)
		goto err;
			
	stream_tag = apl_init(sdev, plat_data->fw->data, 
		plat_data->fw->size);
				
	/* Retry Enabling core and ROM load. Retry seemed to help */
	if (stream_tag <= 0) {
		dev_dbg(sdev->dev, "retrying apl_init\n");
		stream_tag = apl_init(sdev, plat_data->fw->data,
					plat_data->fw->size);
		if (stream_tag <= 0) {
			dev_err(sdev->dev, "Error code=0x%x: FW status=0x%x\n",
			snd_sof_dsp_read(sdev, APL_DSP_BAR, BXT_ADSP_ERROR_CODE),
			snd_sof_dsp_read(sdev, APL_DSP_BAR, BXT_ADSP_FW_STATUS));

			dev_err(sdev->dev, "Core En/ROM load fail:%d\n", stream_tag);
			ret = stream_tag;
			goto irq_err;
		}
	}
	
	/* At this point DSP ROM has been initialized and should be ready for 
	 code loading and firmware boot */
	ret = apl_transfer_fw(sdev, stream_tag);
	
	if(ret < 0) {
		dev_err(sdev->dev, "Load FW failed\n");
		return ret;
	} 
	
	return 0;

irq_err:
	free_irq(sdev->ipc_irq, sdev);
err:
	/* disable DSP */
	snd_sof_dsp_update_bits(sdev, APL_PP_BAR, HDA_REG_PP_PPCTL,
		HDA_PPCTL_GPROCEN, 0);

	return ret;
}

static int apl_remove(struct snd_sof_dev *sdev)
{
	/* disable cores */
	apl_disable_core(sdev, SKL_DSP_CORE_MASK(0) | SKL_DSP_CORE_MASK(1));

	/* disable DSP IRQ */
	snd_sof_dsp_update_bits(sdev, APL_PP_BAR, HDA_REG_PP_PPCTL,
		HDA_PPCTL_PIE, 0);

	/* disable DSP */
	snd_sof_dsp_update_bits(sdev, APL_PP_BAR, HDA_REG_PP_PPCTL,
		HDA_PPCTL_GPROCEN, 0);

	free_irq(sdev->ipc_irq, sdev);

	return 0;
}

/* broxton ops */
struct snd_sof_dsp_ops snd_sof_bxt_ops = {

	/* probe and remove */
	.probe		= apl_probe,
	.remove		= apl_remove,

	/* DSP core boot / reset */
//	.run		= apl_run,
//	.reset		= apl_reset,

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
//	.run		= apl_run,
//	.reset		= apl_reset,

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

	/* firmware loading */
	


};
EXPORT_SYMBOL(snd_sof_apl_ops);

MODULE_LICENSE("Dual BSD/GPL");
