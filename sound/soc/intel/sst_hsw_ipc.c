/*
 *  sst_hsw_ipc.c - Intel SST Haswell FW ABI
 *
 *  Copyright (C) 2013	Intel Corp
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
#include <linux/list.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>

#include "sst_hsw_ipc.h"
#include "sst_dsp.h"

/* Global Message - Generic */
#define IPC_GLB_TYPE_SHIFT	24
#define IPC_GLB_TYPE_MASK	(0xf << IPC_GLB_TYPE_SHIFT)
#define IPC_GLB_TYPE(x)		(x << IPC_GLB_TYPE_SHIFT)

/* Global Message - Reply */
#define IPC_GLB_REPLY_SHIFT	0
#define IPC_GLB_REPLY_MASK	(0x1f << IPC_GLB_REPLY_SHIFT)
#define IPC_GLB_REPLY_TYPE(x)	(x << IPC_GLB_REPLY_TYPE_SHIFT)

/* Stream Message - Generic */
#define IPC_STR_TYPE_SHIFT	20
#define IPC_STR_TYPE_MASK	(0xf << IPC_STR_TYPE_SHIFT)
#define IPC_STR_TYPE(x)		(x << IPC_STR_TYPE_SHIFT)
#define IPC_STR_ID_SHIFT	16
#define IPC_STR_ID_MASK		(0xf << IPC_STR_ID_SHIFT)
#define IPC_STR_ID(x)		(x << IPC_STR_ID_SHIFT)

/* Stream Message - Reply */
#define IPC_STR_REPLY_SHIFT	0
#define IPC_STR_REPLY_MASK	(0x1f << IPC_STR_REPLY_SHIFT)

/* Stream Stage Message - Generic */
#define IPC_STG_TYPE_SHIFT	12
#define IPC_STG_TYPE_MASK	(0xf << IPC_STG_TYPE_SHIFT)
#define IPC_STG_TYPE(x)		(x << IPC_STG_TYPE_SHIFT)
#define IPC_STG_ID_SHIFT	10
#define IPC_STG_ID_MASK		(0x3 << IPC_STG_ID_SHIFT)
#define IPC_STG_ID(x)		(x << IPC_STG_ID_SHIFT)

/* Stream Stage Message - Reply */
#define IPC_STG_REPLY_SHIFT	0
#define IPC_STG_REPLY_MASK	(0x1f << IPC_STG_REPLY_SHIFT)

/* IPC message timeout (msecs) TODO: get better value */
#define IPC_TIMEOUT_MSECS	300
#define IPC_BOOT_MSECS		200
#define IPC_MSG_WAIT		0
#define IPC_MSG_NOWAIT		1

/* Firmware Ready Message */
#define IPC_FW_READY		(0x1 << 29)
#define IPC_STATUS_MASK		(0x3 << 30)

#define IPC_EMPTY_LIST_SIZE	8
#define IPC_MAX_STREAMS		4

/* Global Message - Types and Replies */
enum ipc_glb_type {
	IPC_GLB_GET_FW_VERSION = 0,		/**< Retrieves firmware version */
	IPC_GLB_ALLOCATE_STREAM = 3,		/**< Request to allocate new stream */
	IPC_GLB_FREE_STREAM = 4,			/**< Request to free stream */
	IPC_GLB_GET_FW_CAPABILITIES = 5,		/**< Retrieves firmware capabilities */
	IPC_GLB_STREAM_MESSAGE = 6,		/**< Message directed to stream or its stages */
	/** Request to store firmware context during D0->D3 transition */
	IPC_GLB_SAVE_CONTEXT = 7,
	/** Request to restore firmware context during D3->D0 transition */
	IPC_GLB_RESTORE_CONTEXT = 8,
	IPC_GLB_GET_DEVICE_FORMATS = 9,		/**< TODO: Add description */
	IPC_GLB_SET_DEVICE_FORMATS = 10,		/**< TODO: Add description */
	IPC_GLB_SHORT_REPLY = 11,
	IPC_GLB_ENTER_DX_STATE = 12,
	IPC_GLB_GET_MIXER_STREAM_INFO = 13,	/** < Request mixer stream params */
	IPC_GLB_DEBUG_LOG_MESSAGE = 14,		/* Message to or from the debug logger. */
	IPC_GLB_MAX_IPC_MESSAGE_TYPE = 15,	/**< Maximum message number */
};

enum ipc_glb_reply {
	IPC_GLB_REPLY_SUCCESS = 0,		/**< The operation was successful. */
	IPC_GLB_REPLY_ERROR_INVALID_PARAM = 1,	/**< Invalid parameter was passed. */
	IPC_GLB_REPLY_UNKNOWN_MESSAGE_TYPE = 2,		/**< Uknown message type was resceived. */
	IPC_GLB_REPLY_OUT_OF_RESOURCES = 3,	/**< No resources to satisfy the request. */
	IPC_GLB_REPLY_BUSY = 4,				/**< The system or resource is busy. */
	IPC_GLB_REPLY_PENDING = 5,		/**< The action was scheduled for processing.  */
	IPC_GLB_REPLY_FAILURE = 6,		/**< Critical error happened. */
	IPC_GLB_REPLY_INVALID_REQUEST = 7,	/**< Request can not be completed. */
	IPC_GLB_REPLY_STAGE_UNINITIALIZED = 8,	/**< Processing stage was uninitialized. */
	IPC_GLB_REPLY_NOT_FOUND = 9,		/**< Required resource can not be found. */
	IPC_GLB_REPLY_SOURCE_NOT_STARTED = 10,	/**< Source was not started. */
};

/* Stream Message - Types */
enum ipc_str_operation {
	IPC_STR_RESET = 0,
	IPC_STR_PAUSE = 1,
	IPC_STR_RESUME = 2,
	IPC_STR_STAGE_MESSAGE = 3,
	IPC_STR_NOTIFICATION = 4,
	IPC_STR_MAX_MESSAGE
};

