/*
 *  Intel SST Haswell/Broadwell IPC Support
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

#include "../intel/haswell/sst-haswell-ipc.h"
#include "../intel/common/sst-dsp.h"
#include "../intel/common/sst-dsp-priv.h"
#include "../intel/common/sst-ipc.h"



struct sst_hsw_stream;
struct sst_hsw;

/* Stream infomation */
struct sst_hsw_stream {
	/* configuration */
	struct sst_hsw_ipc_stream_alloc_req request;
	struct sst_hsw_ipc_stream_alloc_reply reply;
	struct sst_hsw_ipc_stream_free_req free_req;

	/* Mixer info */
	u32 mute_volume[SST_HSW_NO_CHANNELS];
	u32 mute[SST_HSW_NO_CHANNELS];

	/* runtime info */
	struct sst_hsw *hsw;
	int host_id;
	bool commited;
	bool running;

	/* Notification work */
	struct work_struct notify_work;
	u32 header;

	/* Position info from DSP */
	struct sst_hsw_ipc_stream_set_position wpos;
	struct sst_hsw_ipc_stream_get_position rpos;
	struct sst_hsw_ipc_stream_glitch_position glitch;

	/* Volume info */
	struct sst_hsw_ipc_volume_req vol_req;

	/* driver callback */
	u32 (*notify_position)(struct sst_hsw_stream *stream, void *data);
	void *pdata;

	/* record the fw read position when playback */
	snd_pcm_uframes_t old_position;
	bool play_silence;
	struct list_head node;
};

/* FW log ring information */
struct sst_hsw_log_stream {
	dma_addr_t dma_addr;
	unsigned char *dma_area;
	unsigned char *ring_descr;
	int pages;
	int size;

	/* Notification work */
	struct work_struct notify_work;
	wait_queue_head_t readers_wait_q;
	struct mutex rw_mutex;

	u32 last_pos;
	u32 curr_pos;
	u32 reader_pos;

	/* fw log config */
	u32 config[SST_HSW_FW_LOG_CONFIG_DWORDS];

	struct sst_hsw *hsw;
};

struct dma_trace_sg {
	int size;
	void *buf;
	struct list_head list;
};



/* SST Haswell IPC data */
struct sst_hsw {
	struct device *dev;
	struct sst_dsp *dsp;
	struct platform_device *pdev_pcm;

	/* FW config */
	struct sst_hsw_ipc_fw_ready fw_ready;
	struct sst_hsw_ipc_fw_version version;
	bool fw_done;
	struct sst_fw *sst_fw;

	/* stream */
	struct list_head stream_list;

	/* global mixer */
	struct sst_hsw_ipc_stream_info_reply mixer_info;
	enum sst_hsw_volume_curve curve_type;
	u32 curve_duration;
	u32 mute[SST_HSW_NO_CHANNELS];
	u32 mute_volume[SST_HSW_NO_CHANNELS];
	bool lbm; /* loopback mode */

	/* DX */
	struct sst_hsw_ipc_dx_reply dx;
	void *dx_context;
	dma_addr_t dx_context_paddr;
	enum sst_hsw_device_id dx_dev;
	enum sst_hsw_device_mclk dx_mclk;
	enum sst_hsw_device_mode dx_mode;
	u32 dx_clock_divider;

	/* boot */
	wait_queue_head_t boot_wait;
	bool boot_complete;
	bool shutdown;

	/* IPC messaging */
	struct sst_generic_ipc ipc;

	/* dma_trace buffer */
	struct snd_dma_buffer trace_dma_descriptor;
	struct sst_hsw_ipc_debug_log_enable_req dt_enable_request;
	struct snd_dma_buffer dtrace_buffer;
	struct dma_trace_buffer host_buffer;

	/* FW log stream */
	struct sst_hsw_log_stream log_stream;
};

#define CREATE_TRACE_POINTS
#include <trace/events/hswadsp.h>

static inline u32 msg_get_global_type(u32 msg)
{
	return (msg & IPC_GLB_TYPE_MASK) >> IPC_GLB_TYPE_SHIFT;
}

static inline u32 msg_get_global_reply(u32 msg)
{
	return (msg & IPC_GLB_REPLY_MASK) >> IPC_GLB_REPLY_SHIFT;
}

static inline u32 msg_get_stream_type(u32 msg)
{
	return (msg & IPC_STR_TYPE_MASK) >>  IPC_STR_TYPE_SHIFT;
}

