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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/debugfs.h>
#include <linux/pm_runtime.h>
#include <sound/asound.h>
#include <sound/sof.h>
#include <uapi/sound/sof-ipc.h>
#include "sof-priv.h"
#include "ops.h"

#if 0
/* Global Message Types */
#define SOF_IPC_GLB_NONE			SOF_GLB_TYPE(0x0)
#define SOF_IPC_GLB_VERSION			SOF_GLB_TYPE(0x1)
#define SOF_IPC_GLB_COMPOUND			SOF_GLB_TYPE(0x2)
#define SOF_IPC_GLB_TPLG_MSG			SOF_GLB_TYPE(0x3)
#define SOF_IPC_GLB_PM_MSG			SOF_GLB_TYPE(0x4)
#define SOF_IPC_GLB_COMP_MSG			SOF_GLB_TYPE(0x5)
#define SOF_IPC_GLB_STREAM_MSG			SOF_GLB_TYPE(0x6)
#define SOF_IPC_GLB_DAI_MSG			SOF_GLB_TYPE(0x7)
#define SOF_IPC_GLB_HOST_MSG			SOF_GLB_TYPE(0x8)

/* DSP Command Message Types */
#define SOF_IPC_TPLG_COMP_NEW			SOF_CMD_TYPE(0x000)
#define SOF_IPC_TPLG_COMP_FREE			SOF_CMD_TYPE(0x001)
#define SOF_IPC_TPLG_COMP_CONNECT		SOF_CMD_TYPE(0x002)
#define SOF_IPC_TPLG_PIPE_NEW			SOF_CMD_TYPE(0x010)
#define SOF_IPC_TPLG_PIPE_FREE			SOF_CMD_TYPE(0x011)
#define SOF_IPC_TPLG_PIPE_CONNECT		SOF_CMD_TYPE(0x012)
#define SOF_IPC_TPLG_PIPE_COMPLETE		SOF_CMD_TYPE(0x013)
#define SOF_IPC_TPLG_BUFFER_NEW			SOF_CMD_TYPE(0x020)
#define SOF_IPC_TPLG_BUFFER_FREE		SOF_CMD_TYPE(0x021)
#define SOF_IPC_PM_CTX_SAVE			SOF_CMD_TYPE(0x030)
#define SOF_IPC_PM_CTX_RESTORE			SOF_CMD_TYPE(0x031)
#define SOF_IPC_PM_CTX_SIZE			SOF_CMD_TYPE(0x032)
#define SOF_IPC_PM_CLK_SET			SOF_CMD_TYPE(0x033)
#define SOF_IPC_PM_CLK_GET			SOF_CMD_TYPE(0x034)
#define SOF_IPC_PM_CLK_REQ			SOF_CMD_TYPE(0x035)
#define SOF_IPC_COMP_SET_VOLUME			SOF_CMD_TYPE(0x040)
#define SOF_IPC_COMP_GET_VOLUME			SOF_CMD_TYPE(0x041)
#define SOF_IPC_COMP_SET_MIXER			SOF_CMD_TYPE(0x042)
#define SOF_IPC_COMP_GET_MIXER			SOF_CMD_TYPE(0x043)
#define SOF_IPC_COMP_SET_MUX			SOF_CMD_TYPE(0x044)
#define SOF_IPC_COMP_GET_MUX			SOF_CMD_TYPE(0x045)
#define SOF_IPC_COMP_SET_SRC			SOF_CMD_TYPE(0x046)
#define SOF_IPC_COMP_GET_SRC			SOF_CMD_TYPE(0x047)
#define SOF_IPC_STREAM_PCM_PARAMS		SOF_CMD_TYPE(0x080)
#define SOF_IPC_STREAM_PCM_FREE			SOF_CMD_TYPE(0x081)
#define SOF_IPC_STREAM_TRIG_START		SOF_CMD_TYPE(0x082)
#define SOF_IPC_STREAM_TRIG_STOP		SOF_CMD_TYPE(0x083)
#define SOF_IPC_STREAM_TRIG_PAUSE		SOF_CMD_TYPE(0x084)
#define SOF_IPC_STREAM_TRIG_RELEASE		SOF_CMD_TYPE(0x085)
#define SOF_IPC_STREAM_TRIG_DRAIN		SOF_CMD_TYPE(0x086)
#define SOF_IPC_STREAM_TRIG_XRUN		SOF_CMD_TYPE(0x087)
#define SOF_IPC_DAI_SSP_CONFIG			SOF_CMD_TYPE(0x090)
#define SOF_IPC_DAI_HDA_CONFIG			SOF_CMD_TYPE(0x091)
#define SOF_IPC_DAI_DMIC_CONFIG			SOF_CMD_TYPE(0x092)
#define SOF_IPC_DAI_LOOPBACK			SOF_CMD_TYPE(0x093)
#define SOF_IPC_STREAM_VORBIS_PARAMS		SOF_CMD_TYPE(0x0b0)
#define SOF_IPC_STREAM_VORBIS_FREE		SOF_CMD_TYPE(0x0b1)
#endif