/* Stream Stage Message Types */
enum ipc_stg_operation {
	IPC_STG_GET_VOLUME = 0,
	IPC_STG_SET_VOLUME,
	IPC_STG_SET_WRITE_POSITION,
	IPC_STG_MUTE_LOOPBACK,
	IPC_STG_MAX_MESSAGE
};

/* Stream Stage Message Types For Notification*/
enum ipc_stg_operation_notify {
	IPC_POSITION_CHANGED = 0,
	IPC_STG_GLITCH,
	IPC_STG_MAX_NOTIFY
};

enum ipc_glitch_type {
        IPC_GLITCH_UNDERRUN = 1,
        IPC_GLITCH_DECODER_ERROR,
        IPC_GLITCH_DOUBLED_WRITE_POS,
        IPC_GLITCH_MAX
};

/* Debug Control */
enum ipc_debug_operation {
	IPC_DEBUG_ENABLE_LOG = 0,
	IPC_DEBUG_DISABLE_LOG = 1,
	IPC_DEBUG_REQUEST_LOG_DUMP = 2,
	IPC_DEBIG_NOTIFY_LOG_DUMP = 3,
	IPC_DEBUG_MAX_DEBUG_LOG
};

/* Firmware Ready */
struct sst_hsw_ipc_fw_ready {
	uint32_t inbox_offset;
	uint32_t outbox_offset;
	uint32_t inbox_size;
	uint32_t outbox_size;
} __attribute__((packed));

struct ipc_message {
	struct list_head list;
	u32 header;

	/* direction wrt host CPU */
	char tx_data[256];
	size_t tx_size;
	char rx_data[256];
	size_t rx_size;

	wait_queue_head_t waitq;
	bool pending;
	bool complete;
	bool wait;
	int errno;
};

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
	u32 header; //TODO: Get header from message

	/* Position info from DSP */
	struct sst_hsw_ipc_stream_set_position wpos;
	struct sst_hsw_ipc_stream_get_position rpos;
	struct sst_hsw_ipc_stream_glitch_position glitch;

	/* Volume info */
	struct sst_hsw_ipc_volume_req vol_req;

	/* driver callback */
	u32 (*notify_position)(struct sst_hsw_stream *stream, void *data);
	void *pdata;

	struct list_head node;
};

/* SST Haswell IPC data */
struct sst_hsw {
	struct device *dev;
	struct sst_dsp *dsp;

	/* FW config */
	struct sst_hsw_ipc_fw_ready fw_ready;
	struct sst_hsw_ipc_fw_version version;
	bool fw_done;

	/* stream */
	struct list_head stream_list;

	/* global mixer */
	struct sst_hsw_ipc_stream_info_reply mixer_info;
	enum sst_hsw_ipc_volume_curve_type curve_type;
	u32 curve_duration;
	u32 mute[SST_HSW_NO_CHANNELS];
	u32 mute_volume[SST_HSW_NO_CHANNELS];

	/* DX */
	struct sst_hsw_ipc_dx_reply dx;

	/* boot */
	wait_queue_head_t boot_wait;
	bool boot_complete;
	bool shutdown;