static inline u32 msg_get_stage_type(u32 msg)
{
	return (msg & IPC_STG_TYPE_MASK) >>  IPC_STG_TYPE_SHIFT;
}

static inline u32 msg_get_stream_id(u32 msg)
{
	return (msg & IPC_STR_ID_MASK) >>  IPC_STR_ID_SHIFT;
}

static inline u32 msg_get_notify_reason(u32 msg)
{
	return (msg & IPC_STG_TYPE_MASK) >> IPC_STG_TYPE_SHIFT;
}

static inline u32 msg_get_module_operation(u32 msg)
{
	return (msg & IPC_MODULE_OPERATION_MASK) >> IPC_MODULE_OPERATION_SHIFT;
}

static inline u32 msg_get_module_id(u32 msg)
{
	return (msg & IPC_MODULE_ID_MASK) >> IPC_MODULE_ID_SHIFT;
}

u32 create_channel_map(enum sst_hsw_channel_config config)
{
	switch (config) {
	case SST_HSW_CHANNEL_CONFIG_MONO:
		return (0xFFFFFFF0 | SST_HSW_CHANNEL_CENTER);
	case SST_HSW_CHANNEL_CONFIG_STEREO:
		return (0xFFFFFF00 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_RIGHT << 4));
	case SST_HSW_CHANNEL_CONFIG_2_POINT_1:
		return (0xFFFFF000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_RIGHT << 4)
			| (SST_HSW_CHANNEL_LFE << 8 ));
	case SST_HSW_CHANNEL_CONFIG_3_POINT_0:
		return (0xFFFFF000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_CENTER << 4)
			| (SST_HSW_CHANNEL_RIGHT << 8));
	case SST_HSW_CHANNEL_CONFIG_3_POINT_1:
		return (0xFFFF0000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_CENTER << 4)
			| (SST_HSW_CHANNEL_RIGHT << 8)
			| (SST_HSW_CHANNEL_LFE << 12));
	case SST_HSW_CHANNEL_CONFIG_QUATRO:
		return (0xFFFF0000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_RIGHT << 4)
			| (SST_HSW_CHANNEL_LEFT_SURROUND << 8)
			| (SST_HSW_CHANNEL_RIGHT_SURROUND << 12));
	case SST_HSW_CHANNEL_CONFIG_4_POINT_0:
		return (0xFFFF0000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_CENTER << 4)
			| (SST_HSW_CHANNEL_RIGHT << 8)
			| (SST_HSW_CHANNEL_CENTER_SURROUND << 12));
	case SST_HSW_CHANNEL_CONFIG_5_POINT_0:
		return (0xFFF00000 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_CENTER << 4)
			| (SST_HSW_CHANNEL_RIGHT << 8)
			| (SST_HSW_CHANNEL_LEFT_SURROUND << 12)
			| (SST_HSW_CHANNEL_RIGHT_SURROUND << 16));
	case SST_HSW_CHANNEL_CONFIG_5_POINT_1:
		return (0xFF000000 | SST_HSW_CHANNEL_CENTER
			| (SST_HSW_CHANNEL_LEFT << 4)
			| (SST_HSW_CHANNEL_RIGHT << 8)
			| (SST_HSW_CHANNEL_LEFT_SURROUND << 12)
			| (SST_HSW_CHANNEL_RIGHT_SURROUND << 16)
			| (SST_HSW_CHANNEL_LFE << 20));
	case SST_HSW_CHANNEL_CONFIG_DUAL_MONO:
		return (0xFFFFFF00 | SST_HSW_CHANNEL_LEFT
			| (SST_HSW_CHANNEL_LEFT << 4));
	default:
		return 0xFFFFFFFF;
	}
}

static struct sst_hsw_stream *get_stream_by_id(struct sst_hsw *hsw,
	int stream_id)
{
	struct sst_hsw_stream *stream;

	list_for_each_entry(stream, &hsw->stream_list, node) {
		if (stream->reply.stream_hw_id == stream_id)
			return stream;
	}

	return NULL;
}





#define DMA_TRACE_PAGE_NUMBER 4
static int hsw_setup_dma_trace_page_table(struct sst_hsw *hsw)
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




