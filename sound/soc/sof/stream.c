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
#include <uapi/sound/sof-ipc.h>
#include "sof.h"

static int stream_hw_params_ipc(struct snd_sof_stream *stream)
{
	struct snd_sof_dev_ipc_stream_alloc_req *str_req = &stream->request;
	struct snd_sof_dev_ipc_stream_alloc_reply *reply = &stream->reply;
	u32 header;
	int ret;

	header = IPC_GLB_TYPE(IPC_GLB_ALLOCATE_STREAM);

	ret = sst_ipc_tx_message_wait(&sof_dev->ipc, header, str_req,
		sizeof(*str_req), reply, sizeof(*reply));
	if (ret < 0) {
		dev_err(sof_dev->dev, "error: stream commit failed\n");
		return ret;
	}

	return 0;
}

/*
 *
 */
struct snd_sof_stream *snd_sof_stream_new(struct snd_sof_dev *sof_dev,
	struct snd_pcm_substream *substream, int stream_id,
	u32 (*notify_position)(struct snd_sof_stream *stream, void *data),
	void *data)
{
	struct snd_sof_stream *stream;
	struct sst_dsp *sst = sof_dev->dsp;
	unsigned long flags;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (stream == NULL)
		return NULL;

	spin_lock_irqsave(&sst->spinlock, flags);
	stream->reply.stream_hw_id = INVALID_STREAM_HW_ID;
	list_add(&stream->node, &sof_dev->stream_list);
	stream->notify_position = notify_position;
	stream->pdata = data;
	stream->sof_dev = sof_dev;
	stream->host_id = id;

	/* work to process notification messages */
	INIT_WORK(&stream->notify_work, sof_dev_notification_work);
	spin_unlock_irqrestore(&sst->spinlock, flags);

	return stream;
}

int snd_sof_stream_free(struct snd_sof_stream *stream,
	struct snd_pcm_substream *substream)
{
	u32 header;
	int ret = 0;
	struct sst_dsp *sst = sof_dev->dsp;
	unsigned long flags;

	/* dont free DSP streams that are not commited */
	if (!stream->commited)
		goto out;

	trace_ipc_request("stream free", stream->host_id);

	stream->free_req.stream_id = stream->reply.stream_hw_id;
	header = IPC_GLB_TYPE(IPC_GLB_FREE_STREAM);

	ret = sst_ipc_tx_message_wait(&sof_dev->ipc, header, &stream->free_req,
		sizeof(stream->free_req), NULL, 0);
	if (ret < 0) {
		dev_err(sof_dev->dev, "error: free stream %d failed\n",
			stream->free_req.stream_id);
		return -EAGAIN;
	}

	trace_sof_dev_stream_free_req(stream, &stream->free_req);

out:
	cancel_work_sync(&stream->notify_work);
	spin_lock_irqsave(&sst->spinlock, flags);
	list_del(&stream->node);
	kfree(stream);
	spin_unlock_irqrestore(&sst->spinlock, flags);

	return ret;
}

int snd_sof_stream_hw_params(struct snd_sof_stream *stream,
	struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{

}

int snd_sof_stream_hw_free(struct snd_sof_stream *stream,
	struct snd_pcm_substream *substream)
{

}

int snd_sof_stream_prepare(struct snd_sof_stream *stream,
	struct snd_pcm_substream *substream)
{

}

int snd_sof_stream_trigger(struct snd_sof_stream *stream,
	struct snd_pcm_substream *substream, int cmd)
{

}

snd_pcm_uframes_t snd_sof_stream_get_old_position(struct snd_sof_dev *sof_dev,
	struct snd_sof_stream *stream)
{
	return stream->old_position;
}

void snd_sof_stream_set_old_position(struct snd_sof_dev *sof_dev,
	struct snd_sof_stream *stream, snd_pcm_uframes_t val)
{
	stream->old_position = val;
}

bool snd_sof_stream_get_silence_start(struct snd_sof_dev *sof_dev,
	struct snd_sof_stream *stream)
{
	return stream->play_silence;
}

void snd_sof_stream_set_silence_start(struct snd_sof_dev *sof_dev,
	struct snd_sof_stream *stream, bool val)
{
	stream->play_silence = val;
}

/* Send stream command */
static int snd_sof_stream_operations(struct snd_sof_dev *sof_dev, int type,
	int stream_id, int wait)
{
	u32 header;

	header = IPC_GLB_TYPE(IPC_GLB_STREAM_MESSAGE) | IPC_STR_TYPE(type);
	header |= (stream_id << IPC_STR_ID_SHIFT);

	if (wait)
		return sst_ipc_tx_message_wait(&sof_dev->ipc, header,
			NULL, 0, NULL, 0);
	else
		return sst_ipc_tx_message_nowait(&sof_dev->ipc, header, NULL, 0);
}

/* Stream ALSA trigger operations */
int snd_sof_stream_pause(struct snd_sof_dev *sof_dev, struct snd_sof_stream *stream,
	int wait)
{
	int ret;

	if (!stream) {
		dev_warn(sof_dev->dev, "warning: stream is NULL, no stream to pause, ignore it.\n");
		return 0;
	}

	trace_ipc_request("stream pause", stream->reply.stream_hw_id);

	ret = snd_sof_stream_operations(sof_dev, IPC_STR_PAUSE,
		stream->reply.stream_hw_id, wait);
	if (ret < 0)
		dev_err(sof_dev->dev, "error: failed to pause stream %d\n",
			stream->reply.stream_hw_id);

	return ret;
}

int snd_sof_stream_resume(struct snd_sof_dev *sof_dev, struct snd_sof_stream *stream,
	int wait)
{
	int ret;

	if (!stream) {
		dev_warn(sof_dev->dev, "warning: stream is NULL, no stream to resume, ignore it.\n");
		return 0;
	}

	trace_ipc_request("stream resume", stream->reply.stream_hw_id);

	ret = snd_sof_stream_operations(sof_dev, IPC_STR_RESUME,
		stream->reply.stream_hw_id, wait);
	if (ret < 0)
		dev_err(sof_dev->dev, "error: failed to resume stream %d\n",
			stream->reply.stream_hw_id);

	return ret;
}


/* Stream pointer positions */
u32 snd_sof_dev_get_dsp_position(struct snd_sof_dev *sof_dev,
	struct snd_sof_stream *stream)
{
	u32 rpos;

	sst_dsp_read(sof_dev->dsp, &rpos,
		stream->reply.read_position_register_address, sizeof(rpos));

	return rpos;
}

/* Stream presentation (monotonic) positions */
u64 snd_sof_dev_get_dsp_presentation_position(struct snd_sof_dev *sof_dev,
	struct snd_sof_stream *stream)
{
	u64 ppos;

	sst_dsp_read(sof_dev->dsp, &ppos,
		stream->reply.presentation_position_register_address,
		sizeof(ppos));

	return ppos;
}