	/* IPC messaging */
	struct list_head tx_list;
	struct list_head rx_list;
	struct list_head empty_list;
	wait_queue_head_t wait_txq;
	spinlock_t ipc_lock;
	struct task_struct *tx_thread;
	struct kthread_worker kworker;
	struct kthread_work kwork;
	bool pending;
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

static inline u32 msg_set_stage_type(u32 msg, u32 type)
{
	return (msg & ~IPC_STG_TYPE_MASK) +
		(type << IPC_STG_TYPE_SHIFT);
}

static inline u32 msg_get_stream_id(u32 msg)
{
	return (msg & IPC_STR_ID_MASK) >>  IPC_STR_ID_SHIFT;
}

static inline u32 msg_get_notify_reason(u32 msg)
{
	return (msg & IPC_STG_TYPE_MASK) >> IPC_STG_TYPE_SHIFT;
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

static void ipc_shim_dbg(struct sst_hsw *hsw, const char *text)
{
	struct sst_dsp *sst = hsw->dsp;
	u32 isr, ipcd, imrx, ipcx;

	ipcx = sst_dsp_shim_read_unlocked(sst, SST_IPCX);
	isr = sst_dsp_shim_read_unlocked(sst, SST_ISRX);
	ipcd = sst_dsp_shim_read_unlocked(sst, SST_IPCD);
	imrx = sst_dsp_shim_read_unlocked(sst, SST_IMRX);

	dev_err(hsw->dev, "ipc: --%s-- ipcx 0x%8.8x isr 0x%8.8x ipcd 0x%8.8x imrx 0x%8.8x\n",
		text, ipcx, isr, ipcd, imrx);
}

/* locks held by caller */
static struct ipc_message *msg_get_empty(struct sst_hsw *hsw)
{
	struct ipc_message *msg = NULL;

	if (!list_empty(&hsw->empty_list)) {
		msg = list_first_entry(&hsw->empty_list, struct ipc_message, list);
		list_del(&msg->list);
	}

	return msg;
}

/* locks held by caller */
static void msg_put_empty(struct sst_hsw *hsw, struct ipc_message *msg)
{
	list_add_tail(&msg->list, &hsw->empty_list);
}

static void ipc_tx_msgs(struct kthread_work *work)
{
	struct sst_hsw *hsw =
		container_of(work, struct sst_hsw, kwork);
	struct ipc_message *msg;
	u32 ipcx;

	spin_lock(&hsw->ipc_lock);
	if (list_empty(&hsw->tx_list) || hsw->pending) {
		spin_unlock(&hsw->ipc_lock);
		return;
	}

	/* if the DSP is busy we will TX messages after IRQ */
	ipcx = sst_dsp_shim_read(hsw->dsp, SST_IPCX);
	if (ipcx & SST_IPCX_BUSY) {
		spin_unlock(&hsw->ipc_lock);
		return;
	}

	msg = list_first_entry(&hsw->tx_list, struct ipc_message, list);

	list_move(&msg->list, &hsw->rx_list);

	/* send the message */
	sst_dsp_outbox_write(hsw->dsp, msg->tx_data, msg->tx_size);
	sst_dsp_ipc_msg_tx(hsw->dsp, msg->header | SST_IPCX_BUSY);

	spin_unlock(&hsw->ipc_lock);
}

static inline void tx_msg_reply_complete(struct sst_hsw *hsw, struct ipc_message *msg)
{
	spin_lock(&hsw->ipc_lock);
	msg->complete = true;
	trace_ipc_reply("completed", msg->header);

	if (!msg->wait) {
		msg_put_empty(hsw, msg);
		spin_unlock(&hsw->ipc_lock);
	} else {
		spin_unlock(&hsw->ipc_lock);
		wake_up(&msg->waitq);
	}
}

static int tx_wait_done(struct sst_hsw *hsw, struct ipc_message *msg,
	void *rx_data)
{
	int ret;

	/* wait for DSP completion (in all cases atm inc pending) */
	ret = wait_event_timeout(msg->waitq, msg->complete,
		msecs_to_jiffies(IPC_TIMEOUT_MSECS));

	spin_lock(&hsw->ipc_lock);
	if (ret == 0) {
		ipc_shim_dbg(hsw, "message timeout");

		trace_ipc_error("error message timeout for", msg->header);
		ret = -ETIMEDOUT;
	} else {

		/* copy the data returned from DSP */
		if (msg->rx_size)
			memcpy(rx_data, msg->rx_data, msg->rx_size);
		ret = msg->errno;
	}

	msg_put_empty(hsw, msg);
	spin_unlock(&hsw->ipc_lock);
	return ret;
}

static int ipc_tx_message(struct sst_hsw *hsw, u32 header, void *tx_data,
	size_t tx_bytes, void *rx_data, size_t rx_bytes, int wait)
{
	struct ipc_message *msg;

	spin_lock(&hsw->ipc_lock);

	msg = msg_get_empty(hsw);
	if (msg == NULL) {
		spin_unlock(&hsw->ipc_lock);
		return -EBUSY;
	}

	if (tx_bytes)
		memcpy(msg->tx_data, tx_data, tx_bytes);

	msg->header = header;
	msg->tx_size = tx_bytes;
	msg->rx_size = rx_bytes;
	msg->wait = wait;
	msg->errno = 0;
	msg->pending = false;
	msg->complete = false;

	list_add_tail(&msg->list, &hsw->tx_list);
	spin_unlock(&hsw->ipc_lock);

	queue_kthread_work(&hsw->kworker, &hsw->kwork);

	if (wait)
		return tx_wait_done(hsw, msg, rx_data);
	else
		return 0;
}

static inline int ipc_tx_message_wait(struct sst_hsw *hsw, u32 header,
	void *tx_data, size_t tx_bytes, void *rx_data, size_t rx_bytes)
{
	return ipc_tx_message(hsw, header, tx_data, tx_bytes, rx_data,
		rx_bytes, 1);
}

static inline int ipc_tx_message_nowait(struct sst_hsw *hsw, u32 header,
	void *tx_data, size_t tx_bytes)
{
	return ipc_tx_message(hsw, header, tx_data, tx_bytes, NULL, 0, 0);
}

static void hsw_fw_ready(struct sst_hsw *hsw, u32 header)
{
	struct sst_hsw_ipc_fw_ready fw_ready;
	u32 offset;

	offset = (header & 0x1FFFFFFF) << 3;

	dev_dbg(hsw->dev, "ipc: DSP is ready 0x%8.8x offset %d\n", header, offset);

	/* copy data from the DSP FW ready offset */
	sst_dsp_dram_read(hsw->dsp, &fw_ready, offset, sizeof(fw_ready));

	sst_dsp_mailbox_init(hsw->dsp, fw_ready.inbox_offset,
		fw_ready.inbox_size, fw_ready.outbox_offset,
		fw_ready.outbox_size);

	hsw->boot_complete = true;
	wake_up(&hsw->boot_wait);

	dev_dbg(hsw->dev, " mailbox upstream 0x%x - size 0x%x\n",
		fw_ready.inbox_offset, fw_ready.inbox_size);
	dev_dbg(hsw->dev, " mailbox downstream 0x%x - size 0x%x\n",
		fw_ready.outbox_offset, fw_ready.outbox_size);
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
		trace_ipc_notification("DSP stream glitch",
			stream->reply.stream_hw_id);
		sst_dsp_inbox_read(hsw->dsp, glitch, sizeof(*glitch));

		dev_err(hsw->dev, "glitch %d pos 0x%x write pos 0x%x\n",
			glitch->glitch_type, glitch->present_pos,
			glitch->write_pos);
		break;

	case IPC_POSITION_CHANGED:
		trace_ipc_notification("DSP stream position changed for",
			stream->reply.stream_hw_id);
		sst_dsp_inbox_read(hsw->dsp, pos, sizeof(&pos));

		if (stream->notify_position)
			stream->notify_position(stream, stream->pdata);

		break;
	default:
		dev_err(hsw->dev, "unknown notification 0x%x\n", stream->header);
		break;
	}

	/* tell DSP that notification has been handled */
	sst_dsp_shim_update_bits(hsw->dsp, SST_IPCD,
		SST_IPCD_BUSY | SST_IPCD_DONE, SST_IPCD_DONE);

	/* unmask busy interrupt */
	sst_dsp_shim_update_bits(hsw->dsp, SST_IMRX, SST_IMRX_BUSY, 0);
}

static struct ipc_message *reply_find_msg(struct sst_hsw *hsw, u32 header)
{
	struct ipc_message *msg = NULL, *_msg;

	/* clear reply bits & status bits */
	header &= ~(IPC_STATUS_MASK | IPC_GLB_REPLY_MASK);

	spin_lock(&hsw->ipc_lock);

	if (list_empty(&hsw->rx_list)) {
		dev_err(hsw->dev, "ipc: rx list is empty but received 0x%x\n",
			header);
		goto out;
	}