/* IPC message timeout (msecs) */
#define IPC_TIMEOUT_MSECS	300

#define IPC_EMPTY_LIST_SIZE	8

/* SST generic IPC data */
struct snd_sof_ipc {
	struct snd_sof_dev *sdev;

	/* message work and status */
	wait_queue_head_t wait_txq;
	struct task_struct *tx_thread;
	struct work_struct kwork;
	bool msg_pending;

	/* lists */
	struct list_head tx_list;
	struct list_head rx_list;
	struct list_head empty_list;
};

/* locks held by caller */
static struct snd_sof_ipc_msg *msg_get_empty(struct snd_sof_ipc *ipc)
{
	struct snd_sof_ipc_msg *msg = NULL;

	if (!list_empty(&ipc->empty_list)) {
		msg = list_first_entry(&ipc->empty_list, struct snd_sof_ipc_msg,
			list);
		list_del(&msg->list);
	}

	return msg;
}

static int tx_wait_done(struct snd_sof_ipc *ipc, struct snd_sof_ipc_msg *msg,
	void *reply_data)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	unsigned long flags;
	int ret;

	/* wait for DSP IPC completion */
	ret = wait_event_timeout(msg->waitq, msg->complete,
		msecs_to_jiffies(IPC_TIMEOUT_MSECS));

	spin_lock_irqsave(&sdev->spinlock, flags);
	if (ret == 0) {
		dev_err(sdev->dev, "error: ipc timed out\n");
		list_del(&msg->list);
		ret = -ETIMEDOUT;
	} else {
		/* copy the data returned from DSP */
		if (msg->reply_size)
			memcpy(reply_data, msg->reply_data, msg->reply_size);
		ret = 0;
	}

	/* return message body to empty list */
	list_add_tail(&msg->list, &ipc->empty_list);

	spin_unlock_irqrestore(&sdev->spinlock, flags);
	return ret;
}

static int ipc_tx_message(struct snd_sof_ipc *ipc, u64 header,
	void *msg_data, size_t msg_bytes, void *reply_data, 
	size_t reply_bytes, int wait)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	struct snd_sof_ipc_msg *msg;
	unsigned long flags;

	spin_lock_irqsave(&sdev->spinlock, flags);

	msg = msg_get_empty(ipc);
	if (msg == NULL) {
		spin_unlock_irqrestore(&sdev->spinlock, flags);
		return -EBUSY;
	}

	msg->header = header;
	msg->msg_size = msg_bytes;
	msg->reply_size = reply_bytes;
	msg->wait = wait;
	msg->complete = false;

	if (msg_bytes)
		memcpy(msg->msg_data, msg_data, msg_bytes);

	list_add_tail(&msg->list, &ipc->tx_list);
	schedule_work(&ipc->kwork);
	spin_unlock_irqrestore(&sdev->spinlock, flags);

	if (wait)
		return tx_wait_done(ipc, msg, reply_data);
	else
		return 0;
}

static void ipc_tx_next_msg(struct work_struct *work)
{
	struct snd_sof_ipc *ipc =
		container_of(work, struct snd_sof_ipc, kwork);
	struct snd_sof_dev *sdev = ipc->sdev;
	struct snd_sof_ipc_msg *msg;

	spin_lock_irq(&sdev->spinlock);

	if (list_empty(&ipc->tx_list))
		goto out;

	msg = list_first_entry(&ipc->tx_list, struct snd_sof_ipc_msg, list);
	list_move(&msg->list, &ipc->rx_list);

	snd_sof_dsp_tx_msg(sdev, msg);

out:
	spin_unlock_irq(&sdev->spinlock);
}


