/*
 * Intel Smart Sound Technology (SST) Core
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

#ifndef __SOUND_SOC_SOF_H
#define __SOUND_SOC_SOF_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/device.h>

#define SOF_NAME_SIZE			16
#define SOF_IRAM			1
#define SOF_DRAM			2
#define SOF_REGS			3

struct snd_soc_sof_fw_block {
	__le32 ssize;		/* size of this structure */
	__le32 type;		/* destination type e.g IRA, DRAM etc */
	__le32 size;		/* block size */
	__le32 offset;		/* offset from DSP base */
} __attribute__((packed));

struct snd_soc_sof_fw_hdr {
	__le32 magic;
	__le32 ssize;		/* size of this structure */
	char name[SOF_NAME_SIZE]; /* FW name */
	__le32 file_size;	/* size of fw minus this header */
	__le32 abi;		/* ABI version */
	__le32 modules;		/* # of modules */
} __attribute__((packed));

struct snd_soc_sof_fw_module {
	__le32 ssize;		/* size of this structure */
	char name[SOF_NAME_SIZE]; /* module name */
	__le32 size;		/* size of module */
	__le32 blocks;		/* # of blocks */
} __attribute__((packed));


struct snd_sof_dev {
	struct device *dev;
	struct mutex mutex;

};

/*
 * Stream APIs.
 */
struct snd_sof_stream *snd_sof_stream_new(struct snd_sof_dev *sof_dev, 
	struct snd_pcm_substream *substream, int stream_id,
	u32 (*notify_position)(struct snd_sof_stream *stream, void *data),
	void *data);

int snd_sof_stream_free(struct snd_sof_stream *stream, 
	struct snd_pcm_substream *substream);

int snd_sof_stream_hw_params(struct snd_sof_stream *stream,
	struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params);

int snd_sof_stream_hw_free(struct snd_sof_stream *stream,
	struct snd_pcm_substream *substream);

int snd_sof_stream_prepare(struct snd_sof_stream *stream,
	struct snd_pcm_substream *substream);

int snd_sof_stream_trigger(struct snd_sof_stream *stream,
	struct snd_pcm_substream *substream, int cmd);

#endif