	list_for_each_entry(_msg, &hsw->rx_list, list) {
		if (_msg->header == header) {
			msg = _msg;
			break;
		}
	}

out:
	spin_unlock(&hsw->ipc_lock);
	return msg;
}

static void reply_remove(struct sst_hsw *hsw, struct ipc_message *msg)
{
	spin_lock(&hsw->ipc_lock);
	list_del(&msg->list);
	spin_unlock(&hsw->ipc_lock);
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
	case IPC_STR_RESET:
		break;
	case IPC_STR_PAUSE:
		stream->running = false;
		trace_ipc_notification("stream paused", stream->reply.stream_hw_id);
		break;
	case IPC_STR_RESUME:
		stream->running = true;
		trace_ipc_notification("stream running", stream->reply.stream_hw_id);
		break;
	}
}

static int hsw_process_reply(struct sst_hsw *hsw, u32 header)
{
	struct ipc_message *msg;
	u32 reply = msg_get_global_reply(header);

	trace_ipc_reply("processing -->", header);
	msg = reply_find_msg(hsw, header);
	if (msg == NULL) {
		trace_ipc_error("can't find message for header", header);
		return 1;
	}

	/* first process the header */
	switch (reply) {
	case IPC_GLB_REPLY_PENDING:
		trace_ipc_pending_reply("received", header);
		msg->pending = true;
		spin_lock(&hsw->ipc_lock);
		hsw->pending = true;
		spin_unlock(&hsw->ipc_lock);
		return 1;
	case IPC_GLB_REPLY_SUCCESS:
		if (msg->pending) {
			trace_ipc_pending_reply("completed", header);
			sst_dsp_inbox_read(hsw->dsp, msg->rx_data, msg->rx_size);

			spin_lock(&hsw->ipc_lock);
			hsw->pending = false;
			spin_unlock(&hsw->ipc_lock);

		} else {
			/* copy data from the DSP */
			sst_dsp_outbox_read(hsw->dsp, msg->rx_data, msg->rx_size);
		}
		break;
	/* these will be rare - but useful for debug */
	case IPC_GLB_REPLY_UNKNOWN_MESSAGE_TYPE:
		trace_ipc_error("unknown message type", header);
		msg->errno = -EBADMSG;
		break;
	case IPC_GLB_REPLY_OUT_OF_RESOURCES:
		trace_ipc_error("out of resources", header);
		msg->errno = -ENOMEM;
		break;
	case IPC_GLB_REPLY_BUSY:
		trace_ipc_error("reply busy", header);
		msg->errno = -EBUSY;
		break;
	case IPC_GLB_REPLY_FAILURE:
		trace_ipc_error("reply failure", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_STAGE_UNINITIALIZED:
		trace_ipc_error("stage uninitialized", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_NOT_FOUND:
		trace_ipc_error("reply not found", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_SOURCE_NOT_STARTED:
		trace_ipc_error("source not started", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_INVALID_REQUEST:
		trace_ipc_error("invalid request", header);
		msg->errno = -EINVAL;
		break;
	case IPC_GLB_REPLY_ERROR_INVALID_PARAM:
		trace_ipc_error("invalid parameter", header);
		msg->errno = -EINVAL;
		break;
	default:
		trace_ipc_error("unknown reply", header);
		msg->errno = -EINVAL;
		break;
	}

	/* update any stream states */
	hsw_stream_update(hsw, msg);

	/* wake up and return the error if we have waiters on this message ? */
	reply_remove(hsw, msg);
	tx_msg_reply_complete(hsw, msg);

	return 1;
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
		dev_err(hsw->dev, "ipc: stage msg not implemented 0x%8.8x\n", header);
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

static int hsw_process_notification(struct sst_hsw *hsw)
{
	struct sst_dsp *sst = hsw->dsp;
	u32 type, header;
	int handled = 1;

	header = sst_dsp_shim_read_unlocked(sst, SST_IPCD);
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
	case IPC_GLB_SAVE_CONTEXT:
	case IPC_GLB_GET_DEVICE_FORMATS:
	case IPC_GLB_SET_DEVICE_FORMATS:
	case IPC_GLB_ENTER_DX_STATE:
	case IPC_GLB_GET_MIXER_STREAM_INFO:
	case IPC_GLB_DEBUG_LOG_MESSAGE:
	case IPC_GLB_MAX_IPC_MESSAGE_TYPE:
	case IPC_GLB_RESTORE_CONTEXT:
	case IPC_GLB_SHORT_REPLY:
		dev_err(hsw->dev, "ipc: error received message type %d header 0x%x not supported\n",
			type, header);
		break;
	case IPC_GLB_STREAM_MESSAGE:
		handled = hsw_stream_message(hsw, header);
		break;
	default:
		dev_err(hsw->dev, "ipc: error received unexpected type %d hdr 0x%8.8x\n",
			type, header);
		break;
	}

	return handled;
}

static irqreturn_t hsw_irq_thread(int irq, void *context)
{
	struct sst_dsp *sst = (struct sst_dsp *) context;
	struct sst_hsw *hsw = sst_dsp_get_thread_context(sst);
	u32 ipcx, ipcd;
	int handled;

	ipcx = sst_dsp_ipc_msg_rx(hsw->dsp);
	ipcd = sst_dsp_shim_read_unlocked(sst, SST_IPCD);

	/* reply message from DSP */
	if (ipcx & SST_IPCX_DONE) {

		/* Handle Immediate reply from DSP Core */
		handled = hsw_process_reply(hsw, ipcx);

		if (handled) {
			/* clear DONE bit - tell DSP we have completed the operation */
			sst_dsp_shim_update_bits(sst, SST_IPCX, SST_IPCX_DONE, 0);

			/* unmask Done interrupt */
			sst_dsp_shim_update_bits(sst, SST_IMRX, SST_IMRX_DONE, 0);
		}
	}

	/* new message from DSP */
	if (ipcd & SST_IPCD_BUSY) {

		/* Handle Notification and Delayed reply from DSP Core */
		handled = hsw_process_notification(hsw);

		/* clear BUSY bit and set DONE bit - tell DSP we can accept new messages */
		if (handled) {
			sst_dsp_shim_update_bits(sst, SST_IPCD,
				SST_IPCD_BUSY | SST_IPCD_DONE, SST_IPCD_DONE);

			/* unmask busy interrupt */
			sst_dsp_shim_update_bits(sst, SST_IMRX, SST_IMRX_BUSY, 0);
		}
	}

	/* continue to send any remaining messages... */
	queue_kthread_work(&hsw->kworker, &hsw->kwork);

	return IRQ_HANDLED;
}

int sst_hsw_fw_get_version(struct sst_hsw *hsw,
	struct sst_hsw_ipc_fw_version *version)
{
	int ret;

	ret = ipc_tx_message_wait(hsw, IPC_GLB_TYPE(IPC_GLB_GET_FW_VERSION),
		NULL, 0, version, sizeof(*version));
	if (ret < 0)
		dev_err(hsw->dev, "ipc: get version failed\n");

	return ret;
}

/* Mixer Controls */
int sst_hsw_stream_mute(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 channel)
{
	int ret;

	ret = sst_hsw_stream_get_volume(hsw, stream, stage_id, channel,
		&stream->mute_volume[channel]);
	if (ret < 0)
		return ret;

	ret = sst_hsw_stream_set_volume(hsw, stream, stage_id, channel, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "failed to unmute stream %d channel %d\n",
			stream->reply.stream_hw_id, channel);
		return ret;
	}

	stream->mute[channel] = 1;
	return 0;
}

int sst_hsw_stream_unmute(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 channel)

{
	int ret;

	stream->mute[channel] = 0;
	ret = sst_hsw_stream_set_volume(hsw, stream, stage_id, channel,
		stream->mute_volume[channel]);
	if (ret < 0) {
		dev_err(hsw->dev, "failed to unmute stream %d channel %d\n",
			stream->reply.stream_hw_id, channel);
		return ret;
	}

	return 0;
}

int sst_hsw_stream_get_volume(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 channel, u32 *volume)
{
	if (channel > 1)
		return -EINVAL;

	sst_dsp_dram_read(hsw->dsp, volume,
		stream->reply.volume_register_address[channel], sizeof(volume));

	return 0;
}

int sst_hsw_stream_set_volume_curve(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	 u64 curve_duration, enum sst_hsw_volume_curve curve)
{
	/* curve duration in steps of 100ns */
	stream->vol_req.curve_duration = curve_duration;
	stream->vol_req.curve_type = curve;

	return 0;
}

/* stream volume */
int sst_hsw_stream_set_volume(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 channel, u32 volume)
{
	struct sst_hsw_ipc_volume_req *req;
	u32 header;
	int ret;

	trace_ipc_request("set stream volume", stream->reply.stream_hw_id);

	if (channel > 1)
		return -EINVAL;

	if (stream->mute[channel]) {
		stream->mute_volume[channel] = volume;
		return 0;
	}

	header = IPC_GLB_TYPE(IPC_GLB_STREAM_MESSAGE) |
		IPC_STR_TYPE(IPC_STR_STAGE_MESSAGE);
	header |= (stream->reply.stream_hw_id << IPC_STR_ID_SHIFT);
	header |= (IPC_STG_SET_VOLUME << IPC_STG_TYPE_SHIFT);
	header |= (stage_id << IPC_STG_ID_SHIFT);

	req = &stream->vol_req;
	req->channel = channel;
	req->target_volume = volume;

	ret = ipc_tx_message_wait(hsw, header, req, sizeof(*req), NULL, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "ipc: set stream volume failed\n");
		return ret;
	}

	return 0;
}

int sst_hsw_mixer_mute(struct sst_hsw *hsw, u32 stage_id, u32 channel)
{
	int ret;

	ret = sst_hsw_mixer_get_volume(hsw, stage_id, channel,
		&hsw->mute_volume[channel]);
	if (ret < 0)
		return ret;

	ret = sst_hsw_mixer_set_volume(hsw, stage_id, channel, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "failed to unmute mixer channel %d\n",
			channel);
		return ret;
	}

	hsw->mute[channel] = 1;
	return 0;
}

int sst_hsw_mixer_unmute(struct sst_hsw *hsw, u32 stage_id, u32 channel)
{
	int ret;

	hsw->mute[channel] = 0;
	ret = sst_hsw_mixer_set_volume(hsw, stage_id, channel,
		hsw->mixer_info.volume_register_address[channel]);
	if (ret < 0) {
		dev_err(hsw->dev, "failed to unmute mixer channel %d\n",
			channel);
		return ret;
	}

	return 0;
}

int sst_hsw_mixer_get_volume(struct sst_hsw *hsw, u32 stage_id, u32 channel,
	u32 *volume)
{
	if (channel > 1)
		return -EINVAL;

	sst_dsp_dram_read(hsw->dsp, volume,
		hsw->mixer_info.volume_register_address[channel], sizeof(*volume));

	return 0;
}

int sst_hsw_mixer_set_volume_curve(struct sst_hsw *hsw,
	 u64 curve_duration, enum sst_hsw_volume_curve curve)
{
	/* curve duration in steps of 100ns */
	hsw->curve_duration = curve_duration;
	hsw->curve_type = curve;

	return 0;
}

/* global mixer volume */
int sst_hsw_mixer_set_volume(struct sst_hsw *hsw, u32 stage_id, u32 channel,
	u32 volume)
{
	struct sst_hsw_ipc_volume_req req;
	u32 header;
	int ret;

	trace_ipc_request("set mixer volume", volume);

	/* set both at same time */
	// TODO: mute not required as in db scale
	if (channel == 3) {
		if (hsw->mute[0] && hsw->mute[1]) {
			hsw->mute_volume[0] = hsw->mute_volume[1] = volume;
			return 0;
		} else if (hsw->mute[0])
			req.channel = 1;
		else if (hsw->mute[1])
			req.channel = 0;
		else
			req.channel = 0xffffffff;
	} else {
		if (hsw->mute[channel]) {
			hsw->mute_volume[channel] = volume;
			return 0;
		}
		req.channel = channel;
	}

	header = IPC_GLB_TYPE(IPC_GLB_STREAM_MESSAGE) |
		IPC_STR_TYPE(IPC_STR_STAGE_MESSAGE);
	header |= (hsw->mixer_info.mixer_hw_id << IPC_STR_ID_SHIFT);
	header |= (IPC_STG_SET_VOLUME << IPC_STG_TYPE_SHIFT);
	header |= (stage_id << IPC_STG_ID_SHIFT);

	req.curve_duration = hsw->curve_duration;
	req.curve_type = hsw->curve_type;
	req.target_volume = volume;

	ret = ipc_tx_message_wait(hsw, header, &req, sizeof(req), NULL, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "ipc: set mixer volume failed\n");
		return ret;
	}

	return 0;
}