struct snd_sof_ipc_msg *sof_ipc_reply_find_msg(struct snd_sof_ipc *ipc, u32 header)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	struct snd_sof_ipc_msg *msg;

	header = SOF_IPC_MESSAGE_ID(header);

	if (list_empty(&ipc->rx_list))
		goto err;

	list_for_each_entry(msg, &ipc->rx_list, list) {
		if (SOF_IPC_MESSAGE_ID(msg->header) == header)
			return msg;
	}

err:
	dev_err(sdev->dev, "error: rx list empty but received 0x%x\n",
			header);
	return NULL;
}
EXPORT_SYMBOL(sof_ipc_reply_find_msg);

/* locks held by caller */
void sof_ipc_tx_msg_reply_complete(struct snd_sof_ipc *ipc,
	struct snd_sof_ipc_msg *msg)
{
	if (!msg->wait)
		list_add_tail(&msg->list, &ipc->empty_list);
	else {
		msg->complete = true;
		wake_up(&msg->waitq);
	}
}

void sof_ipc_drop_all(struct snd_sof_ipc *ipc)
{
	struct snd_sof_dev *sdev = ipc->sdev;
	struct snd_sof_ipc_msg *msg, *tmp;
	unsigned long flags;

	/* drop all TX and Rx messages before we stall + reset DSP */
	spin_lock_irqsave(&sdev->spinlock, flags);

	list_for_each_entry_safe(msg, tmp, &ipc->tx_list, list) {
		list_move(&msg->list, &ipc->empty_list);
		dev_err(sdev->dev, "error: dropped msg %d\n", msg->header);
	}

	list_for_each_entry_safe(msg, tmp, &ipc->rx_list, list) {
		list_move(&msg->list, &ipc->empty_list);
		dev_err(sdev->dev, "error: dropped reply %d\n", msg->header);
	}

	spin_unlock_irqrestore(&sdev->spinlock, flags);
}
EXPORT_SYMBOL(sof_ipc_drop_all);

int snd_sof_snd_sof_ipc_msg(struct snd_sof_dev *sdev,
	uint32_t cmd)
{

	return 0;
}

int snd_sof_ipc_stream_pcm_params(struct snd_sof_dev *sdev,
	struct sof_ipc_pcm_params *params)
{

	return 0;
}

void snd_sof_ipc_process_reply(struct snd_sof_dev *sdev, u32 msg_id)
{

}
EXPORT_SYMBOL(snd_sof_ipc_process_reply);

void snd_sof_ipc_process_notification(struct snd_sof_dev *sdev, u32 msg_id)
{
	/* first check for FW boot completion as it's special case */
	if (!sdev->boot_complete && msg_id & SOF_FW_READY) {
		sdev->boot_complete = true;
		dev_dbg(sdev->dev, "booting DSP firmware completed\n");
		wake_up(&sdev->boot_wait);
		return;
	}

	/* now check for regular notifications */
}
EXPORT_SYMBOL(snd_sof_ipc_process_notification);

void snd_sof_ipc_process_msgs(struct snd_sof_dev *sdev)
{
	schedule_work(&sdev->ipc->kwork);
}
EXPORT_SYMBOL(snd_sof_ipc_process_msgs);

int sof_ipc_tx_message_wait(struct snd_sof_ipc *ipc, u64 header,
	void *tx_data, size_t tx_bytes, void *rx_data, size_t rx_bytes)
{
	return ipc_tx_message(ipc, header, tx_data, tx_bytes,
		rx_data, rx_bytes, 1);
}
EXPORT_SYMBOL(sof_ipc_tx_message_wait);

int sof_ipc_tx_message_nowait(struct snd_sof_ipc *ipc, u64 header,
	void *tx_data, size_t tx_bytes)
{
	return ipc_tx_message(ipc, header, tx_data, tx_bytes, NULL, 0, 0);
}
EXPORT_SYMBOL(sof_ipc_tx_message_nowait);

struct snd_sof_ipc *snd_sof_ipc_init(struct snd_sof_dev *sdev)
{
	struct snd_sof_ipc *ipc;
	struct snd_sof_ipc_msg *msg;
	int i;