static void hsw_fw_ready(struct sst_hsw *hsw, u32 header)
{
	struct sst_hsw_ipc_fw_ready fw_ready;
	u32 offset;
	u8 fw_info[IPC_MAX_MAILBOX_BYTES - 5 * sizeof(u32)];
	char *tmp[5], *pinfo;
	int i = 0;

	offset = (header & 0x1FFFFFFF) << 3;

	dev_dbg(hsw->dev, "ipc: DSP is ready 0x%8.8x offset %d\n",
		header, offset);

	/* copy data from the DSP FW ready offset */
	sst_dsp_read(hsw->dsp, &fw_ready, offset, sizeof(fw_ready));

	sst_dsp_mailbox_init(hsw->dsp, fw_ready.inbox_offset,
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

static void hsw_notification_work(struct work_struct *work)
{
	struct sst_hsw_stream *stream = container_of(work,
			struct sst_hsw_stream, notify_work);
	struct sst_hsw_ipc_stream_glitch_position *glitch = &stream->glitch;
	struct sst_hsw_ipc_stream_get_position *pos = &stream->rpos;
	struct sst_hsw *hsw = stream->hsw;
	u32 reason;

	reason = msg_get_notify_reason(stream->header);

	switch (reason) {
	case IPC_STG_GLITCH:
		trace_ipc_notification("DSP stream under/overrun",
			stream->reply.stream_hw_id);
		sst_dsp_inbox_read(hsw->dsp, glitch, sizeof(*glitch));

		dev_err(hsw->dev, "glitch %d pos 0x%x write pos 0x%x\n",
			glitch->glitch_type, glitch->present_pos,
			glitch->write_pos);
		break;

	case IPC_POSITION_CHANGED:
		trace_ipc_notification("DSP stream position changed for",
			stream->reply.stream_hw_id);
		sst_dsp_inbox_read(hsw->dsp, pos, sizeof(*pos));

		if (stream->notify_position)
			stream->notify_position(stream, stream->pdata);

		break;
	default:
		dev_err(hsw->dev, "error: unknown notification 0x%x\n",
			stream->header);
		break;
	}

	/* tell DSP that notification has been handled */
	hsw->ipc.ops.dsp_notify(hsw->dsp);

	/* unmask busy interrupt */
	sst_dsp_shim_update_bits(hsw->dsp, SST_IMRX, SST_IMRX_BUSY, 0);
}

static void hsw_stream_update(struct sst_hsw *hsw, struct ipc_message *msg)
{
	struct sst_hsw_stream *stream;
	u32 header = msg->header & ~(IPC_STATUS_MASK | IPC_GLB_REPLY_MASK);
	u32 stream_id = msg_get_stream_id(header);
	u32 stream_msg = msg_get_stream_type(header);

	stream = get_stream_by_id(hsw, stream_id);
	if (stream == NULL)
		return;

	switch (stream_msg) {
	case IPC_STR_STAGE_MESSAGE:
	case IPC_STR_NOTIFICATION:
		break;
	case IPC_STR_RESET:
		trace_ipc_notification("stream reset", stream->reply.stream_hw_id);
		break;
	case IPC_STR_PAUSE:
		stream->running = false;
		trace_ipc_notification("stream paused",
			stream->reply.stream_hw_id);
		break;
	case IPC_STR_RESUME:
		stream->running = true;
		trace_ipc_notification("stream running",
			stream->reply.stream_hw_id);
		break;
	}
}

static int hsw_process_reply(struct sst_hsw *hsw, u32 header)
{
	struct ipc_message *msg;
	u32 reply = msg_get_global_reply(header);

	trace_ipc_reply("processing -->", header);

	msg = sst_ipc_reply_find_msg(&hsw->ipc, header);
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
			sst_dsp_inbox_read(hsw->dsp, msg->rx_data,
				msg->rx_size);
			hsw->ipc.pending = false;
		} else {
			/* copy data from the DSP */
			sst_dsp_inbox_read(hsw->dsp, msg->rx_data,
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
	sst_ipc_tx_msg_reply_complete(&hsw->ipc, msg);

	return 1;
}

static int hsw_module_message(struct sst_hsw *hsw, u32 header)
{
	u32 operation, module_id;
	int handled = 0;

	operation = msg_get_module_operation(header);
	module_id = msg_get_module_id(header);
	dev_dbg(hsw->dev, "received module message header: 0x%8.8x\n",
			header);
	dev_dbg(hsw->dev, "operation: 0x%8.8x module_id: 0x%8.8x\n",
			operation, module_id);

	switch (operation) {
	case IPC_MODULE_NOTIFICATION:
		dev_dbg(hsw->dev, "module notification received");
		handled = 1;
		break;
	default:
		handled = hsw_process_reply(hsw, header);
		break;
	}

	return handled;
}

static int hsw_stream_message(struct sst_hsw *hsw, u32 header)
{
	u32 stream_msg, stream_id, stage_type;
	struct sst_hsw_stream *stream;
	int handled = 0;

	stream_msg = msg_get_stream_type(header);
	stream_id = msg_get_stream_id(header);
	stage_type = msg_get_stage_type(header);

	stream = get_stream_by_id(hsw, stream_id);
	if (stream == NULL)
		return handled;

	stream->header = header;

	switch (stream_msg) {
	case IPC_STR_STAGE_MESSAGE:
		dev_err(hsw->dev, "error: stage msg not implemented 0x%8.8x\n",
			header);
		break;
	case IPC_STR_NOTIFICATION:
		schedule_work(&stream->notify_work);
		break;
	default:
		/* handle pending message complete request */
		handled = hsw_process_reply(hsw, header);
		break;
	}

	return handled;
}

static int hsw_log_message(struct sst_hsw *hsw, u32 header)
{
	u32 operation = (header & IPC_LOG_OP_MASK) >>  IPC_LOG_OP_SHIFT;
	struct sst_hsw_log_stream *stream = &hsw->log_stream;
	struct sst_hsw_ipc_debug_log_pos_update pos;
	struct dma_trace_buffer *hbuf = &hsw->host_buffer;
	int ret = 1;

	sst_dsp_shim_update_bits64_unlocked(hsw->dsp, SST_IPCD,
			SST_BYT_IPCD_BUSY | SST_BYT_IPCD_DONE,
				SST_BYT_IPCD_DONE);
	sst_dsp_shim_update_bits_unlocked(hsw->dsp, SST_IMRX, SST_IMRX_BUSY, 0);

	if (operation != IPC_DEBUG_REQUEST_LOG_DUMP) {
		dev_err(hsw->dev,
			"error: log msg not implemented 0x%8.8x\n", header);
		return 0;
	}

	stream->last_pos = stream->curr_pos;
	pos.log_size = 0;
	sst_dsp_inbox_read(hsw->dsp, &pos, sizeof(pos));
	hbuf->pending_size += pos.log_size;
	/*
	 * if pending_size > hbuf->size, it means
	 * some msg data has been overwritten
	 */
	if (hbuf->pending_size > hbuf->size)
		hbuf->pending_size = hbuf->size;

	return ret;
}

static int hsw_process_notification(struct sst_hsw *hsw, u64 header)
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

int sst_hsw_fw_get_version(struct sst_hsw *hsw,
	struct sst_hsw_ipc_fw_version *version)
{
	int ret;

	ret = sst_ipc_tx_message_wait(&hsw->ipc,
		IPC_GLB_TYPE(IPC_GLB_GET_FW_VERSION),
		NULL, 0, version, sizeof(*version));
	if (ret < 0)
		dev_err(hsw->dev, "error: get version failed\n");

	return ret;
}





/* physical BE config */
int sst_hsw_device_set_config(struct sst_hsw *hsw,
	enum sst_hsw_device_id dev, enum sst_hsw_device_mclk mclk,
	enum sst_hsw_device_mode mode, u32 clock_divider)
{
	struct sst_hsw_ipc_device_config_req config;
	u32 header;
	int ret;

	trace_ipc_request("set device config", dev);

	hsw->dx_dev = config.ssp_interface = dev;
	hsw->dx_mclk = config.clock_frequency = mclk;
	hsw->dx_mode = config.mode = mode;
	hsw->dx_clock_divider = config.clock_divider = clock_divider;
	if (mode == SST_HSW_DEVICE_TDM_CLOCK_MASTER)
		config.channels = 4;
	else
		config.channels = 2;

	trace_hsw_device_config_req(&config);

	header = IPC_GLB_TYPE(IPC_GLB_SET_DEVICE_FORMATS);

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, &config,
		sizeof(config), NULL, 0);
	if (ret < 0)
		dev_err(hsw->dev, "error: set device formats failed\n");

	return ret;
}
EXPORT_SYMBOL_GPL(sst_hsw_device_set_config);

/* loopback mode config, for loopback debug */
int sst_hsw_device_set_loopback(struct sst_hsw *hsw, bool lbm)
{
	int ret;

	trace_ipc_request("set loopback", lbm);

	ret = sst_ipc_tx_message_wait(&hsw->ipc,
		IPC_GLB_TYPE((lbm ? IPC_GLB_ENABLE_LOOPBACK : IPC_GLB_DISABLE_LOOPBACK)),
		NULL, 0, NULL, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: set loopback failed\n");
		return ret;
	}
	hsw->lbm = lbm;
	return ret;
}
EXPORT_SYMBOL_GPL(sst_hsw_device_set_loopback);

/* loopback mode config, for loopback debug */
bool sst_hsw_device_get_loopback(struct sst_hsw *hsw)
{
	return hsw->lbm;
}
EXPORT_SYMBOL_GPL(sst_hsw_device_get_loopback);

/* DX Config */
int sst_hsw_dx_set_state(struct sst_hsw *hsw,
	enum sst_hsw_dx_state state, struct sst_hsw_ipc_dx_reply *dx)
{
	u32 header, state_;
	int ret, item;

	header = IPC_GLB_TYPE(IPC_GLB_ENTER_DX_STATE);
	state_ = state;

	trace_ipc_request("PM enter Dx state", state);

	ret = sst_ipc_tx_message_wait(&hsw->ipc, header, &state_,
		sizeof(state_), dx, sizeof(*dx));
	if (ret < 0) {
		dev_err(hsw->dev, "ipc: error set dx state %d failed\n", state);
		return ret;
	}

	for (item = 0; item < dx->entries_no; item++) {
		dev_dbg(hsw->dev,
			"Item[%d] offset[%x] - size[%x] - source[%x]\n",
			item, dx->mem_info[item].offset,
			dx->mem_info[item].size,
			dx->mem_info[item].source);
	}
	dev_dbg(hsw->dev, "ipc: got %d entry numbers for state %d\n",
		dx->entries_no, state);

	return ret;
}

#ifdef CONFIG_PM
static int sst_hsw_dx_state_dump(struct sst_hsw *hsw)
{
	struct sst_dsp *sst = hsw->dsp;
	u32 item, offset, size;
	int ret = 0;

	trace_ipc_request("PM state dump. Items #", SST_HSW_MAX_DX_REGIONS);

	if (hsw->dx.entries_no > SST_HSW_MAX_DX_REGIONS) {
		dev_err(hsw->dev,
			"error: number of FW context regions greater than %d\n",
			SST_HSW_MAX_DX_REGIONS);
		memset(&hsw->dx, 0, sizeof(hsw->dx));
		return -EINVAL;
	}

	ret = sst_dsp_dma_get_channel(sst, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: cant allocate dma channel %d\n", ret);
		return ret;
	}

	/* set on-demond mode on engine 0 channel 3 */
	sst_dsp_shim_update_bits(sst, SST_HMDC,
			SST_HMDC_HDDA_E0_ALLCH | SST_HMDC_HDDA_E1_ALLCH,
			SST_HMDC_HDDA_E0_ALLCH | SST_HMDC_HDDA_E1_ALLCH);

	for (item = 0; item < hsw->dx.entries_no; item++) {
		if (hsw->dx.mem_info[item].source == SST_HSW_DX_TYPE_MEMORY_DUMP
			&& hsw->dx.mem_info[item].offset > DSP_DRAM_ADDR_OFFSET
			&& hsw->dx.mem_info[item].offset <
			DSP_DRAM_ADDR_OFFSET + SST_HSW_DX_CONTEXT_SIZE) {

			offset = hsw->dx.mem_info[item].offset
					- DSP_DRAM_ADDR_OFFSET;
			size = (hsw->dx.mem_info[item].size + 3) & (~3);

			ret = sst_dsp_dma_copyfrom(sst, hsw->dx_context_paddr + offset,
				sst->addr.lpe_base + offset, size);
			if (ret < 0) {
				dev_err(hsw->dev,
					"error: FW context dump failed\n");
				memset(&hsw->dx, 0, sizeof(hsw->dx));
				goto out;
			}
		}
	}

out:
	sst_dsp_dma_put_channel(sst);
	return ret;
}

static int sst_hsw_dx_state_restore(struct sst_hsw *hsw)
{
	struct sst_dsp *sst = hsw->dsp;
	u32 item, offset, size;
	int ret;

	for (item = 0; item < hsw->dx.entries_no; item++) {
		if (hsw->dx.mem_info[item].source == SST_HSW_DX_TYPE_MEMORY_DUMP
			&& hsw->dx.mem_info[item].offset > DSP_DRAM_ADDR_OFFSET
			&& hsw->dx.mem_info[item].offset <
			DSP_DRAM_ADDR_OFFSET + SST_HSW_DX_CONTEXT_SIZE) {

			offset = hsw->dx.mem_info[item].offset
					- DSP_DRAM_ADDR_OFFSET;
			size = (hsw->dx.mem_info[item].size + 3) & (~3);

			ret = sst_dsp_dma_copyto(sst, sst->addr.lpe_base + offset,
				hsw->dx_context_paddr + offset, size);
			if (ret < 0) {
				dev_err(hsw->dev,
					"error: FW context restore failed\n");
				return ret;
			}
		}
	}

	return 0;
}

int sst_hsw_dsp_load(struct sst_hsw *hsw)
{
	struct sst_dsp *dsp = hsw->dsp;
	struct sst_fw *sst_fw, *t;
	int ret;

	dev_dbg(hsw->dev, "loading audio DSP....");

	ret = sst_dsp_wake(dsp);
	if (ret < 0) {
		dev_err(hsw->dev, "error: failed to wake audio DSP\n");
		return -ENODEV;
	}

	ret = sst_dsp_dma_get_channel(dsp, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: cant allocate dma channel %d\n", ret);
		return ret;
	}

	list_for_each_entry_safe_reverse(sst_fw, t, &dsp->fw_list, list) {
		ret = sst_fw_reload(sst_fw);
		if (ret < 0) {
			dev_err(hsw->dev, "error: SST FW reload failed\n");
			sst_dsp_dma_put_channel(dsp);
			return -ENOMEM;
		}
	}
	ret = sst_block_alloc_scratch(hsw->dsp);
	if (ret < 0)
		return -EINVAL;

	sst_dsp_dma_put_channel(dsp);
	return 0;
}

static int sst_hsw_dsp_restore(struct sst_hsw *hsw)
{
	struct sst_dsp *dsp = hsw->dsp;
	int ret = 0;

	dev_dbg(hsw->dev, "restoring audio DSP....");

	ret = sst_dsp_dma_get_channel(dsp, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "error: cant allocate dma channel %d\n", ret);
		return ret;
	}

	ret = sst_hsw_dx_state_restore(hsw);
	if (ret < 0) {
		dev_err(hsw->dev, "error: SST FW context restore failed\n");
		sst_dsp_dma_put_channel(dsp);
		return -ENOMEM;
	}
	sst_dsp_dma_put_channel(dsp);

	/* wait for DSP boot completion */
	sst_dsp_boot(dsp);

	return ret;
}

int sst_hsw_dsp_runtime_suspend(struct sst_hsw *hsw)
{
	int ret;

	dev_dbg(hsw->dev, "audio dsp runtime suspend\n");

	ret = sst_hsw_dx_set_state(hsw, SST_HSW_DX_STATE_D3, &hsw->dx);
	if (ret < 0)
		return ret;

	sst_dsp_stall(hsw->dsp);

	ret = sst_hsw_dx_state_dump(hsw);
	if (ret < 0)
		return ret;

	sst_ipc_drop_all(&hsw->ipc);

	return 0;
}

int sst_hsw_dsp_runtime_sleep(struct sst_hsw *hsw)
{
	struct sst_fw *sst_fw, *t;
	struct sst_dsp *dsp = hsw->dsp;

	list_for_each_entry_safe(sst_fw, t, &dsp->fw_list, list) {
		sst_fw_unload(sst_fw);
	}
	sst_block_free_scratch(dsp);

	hsw->boot_complete = false;

	sst_dsp_sleep(dsp);

	return 0;
}

int sst_hsw_dsp_runtime_resume(struct sst_hsw *hsw)
{
	struct device *dev = hsw->dev;
	int ret;

	dev_dbg(dev, "audio dsp runtime resume\n");

	if (hsw->boot_complete)
		return 1; /* tell caller no action is required */

	ret = sst_hsw_dsp_restore(hsw);
	if (ret < 0)
		dev_err(dev, "error: audio DSP boot failure\n");

	sst_hsw_init_module_state(hsw);

	ret = wait_event_timeout(hsw->boot_wait, hsw->boot_complete,
		msecs_to_jiffies(IPC_BOOT_MSECS));
	if (ret == 0) {
		dev_err(hsw->dev, "error: audio DSP boot timeout IPCD 0x%x IPCX 0x%x\n",
			sst_dsp_shim_read_unlocked(hsw->dsp, SST_IPCD),
			sst_dsp_shim_read_unlocked(hsw->dsp, SST_IPCX));
		return -EIO;
	}

	/* Set ADSP SSP port settings - sadly the FW does not store SSP port
	   settings as part of the PM context. */
	ret = sst_hsw_device_set_config(hsw, hsw->dx_dev, hsw->dx_mclk,
					hsw->dx_mode, hsw->dx_clock_divider);
	if (ret < 0)
		dev_err(dev, "error: SSP re-initialization failed\n");

	return ret;
}
#endif

struct sst_dsp *sst_hsw_get_dsp(struct sst_hsw *hsw)
{
	return hsw->dsp;
}

static void hsw_tx_data_copy(struct ipc_message *msg, char *tx_data,
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


int sst_hsw_dsp_init(struct device *dev, struct sst_pdata *pdata)
{
	struct sst_hsw_ipc_fw_version version;
	struct sst_hsw *hsw;
	struct sst_generic_ipc *ipc;
	struct sst_dsp_device *dsp_dev;
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

	ret = sst_ipc_init(ipc);
	if (ret != 0)
		goto ipc_init_err;

	INIT_LIST_HEAD(&hsw->stream_list);
	init_waitqueue_head(&hsw->boot_wait);

	/* init SST shim */
	hsw->dsp = sst_dsp_new(dev, dsp_dev, pdata);
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
	sst_dsp_reset(hsw->dsp);

	/* load base module and other modules in base firmware image */
	ret = sst_hsw_module_load(hsw, SST_HSW_MODULE_BASE_FW, 0, "Base");
	if (ret < 0)
		goto fw_err;

	/* try to load module waves */
	sst_hsw_module_load(hsw, SST_HSW_MODULE_WAVES, 0, "intel/IntcPP01.bin");

	/* allocate scratch mem regions */
	ret = sst_block_alloc_scratch(hsw->dsp);
	if (ret < 0)
		goto boot_err;

	/* init param buffer */
	sst_hsw_reset_param_buf(hsw);

	/* wait for DSP boot completion */
	sst_dsp_boot(hsw->dsp);
	ret = wait_event_timeout(hsw->boot_wait, hsw->boot_complete,
		msecs_to_jiffies(IPC_BOOT_MSECS));
	if (ret == 0) {
		ret = -EIO;
		ipc->ops.shim_dbg(ipc, "DSP boot timeout");
		goto boot_err;
	}

	hsw_debugfs_init(hsw);

	/* init module state after boot */
	sst_hsw_init_module_state(hsw);

	/* get the FW version */
	sst_hsw_fw_get_version(hsw, &version);

	/* get the globalmixer */
	ret = sst_hsw_mixer_get_info(hsw);
	if (ret < 0) {
		dev_err(hsw->dev, "error: failed to get stream info\n");
		goto boot_err;
	}

	pdata->dsp = hsw;
	return 0;

boot_err:
	sst_dsp_reset(hsw->dsp);
	sst_fw_free_all(hsw->dsp);
fw_err:
	dma_free_coherent(hsw->dsp->dma_dev, SST_HSW_DX_CONTEXT_SIZE,
			hsw->dx_context, hsw->dx_context_paddr);
dma_err:
	sst_dsp_free(hsw->dsp);
dsp_new_err:
	sst_ipc_fini(ipc);
ipc_init_err:
	return ret;
}
EXPORT_SYMBOL_GPL(sst_hsw_dsp_init);

void sst_hsw_dsp_free(struct device *dev, struct sst_pdata *pdata)
{
	struct sst_hsw *hsw = pdata->dsp;

	snd_dma_free_pages(&hsw->trace_dma_descriptor);
	snd_dma_free_pages(&hsw->dtrace_buffer);
	sst_dsp_reset(hsw->dsp);
	sst_fw_free_all(hsw->dsp);
	dma_free_coherent(hsw->dsp->dma_dev, SST_HSW_DX_CONTEXT_SIZE,
			hsw->dx_context, hsw->dx_context_paddr);
	sst_dsp_free(hsw->dsp);
	sst_ipc_fini(&hsw->ipc);
}
EXPORT_SYMBOL_GPL(sst_hsw_dsp_free);