/* Stream API - TODO: rework mutexes, get ID from struct HSW */
struct sst_hsw_stream *sst_hsw_stream_new(struct sst_hsw *hsw, int id,
	u32 (*notify_position)(struct sst_hsw_stream *stream, void *data),
	void *data)
{
	struct sst_hsw_stream *stream;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (stream == NULL)
		return NULL;

	list_add(&stream->node, &hsw->stream_list);
	stream->notify_position = notify_position;
	stream->pdata = data;
	stream->hsw = hsw;
	stream->host_id = id;

	/* work to process notification messages */
	INIT_WORK(&stream->notify_work, hsw_notification_work);

	return stream;
}

int sst_hsw_stream_free(struct sst_hsw *hsw, struct sst_hsw_stream *stream)
{
	u32 header;
	int ret = 0;

	/* dont free DSP streams that are not commited */
	if (!stream->commited)
		goto out;

	trace_ipc_request("stream free", stream->host_id);

	stream->free_req.stream_id = stream->reply.stream_hw_id;
	header = IPC_GLB_TYPE(IPC_GLB_FREE_STREAM);

	ret = ipc_tx_message_wait(hsw, header, &stream->free_req,
		sizeof(stream->free_req), NULL, 0);
	if (ret < 0) {
		dev_err(hsw->dev, "ipc: free stream %d failed\n",
			stream->free_req.stream_id);
		return -EAGAIN;
	}

	trace_hsw_stream_free_req(stream, &stream->free_req);

out:
	list_del(&stream->node);
	kfree(stream);

	return ret;
}