	ipc = devm_kzalloc(sdev->dev, sizeof(*ipc), GFP_KERNEL);
	if (ipc == NULL)
		return NULL; 

	INIT_LIST_HEAD(&ipc->tx_list);
	INIT_LIST_HEAD(&ipc->rx_list);
	INIT_LIST_HEAD(&ipc->empty_list);
	init_waitqueue_head(&ipc->wait_txq);
	INIT_WORK(&ipc->kwork, ipc_tx_next_msg);
	ipc->sdev = sdev;

	msg = devm_kzalloc(sdev->dev, sizeof(struct snd_sof_ipc_msg) *
		IPC_EMPTY_LIST_SIZE, GFP_KERNEL);
	if (msg == NULL)
		return NULL;

	for (i = 0; i < IPC_EMPTY_LIST_SIZE; i++) {

		msg->msg_data = devm_kzalloc(sdev->dev, PAGE_SIZE, GFP_KERNEL);
		if (msg->msg_data == NULL)
			return NULL;

		msg->reply_data = devm_kzalloc(sdev->dev, PAGE_SIZE, GFP_KERNEL);
		if (msg->reply_data == NULL)
			return NULL;

		init_waitqueue_head(&msg->waitq);
		list_add(&msg->list, &ipc->empty_list);
		msg++;
	}

	return ipc;
}
EXPORT_SYMBOL(snd_sof_ipc_init);

void snd_sof_ipc_free(struct snd_sof_dev *sdev)
{
	/* TODO: send IPC to prepare DSP for shutdown */

	cancel_work_sync(&sdev->ipc->kwork);
}
EXPORT_SYMBOL(snd_sof_ipc_free);


#if 0

#define DMA_TRACE_PAGE_NUMBER 4
static int hsw_setup_dma_trace_page_table(struct sof_hsw *hsw)
{
	struct snd_dma_buffer *dmab = &hsw->dtrace_buffer;
	struct dma_trace_buffer *hbuf = &hsw->host_buffer;
	struct dma_trace_sg *sg_elem;
	int i, pages;

	hbuf->read_offset = 0;
	hbuf->write_offset = 0;
	hbuf->rcurrent = hbuf->wcurrent = hbuf->elem_list.next;
	pages = DMA_TRACE_PAGE_NUMBER;
	hbuf->size = pages * PAGE_SIZE;

	for (i = 0; i < pages; i++) {
		u32 idx = (((i << 2) + i)) >> 1;
		u32 pfn = snd_sgbuf_get_addr(dmab, i * PAGE_SIZE) >> PAGE_SHIFT;
		u32 *pg_table;

		sg_elem = devm_kzalloc(hsw->dev, sizeof(*sg_elem), GFP_KERNEL);
		sg_elem->size = PAGE_SIZE;
		sg_elem->buf = snd_sgbuf_get_ptr(dmab, i * PAGE_SIZE);
		list_add_tail(&sg_elem->list, &hbuf->elem_list);
		pg_table = (u32 *)(hsw->trace_dma_descriptor.area + idx);

		if (i & 1)
			*pg_table |= (pfn << 4);
		else
			*pg_table |= pfn;
	}

	hbuf->rcurrent = hbuf->wcurrent = hbuf->elem_list.next;

	return 0;
}