int sst_hsw_stream_set_bits(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum bitdepth bits)
{
	stream->request.format.bitdepth = bits;
	return 0;
}

int sst_hsw_stream_set_channels(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u8 channels)
{
	stream->request.format.ch_num = channels;
	return 0;
}

int sst_hsw_stream_set_rate(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum sample_frequency rate)
{
	stream->request.format.frequency = rate;
	return 0;
}

int sst_hsw_stream_set_map_config(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 map, enum sst_hsw_channel_config config)
{
	stream->request.format.map = map;
	stream->request.format.config = config;
	return 0;
}

int sst_hsw_stream_set_style(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum sst_hsw_interleaving style)
{
	stream->request.format.style = style;
	return 0;
}

int sst_hsw_stream_set_valid(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 bits)
{
	stream->request.format.valid_bit = bits;
	return 0;
}

/* Stream Configuration */
int sst_hsw_stream_format(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum sst_hsw_stream_path_id path_id,
	enum sst_hsw_stream_type stream_type,
	enum sst_hsw_stream_format format_id)
{
	stream->request.path_id = path_id;
	stream->request.stream_type = stream_type;
	stream->request.format_id = format_id;

	trace_hsw_stream_alloc_request(stream, &stream->request);

	return 0;
}

int sst_hsw_stream_buffer(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 ring_pt_address, u32 num_pages,
	u32 ring_size, u32 ring_offset, u32 ring_first_pfn)
{
	stream->request.ringinfo.ring_pt_address = ring_pt_address;
	stream->request.ringinfo.num_pages = num_pages;
	stream->request.ringinfo.ring_size = ring_size;
	stream->request.ringinfo.ring_offset = ring_offset;
	stream->request.ringinfo.ring_first_pfn = ring_first_pfn;

	trace_hsw_stream_buffer(stream);

	return 0;
}

int sst_hsw_stream_commit(struct sst_hsw *hsw, struct sst_hsw_stream *stream)
{
	struct sst_hsw_ipc_stream_alloc_req *str_req = &stream->request;
	struct sst_hsw_ipc_stream_alloc_reply *reply = &stream->reply;
	u32 header;
	int ret;

	trace_ipc_request("stream alloc", stream->host_id);

	header = IPC_GLB_TYPE(IPC_GLB_ALLOCATE_STREAM);

	ret = ipc_tx_message_wait(hsw, header, str_req, sizeof(*str_req),
		reply, sizeof(*reply));
	if (ret < 0) {
		dev_err(hsw->dev, "ipc: error stream commit failed\n");
		return ret;
	}

	stream->commited = 1;
	trace_hsw_stream_alloc_reply(stream);

	return 0;
}

/* Stream Information */
int sst_hsw_stream_get_hw_id(struct sst_hsw *hsw, struct sst_hsw_stream *stream)
{
	return stream->reply.stream_hw_id;
}

int sst_hsw_stream_get_mixer_id(struct sst_hsw *hsw, struct sst_hsw_stream *stream)
{
	return stream->reply.mixer_hw_id;
}

int sst_hsw_stream_get_read_reg(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	 u32 *reg)
{
	*reg = stream->reply.read_position_register_address;
	return 0;
}

int sst_hsw_stream_get_pointer_reg(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	 u32 *reg)
{
	*reg = stream->reply.presentation_position_register_address;
	return 0;
}

/* These info are from Mixer stream info reply */
int sst_hsw_stream_get_peak_reg(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	 u32 channel, u32 *reg)
{
	*reg = stream->reply.peak_meter_register_address[channel];
	return 0;
}

int sst_hsw_stream_get_vol_reg(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 channel, u32 *reg)
{
	*reg = stream->reply.volume_register_address[channel];
	return 0;
}

int sst_hsw_mixer_get_info(struct sst_hsw *hsw)
{
	struct sst_hsw_ipc_stream_info_reply *reply;
	u32 header;
	int ret;

	reply = &hsw->mixer_info;
	header = IPC_GLB_TYPE(IPC_GLB_GET_MIXER_STREAM_INFO);

	trace_ipc_request("get global mixer info", 0);

	ret = ipc_tx_message_wait(hsw, header, NULL, 0, reply, sizeof(*reply));
	if (ret < 0) {
		dev_err(hsw->dev, "ipc: get stream info failed\n");
		return ret;
	}

	trace_hsw_mixer_info_reply(reply);

	return 0;
}

/* Send stream command */
static int sst_hsw_stream_operations(struct sst_hsw *hsw, int type,
	int stream_id, int wait)
{
	u32 header;

	header = IPC_GLB_TYPE(IPC_GLB_STREAM_MESSAGE) | IPC_STR_TYPE(type);
	header |= (stream_id << IPC_STR_ID_SHIFT);

	if (wait)
		return ipc_tx_message_wait(hsw, header, NULL, 0, NULL, 0);
	else
		return ipc_tx_message_nowait(hsw, header, NULL, 0);
}

/* Stream ALSA trigger operations */
int sst_hsw_stream_pause(struct sst_hsw *hsw, struct sst_hsw_stream *stream, int wait)
{
	int ret;

	trace_ipc_request("stream pause", stream->reply.stream_hw_id);

	ret = sst_hsw_stream_operations(hsw, IPC_STR_PAUSE,
		stream->reply.stream_hw_id, wait);
	if (ret < 0)
		dev_err(hsw->dev, "ipc: error failed to pause stream %d\n",
			stream->reply.stream_hw_id);

	return ret;
}

int sst_hsw_stream_resume(struct sst_hsw *hsw, struct sst_hsw_stream *stream, int wait)
{
	int ret;

	trace_ipc_request("stream resume", stream->reply.stream_hw_id);

	ret = sst_hsw_stream_operations(hsw, IPC_STR_RESUME, stream->reply.stream_hw_id, wait);
	if (ret < 0)
		dev_err(hsw->dev, "ipc: error failed to resume stream %d\n",
			stream->reply.stream_hw_id);

	return ret;
}

int sst_hsw_stream_reset(struct sst_hsw *hsw, struct sst_hsw_stream *stream)
{
	int ret, tries = 10;

	/* dont reset streams that are not commited */
	if (!stream->commited)
		return 0;

	/* wait for pause to complete before we reset the stream */
	while (stream->running && tries--)
		msleep(1);
	if (!tries) {
		dev_err(hsw->dev, "ipc: can't reset stream %d still running\n",
			stream->reply.stream_hw_id);
		return -EINVAL;
	}

	trace_ipc_request("stream reset", stream->reply.stream_hw_id);

	ret = sst_hsw_stream_operations(hsw, IPC_STR_RESET, stream->reply.stream_hw_id, 1);
	if (ret < 0)
		dev_err(hsw->dev, "ipc: error failed to reset stream %d\n",
			stream->reply.stream_hw_id);
	return ret;
}

/* Stream pointer positions */
int sst_hsw_get_dsp_position(struct sst_hsw *hsw, struct sst_hsw_stream *stream)
{
	return stream->rpos.position;
}

int sst_hsw_stream_set_write_position(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 position)
{
	u32 header;
	int ret;

	trace_stream_write_position(stream->reply.stream_hw_id, position);

	header = IPC_GLB_TYPE(IPC_GLB_STREAM_MESSAGE) |
		IPC_STR_TYPE(IPC_STR_STAGE_MESSAGE);
	header |= (stream->reply.stream_hw_id << IPC_STR_ID_SHIFT);
	header |= (IPC_STG_SET_WRITE_POSITION << IPC_STG_TYPE_SHIFT);
	header |= (stage_id << IPC_STG_ID_SHIFT);
	stream->wpos.position = position;

	ret = ipc_tx_message_nowait(hsw, header, &stream->wpos, sizeof(stream->wpos));
	if (ret < 0)
		dev_err(hsw->dev, "ipc: error stream %d set position %d failed\n",
			stream->reply.stream_hw_id, position);

	return ret;
}

/* HW port config */
int sst_hsw_device_set_config(struct sst_hsw *hsw,
	enum sst_hsw_device_id dev, enum sst_hsw_device_mclk mclk,
	enum sst_hsw_device_mode mode, u32 clock_divider)
{
	struct sst_hsw_ipc_device_config_req config;
	u32 header;
	int ret;

	trace_ipc_request("set device config", dev);

	config.ssp_interface = dev;
	config.clock_frequency = mclk;
	config.mode = mode;
	config.clock_divider = clock_divider;

	trace_hsw_device_config_req(&config);

	header = IPC_GLB_TYPE(IPC_GLB_SET_DEVICE_FORMATS);

	ret = ipc_tx_message_wait(hsw, header, &config, sizeof(config), NULL, 0);
	if (ret < 0)
		dev_err(hsw->dev, "ipc: error set device formats failed\n");

	return ret;
}
EXPORT_SYMBOL(sst_hsw_device_set_config);

void sst_hsw_dx_state_dump(struct sst_hsw *hsw)
{
	u32 item, size, offset, source;
	int ret;

	trace_ipc_request("PM state dump. Items #", SST_HSW_MAX_DX_REGIONS);

	for (item = 0; item < SST_HSW_MAX_DX_REGIONS; item++) {
		ret = sst_hsw_dx_get_state(hsw, item, &offset, &size, &source);
		if (ret < 0) {
			dev_err(hsw->dev, "ipc: failed to get dx state item %d\n",
				item);
			return;
		}
		dev_dbg(hsw->dev, " Item[%d] offset[%x] - size[%x] - source[%x]\n",
				item, offset, size, source);
	}
}

/* DX Config */
int sst_hsw_dx_set_state(struct sst_hsw *hsw,
	enum sst_hsw_dx_state state, struct sst_hsw_ipc_dx_reply *dx)
{
	u32 header, state_;
	int ret;