static void hsw_fw_ready(struct sof_hsw *hsw, u32 header)
{
	struct sof_hsw_ipc_fw_ready fw_ready;
	u32 offset;
	u8 fw_info[IPC_MAX_MAILBOX_BYTES - 5 * sizeof(u32)];
	char *tmp[5], *pinfo;
	int i = 0;

	offset = (header & 0x1FFFFFFF) << 3;

	dev_dbg(hsw->dev, "ipc: DSP is ready 0x%8.8x offset %d\n",
		header, offset);

	/* copy data from the DSP FW ready offset */
	sof_dsp_read(hsw->dsp, &fw_ready, offset, sizeof(fw_ready));

	sof_dsp_mailbox_init(hsw->dsp, fw_ready.inbox_offset,
		fw_ready.inbox_size, fw_ready.outbox_offset,
		fw_ready.outbox_size);

	hsw->boot_complete = true;
	wake_up(&hsw->boot_wait);

	dev_dbg(hsw->dev, " mailbox upstream 0x%x - size 0x%x\n",
		fw_ready.inbox_offset, fw_ready.inbox_size);
	dev_dbg(hsw->dev, " mailbox downstream 0x%x - size 0x%x\n",
		fw_ready.outbox_offset, fw_ready.outbox_size);
	if (fw_ready.fw_info_size < sizeof(fw_ready.fw_info)) {
		fw_ready.fw_info[fw_ready.fw_info_size] = 0;
		dev_dbg(hsw->dev, " Firmware info: %s \n", fw_ready.fw_info);

		/* log the FW version info got from the mailbox here. */
		memcpy(fw_info, fw_ready.fw_info, fw_ready.fw_info_size);
		pinfo = &fw_info[0];
		for (i = 0; i < ARRAY_SIZE(tmp); i++)
			tmp[i] = strsep(&pinfo, " ");
		dev_info(hsw->dev, "FW loaded, mailbox readback FW info: type %s, - "
			"version: %s.%s, build %s, source commit id: %s\n",
			tmp[0], tmp[1], tmp[2], tmp[3], tmp[4]);
	}
}