	header = IPC_GLB_TYPE(IPC_GLB_ENTER_DX_STATE);
	state_ = state;

	trace_ipc_request("PM enter Dx state", state);

	ret = ipc_tx_message_wait(hsw, header, &state_, sizeof(state_),
		dx, sizeof(dx));
	if (ret < 0) {
		dev_err(hsw->dev, "ipc: error set dx state %d failed\n", state);
		return ret;
	}

	dev_dbg(hsw->dev, "ipc: got %d entry numbers for state %d\n",
		dx->entries_no, state);

	memcpy(&hsw->dx, dx, sizeof(*dx));
	return 0;
}

/* Used to save state into hsw->dx_reply */
int sst_hsw_dx_get_state(struct sst_hsw *hsw, u32 item,
	u32 *offset, u32 *size, u32 *source)
{
	struct sst_hsw_ipc_dx_memory_item *dx_mem;
	struct sst_hsw_ipc_dx_reply *dx_reply;
	int entry_no;

	dx_reply = &hsw->dx;
	entry_no = dx_reply->entries_no;

	trace_ipc_request("PM get Dx state", entry_no);

	if (item >= entry_no)
		return -EINVAL;

	dx_mem = &dx_reply->mem_info[item];
	*offset = dx_mem->offset;
	*size = dx_mem->size;
	*source = dx_mem->source;

	return 0;
}

/* debug control - sysFS */
int sst_hsw_dbg_enable(struct sst_hsw *hsw, struct sst_hsw_stream *stream, u32 log_id)
{
	return 0;
}

int sst_hsw_dbg_disable(struct sst_hsw *hsw, struct sst_hsw_stream *stream, u32 log_id)
{
	return 0;
}

int sst_hsw_dbg_log_dump(struct sst_hsw *hsw, struct sst_hsw_stream *stream, u32 log_id,
	struct sst_hsw_ipc_debug_log_reply *reply)
{
	return 0;
}

static int msg_empty_list_init(struct sst_hsw *hsw)
{
	struct ipc_message *msg;
	int i;

	msg = kzalloc(sizeof(*msg) * IPC_EMPTY_LIST_SIZE, GFP_KERNEL);
	if (msg == NULL)
		return -ENOMEM;

	for (i = 0; i < IPC_EMPTY_LIST_SIZE; i++) {
		init_waitqueue_head(&msg[i].waitq);
		list_add(&msg[i].list, &hsw->empty_list);
	}

	return 0;
}

static struct sst_dsp_device hsw_dev ={
	.id = SST_DEV_ID_HSWULT,
	.thread = hsw_irq_thread,
};

struct sst_hsw *sst_hsw_dsp_init(struct device *dev,
	struct sst_pdata *pdata)
{
	struct sst_hsw_ipc_fw_version version;
	struct sst_hsw *hsw;
	int err;

	dev_dbg(dev, "initialising Hawell DSP IPC\n");

	hsw = kzalloc(sizeof(*hsw), GFP_KERNEL);
	if (hsw == NULL)
		return NULL;

	hsw->dev = dev;
	INIT_LIST_HEAD(&hsw->stream_list);
	INIT_LIST_HEAD(&hsw->tx_list);
	INIT_LIST_HEAD(&hsw->rx_list);
	INIT_LIST_HEAD(&hsw->empty_list);
	spin_lock_init(&hsw->ipc_lock);
	init_waitqueue_head(&hsw->boot_wait);
	init_waitqueue_head(&hsw->wait_txq);

	err = msg_empty_list_init(hsw);
	if (err < 0)
		goto list_err;

	/* start the IPC message thread */
	init_kthread_worker(&hsw->kworker);
	hsw->tx_thread = kthread_run(kthread_worker_fn,
					   &hsw->kworker,
					   dev_name(hsw->dev));
	if (IS_ERR(hsw->tx_thread)) {
		dev_err(hsw->dev, "error failed to create message TX task\n");
		goto list_err;
	}
	init_kthread_work(&hsw->kwork, ipc_tx_msgs);

	hsw_dev.thread_context = hsw;

	/* init SST shim */
	hsw->dsp = sst_dsp_new(dev, &hsw_dev, pdata);
	if (hsw->dsp == NULL)
		goto list_err;

	/* load DSP FW */
	err = sst_fw_load(hsw->dsp, "IntcADSP.bin", 0);
	if (err < 0) {
		dev_err(hsw->dev, "error: failed to load firmware\n");
		goto fw_err;
	}

	/* wait for DSP boot completion */
	sst_dsp_boot(hsw->dsp);
	err = wait_event_timeout(hsw->boot_wait, hsw->boot_complete,
		msecs_to_jiffies(IPC_BOOT_MSECS));
	if (err == 0) {
		dev_err(hsw->dev, "ipc: error DSP boot timeout\n");
		goto boot_err;
	}

	/* get the FW version */
	sst_hsw_fw_get_version(hsw, &version);
	dev_info(hsw->dev, "FW loaded: type %d - version: %d.%d build %d\n",
		version.type, version.major, version.minor, version.build);

	/* get the globalmixer */
	err = sst_hsw_mixer_get_info(hsw);
	if (err < 0) {
		dev_err(hsw->dev, "failed to get stream info\n");
	}

	/* dump DX state at boot */
	sst_hsw_dx_state_dump(hsw);

	/* create debugFS entries for loging */

	return hsw;

boot_err:
	sst_dsp_reset(hsw->dsp);
	sst_fw_free(hsw->dsp);
fw_err:
	sst_dsp_free(hsw->dsp);
list_err:
	kfree(hsw);
	return NULL;
}
EXPORT_SYMBOL(sst_hsw_dsp_init);

void sst_hsw_dsp_free(struct sst_hsw *hsw)
{
	sst_dsp_reset(hsw->dsp);
	sst_fw_free(hsw->dsp);
	sst_dsp_free(hsw->dsp);
	kfree(hsw);
}
EXPORT_SYMBOL(sst_hsw_dsp_free);