static int hsw_process_reply(struct sof_hsw *hsw, u32 header)
{
	struct snd_sof_ipc_msg *msg;
	u32 reply = msg_get_global_reply(header);

	trace_ipc_reply("processing -->", header);

	msg = sof_ipc_reply_find_msg(&hsw->ipc, header);
	if (msg == NULL) {
		trace_ipc_error("error: can't find message header", header);
		return -EIO;
	}

	/* first process the header */
	switch (reply) {
	case IPC_GLB_REPLY_PENDING:
		trace_ipc_pending_reply("received", header);
		msg->pending = true;
		hsw->ipc.pending = true;
		return 1;
	case IPC_GLB_REPLY_SUCCESS:
		if (msg->pending) {
			trace_ipc_pending_reply("completed", header);
			sof_dsp_inbox_read(hsw->dsp, msg->rx_data,
				msg->rx_size);
			hsw->ipc.pending = false;
		} else {
			/* copy data from the DSP */
			sof_dsp_inbox_read(hsw->dsp, msg->rx_data,
				msg->rx_size);
		}
		break;
	/* these will be rare - but useful for debug */
	case IPC_GLB_REPLY_UNKNOWN_MESSAGE_TYPE:
		trace_ipc_error("error: unknown message type", header);
		msg->errno = -EBADMSG;
		break;
	case IPC_GLB_REPLY_OUT_OF_RESOURCES:
		trace_ipc_error("error: out of resources", header);
		msg->errno = -ENOMEM;
		break;
	case IPC_GLB_REPLY_BUSY:
		trace_ipc_error("error: reply busy", header);
		msg->errno = -EBUSY;
		break;
	case IPC_GLB_REPLY_FAILURE:
		trace_ipc_error("error: reply failure", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_STAGE_UNINITIALIZED:
		trace_ipc_error("error: stage uninitialized", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_NOT_FOUND:
		trace_ipc_error("error: reply not found", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_SOURCE_NOT_STARTED:
		trace_ipc_error("error: source not started", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_INVALID_REQUEST:
		trace_ipc_error("error: invalid request", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_ERROR_INVALID_PARAM:
		trace_ipc_error("error: invalid parameter", header);
		msg->errno = -EINVAL;
		break;
	default:
		trace_ipc_error("error: unknown reply", header);
		msg->errno = -EINVAL;
		break;
	}

	/* update any stream states */
	if (msg_get_global_type(header) == IPC_GLB_STREAM_MESSAGE)
		hsw_stream_update(hsw, msg);

	/* wake up and return the error if we have waiters on this message ? */
	list_del(&msg->list);
	sof_ipc_tx_msg_reply_complete(&hsw->ipc, msg);

	return 1;
}


static int hsw_process_notification(struct sof_hsw *hsw, u64 header)
{
	u32 type;
	int handled = 1;

	/* upper 32 bits not used atm */
	type = msg_get_global_type(header);

	trace_ipc_request("processing -->", header);

	/* FW Ready is a special case */
	if (!hsw->boot_complete && header & IPC_FW_READY) {
		hsw_fw_ready(hsw, header);
		return handled;
	}

	switch (type) {
	case IPC_GLB_GET_FW_VERSION:
	case IPC_GLB_ALLOCATE_STREAM:
	case IPC_GLB_FREE_STREAM:
	case IPC_GLB_GET_FW_CAPABILITIES:
	case IPC_GLB_REQUEST_DUMP:
	case IPC_GLB_GET_DEVICE_FORMATS:
	case IPC_GLB_SET_DEVICE_FORMATS:
	case IPC_GLB_ENTER_DX_STATE:
	case IPC_GLB_GET_MIXER_STREAM_INFO:
	case IPC_GLB_MAX_IPC_MESSAGE_TYPE:
	case IPC_GLB_RESTORE_CONTEXT:
	case IPC_GLB_SHORT_REPLY:
		dev_err(hsw->dev, "error: message type %d header 0x%16llx\n",
				type, header);
		break;
	case IPC_GLB_STREAM_MESSAGE:
		handled = hsw_stream_message(hsw, header);
		break;
	case IPC_GLB_DEBUG_LOG_MESSAGE:
		handled = hsw_log_message(hsw, header);
		break;
	case IPC_GLB_MODULE_OPERATION:
		handled = hsw_module_message(hsw, header);
		break;
	default:
		dev_err(hsw->dev, "error: unexpected type %d hdr 0x%16llx\n",
			type, header);
		break;
	}

	return handled;
}

int sof_hsw_fw_get_version(struct sof_hsw *hsw,
	struct sof_hsw_ipc_fw_version *version)
{
	int ret;

	ret = sof_ipc_tx_message_wait(&hsw->ipc,
		IPC_GLB_TYPE(IPC_GLB_GET_FW_VERSION),
		NULL, 0, version, sizeof(*version));
	if (ret < 0)
		dev_err(hsw->dev, "error: get version failed\n");

	return ret;
}


int sof_hsw_dsp_load(struct sof_hsw *hsw)
{
	struct sof_dsp *dsp = hsw->dsp;
	struct sof_fw *sof_fw, *t;
	int ret;

	dev_dbg(hsw->dev, "loading audio DSP....");

	ret = sof_dsp_wake(dsp);
	if (ret < 0) {
		dev_err(hsw->dev, "error: failed to wake audio DSP\n");
		return -ENODEV;
	}

	ret = sof_dsp_dma_get_channel(dsp, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: cant allocate dma channel %d\n", ret);
		return ret;
	}

	list_for_each_entry_safe_reverse(sof_fw, t, &dsp->fw_list, list) {
		ret = sof_fw_reload(sof_fw);
		if (ret < 0) {
			dev_err(hsw->dev, "error: SST FW reload failed\n");
			sof_dsp_dma_put_channel(dsp);
			return -ENOMEM;
		}
	}
	ret = sof_block_alloc_scratch(hsw->dsp);
	if (ret < 0)
		return -EINVAL;

	sof_dsp_dma_put_channel(dsp);
	return 0;
}

static int sof_hsw_dsp_restore(struct sof_hsw *hsw)
{
	struct sof_dsp *dsp = hsw->dsp;
	int ret = 0;

	dev_dbg(hsw->dev, "restoring audio DSP....");

	ret = sof_dsp_dma_get_channel(dsp, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: cant allocate dma channel %d\n", ret);
		return ret;
	}

	ret = sof_hsw_dx_state_restore(hsw);
	if (ret < 0) {
		dev_err(hsw->dev, "error: SST FW context restore failed\n");
		sof_dsp_dma_put_channel(dsp);
		return -ENOMEM;
	}
	sof_dsp_dma_put_channel(dsp);

	/* wait for DSP boot completion */
	sof_dsp_boot(dsp);

	return ret;
}

int sof_hsw_dsp_runtime_suspend(struct sof_hsw *hsw)
{
	int ret;

	dev_dbg(hsw->dev, "audio dsp runtime suspend\n");

	ret = sof_hsw_dx_set_state(hsw, SST_HSW_DX_STATE_D3, &hsw->dx);
	if (ret < 0)
		return ret;

	sof_dsp_stall(hsw->dsp);

	ret = sof_hsw_dx_state_dump(hsw);
	if (ret < 0)
		return ret;

	sof_ipc_drop_all(&hsw->ipc);

	return 0;
}

int sof_hsw_dsp_runtime_sleep(struct sof_hsw *hsw)
{
	struct sof_fw *sof_fw, *t;
	struct sof_dsp *dsp = hsw->dsp;

	list_for_each_entry_safe(sof_fw, t, &dsp->fw_list, list) {
		sof_fw_unload(sof_fw);
	}
	sof_block_free_scratch(dsp);

	hsw->boot_complete = false;

	sof_dsp_sleep(dsp);

	return 0;
}

int sof_hsw_dsp_runtime_resume(struct sof_hsw *hsw)
{
	struct device *dev = hsw->dev;
	int ret;

	dev_dbg(dev, "audio dsp runtime resume\n");

	if (hsw->boot_complete)
		return 1; /* tell caller no action is required */

	ret = sof_hsw_dsp_restore(hsw);
	if (ret < 0)
		dev_err(dev, "error: audio DSP boot failure\n");

	sof_hsw_init_module_state(hsw);

	ret = wait_event_timeout(hsw->boot_wait, hsw->boot_complete,
		msecs_to_jiffies(IPC_BOOT_MSECS));
	if (ret == 0) {
		dev_err(hsw->dev, "error: audio DSP boot timeout IPCD 0x%x IPCX 0x%x\n",
			sof_dsp_shim_read_unlocked(hsw->dsp, SST_IPCD),
			sof_dsp_shim_read_unlocked(hsw->dsp, SST_IPCX));
		return -EIO;
	}

	/* Set ADSP SSP port settings - sadly the FW does not store SSP port
	   settings as part of the PM context. */
	ret = sof_hsw_device_set_config(hsw, hsw->dx_dev, hsw->dx_mclk,
					hsw->dx_mode, hsw->dx_clock_divider);
	if (ret < 0)
		dev_err(dev, "error: SSP re-initialization failed\n");

	return ret;
}


struct sof_dsp *sof_hsw_get_dsp(struct sof_hsw *hsw)
{
	return hsw->dsp;
}

static void hsw_tx_data_copy(struct snd_sof_ipc_msg *msg, char *tx_data,
	size_t tx_size)
{
	memcpy(msg->tx_data, tx_data, tx_size);
}

static u64 hsw_reply_msg_match(u64 header, u64 *mask)
{
	/* clear reply bits & status bits */
	header &= ~(IPC_STATUS_MASK | IPC_GLB_REPLY_MASK);
	*mask = (u64)-1;

	return header;
}


int sof_hsw_dsp_init(struct device *dev, struct sof_pdata *pdata)
{
	struct sof_hsw_ipc_fw_version version;
	struct sof_hsw *hsw;
	struct snd_sof_ipc *ipc;
	struct sof_dsp_device *dsp_dev;
	int ret;

	dev_dbg(dev, "initialising Audio DSP IPC\n");

	hsw = devm_kzalloc(dev, sizeof(*hsw), GFP_KERNEL);
	if (hsw == NULL)
		return -ENOMEM;

	hsw->dev = dev;

	ipc = &hsw->ipc;
	ipc->dev = dev;

	/* set up ops depending on hardware */
	switch (pdata->id) {
	case SST_DEV_ID_BYT:
		/* Baytrail */
		dsp_dev = &byt_dev;
		dsp_dev->thread_context = hsw;
		ipc->ops.tx_msg = byt_tx_msg;
		ipc->ops.shim_dbg = byt_shim_dbg;
		ipc->ops.tx_data_copy = hsw_tx_data_copy;
		ipc->ops.reply_msg_match = hsw_reply_msg_match;
		ipc->ops.is_dsp_busy = byt_is_dsp_busy;
		ipc->ops.dsp_notify = byt_notify;
		break;
	case SST_DEV_ID_LYNX_POINT:
	case SST_DEV_ID_WILDCAT_POINT:
		/* Haswell / Broadwell */
		dsp_dev = &hsw_dev;
		dsp_dev->thread_context = hsw;
		ipc->ops.tx_msg = hsw_tx_msg;
		ipc->ops.shim_dbg = hsw_shim_dbg;
		ipc->ops.tx_data_copy = hsw_tx_data_copy;
		ipc->ops.reply_msg_match = hsw_reply_msg_match;
		ipc->ops.is_dsp_busy = hsw_is_dsp_busy;
		ipc->ops.dsp_notify = hsw_notify;
		break;
	default:
		ret = -EINVAL;
		dev_err(dev, "error: unsupported DSP ID 0x%x\n", pdata->id);
		goto ipc_init_err;
	}

	ipc->tx_data_max_size = IPC_MAX_MAILBOX_BYTES;
	ipc->rx_data_max_size = IPC_MAX_MAILBOX_BYTES;

	ret = sof_ipc_init(ipc);
	if (ret != 0)
		goto ipc_init_err;

	INIT_LIST_HEAD(&hsw->stream_list);
	init_waitqueue_head(&hsw->boot_wait);

	/* init SST shim */
	hsw->dsp = sof_dsp_new(dev, dsp_dev, pdata);
	if (hsw->dsp == NULL) {
		ret = -ENODEV;
		goto dsp_new_err;
	}

	ipc->dsp = hsw->dsp;

	/* allocate DMA buffer for context storage */
	hsw->dx_context = dma_alloc_coherent(hsw->dsp->dma_dev,
		SST_HSW_DX_CONTEXT_SIZE, &hsw->dx_context_paddr, GFP_KERNEL);
	if (hsw->dx_context == NULL) {
		ret = -ENOMEM;
		goto dma_err;
	}

	/* keep the DSP in reset state for base FW loading */
	sof_dsp_reset(hsw->dsp);

	/* load base module and other modules in base firmware image */
	ret = sof_hsw_module_load(hsw, SST_HSW_MODULE_BASE_FW, 0, "Base");
	if (ret < 0)
		goto fw_err;

	/* try to load module waves */
	sof_hsw_module_load(hsw, SST_HSW_MODULE_WAVES, 0, "intel/IntcPP01.bin");

	/* allocate scratch mem regions */
	ret = sof_block_alloc_scratch(hsw->dsp);
	if (ret < 0)
		goto boot_err;

	/* init param buffer */
	sof_hsw_reset_param_buf(hsw);

	/* wait for DSP boot completion */
	sof_dsp_boot(hsw->dsp);
	ret = wait_event_timeout(hsw->boot_wait, hsw->boot_complete,
		msecs_to_jiffies(IPC_BOOT_MSECS));
	if (ret == 0) {
		ret = -EIO;
		ipc->ops.shim_dbg(ipc, "DSP boot timeout");
		goto boot_err;
	}

	hsw_debugfs_init(hsw);

	/* init module state after boot */
	sof_hsw_init_module_state(hsw);

	/* get the FW version */
	sof_hsw_fw_get_version(hsw, &version);

	/* get the globalmixer */
	ret = sof_hsw_mixer_get_info(hsw);
	if (ret < 0) {
		dev_err(hsw->dev, "error: failed to get stream info\n");
		goto boot_err;
	}

	pdata->dsp = hsw;
	return 0;

boot_err:
	sof_dsp_reset(hsw->dsp);
	sof_fw_free_all(hsw->dsp);
fw_err:
	dma_free_coherent(hsw->dsp->dma_dev, SST_HSW_DX_CONTEXT_SIZE,
			hsw->dx_context, hsw->dx_context_paddr);
dma_err:
	sof_dsp_free(hsw->dsp);
dsp_new_err:
	sof_ipc_fini(ipc);
ipc_init_err:
	return ret;
}
EXPORT_SYMBOL(sof_hsw_dsp_init);

void sof_hsw_dsp_free(struct device *dev, struct sof_pdata *pdata)
{
	struct sof_hsw *hsw = pdata->dsp;

	snd_dma_free_pages(&hsw->trace_dma_descriptor);
	snd_dma_free_pages(&hsw->dtrace_buffer);
	sof_dsp_reset(hsw->dsp);
	sof_fw_free_all(hsw->dsp);
	dma_free_coherent(hsw->dsp->dma_dev, SST_HSW_DX_CONTEXT_SIZE,
			hsw->dx_context, hsw->dx_context_paddr);
	sof_dsp_free(hsw->dsp);
	sof_ipc_fini(&hsw->ipc);
}
EXPORT_SYMBOL(sof_hsw_dsp_free);
#endif
