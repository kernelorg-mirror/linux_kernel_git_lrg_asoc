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
 *
 * Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
 *         Keyon Jie <yang.jie@linux.intel.com>
 */

#ifndef __INCLUDE_UAPI_SOF_IPC_H__
#define __INCLUDE_UAPI_SOF_IPC_H__

/*
 * IPC messages have a prefixed 32 bit identifier made up as follows :-
 *
 * 	0xGCCCNNNN where
 * G is global cmd type (4 bits)
 * C is command type (12 bits)
 * I is the ID number (16 bits) - monotonic and overflows
 */

/* Global Message - Generic */
#define SOF_GLB_TYPE_SHIFT			28
#define SOF_GLB_TYPE_MASK			(0xf << SOF_GLB_TYPE_SHIFT)
#define SOF_GLB_TYPE(x)				(x << SOF_GLB_TYPE_SHIFT)

/* Global Message - Reply */
#define SOF_GLB_REPLY_SHIFT			0
#define SOF_GLB_REPLY_MASK			(0x1f << SOF_GLB_REPLY_SHIFT)
#define SOF_GLB_REPLY_TYPE(x)			(x << SOF_GLB_REPLY_TYPE_SHIFT)

/* Command Message - Generic */
#define SOF_CMD_TYPE_SHIFT			16
#define SOF_CMD_TYPE_MASK			(0xfff << SOF_CMD_TYPE_SHIFT)
#define SOF_CMD_TYPE(x)				(x << SOF_CMD_TYPE_SHIFT)

/* Firmware Ready Message */
#define SOF_FW_READY				(0x1 << 29)
#define IPC_INTL_STATUS_MASK			(0x3 << 30)


/* Global Message Types */
#define SOF_IPC_GLB_NONE			SOF_GLB_TYPE(0x0)
#define SOF_IPC_GLB_COMPOUND			SOF_GLB_TYPE(0x1)
#define SOF_IPC_GLB_TPLG_MSG			SOF_GLB_TYPE(0x2)
#define SOF_IPC_GLB_PM_MSG			SOF_GLB_TYPE(0x3)
#define SOF_IPC_GLB_COMP_MSG			SOF_GLB_TYPE(0x4)
#define SOF_IPC_GLB_STREAM_MSG			SOF_GLB_TYPE(0x5)
#define SOF_IPC_GLB_DAI_MSG			SOF_GLB_TYPE(0x6)
#define SOF_IPC_GLB_HOST_MSG			SOF_GLB_TYPE(0x7)

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

/* Host Command Message Types */
#define SOF_IPC_HOST_POSN			SOF_CMD_TYPE(0x000)

/* get message id */
#define SOF_IPC_MESSAGE_ID(x)			(x & 0xffff)


/*
 * Command Header - Header for all IPC. Identifies IPC message.
 * The size can be greater than the structure size and that means there is
 * extended bespoke data beyond the end of the structure including variable
 * arrays.
 */

struct sof_ipc_hdr {
	uint32_t cmd;			/* SOF_IPC_GLB_ + cmd */
	uint32_t size;			/* size of structure */
}  __attribute__((packed));

/*
 * Compound commands - SOF_IPC_GLB_COMPOUND.
 *
 * Compound commands are sent to the DSP as a single IPC operation. The
 * commands are split into blocks and each block has a header. This header
 * identifies the command type and the number of commands before the next
 * header.
 */

struct sof_ipc_compound_hdr {
	struct sof_ipc_hdr hdr;
	uint32_t count;			/* count of 0 means end of compound sequence */
}  __attribute__((packed));


/*
 * Firmware boot and version
 */

/* FW version - SOF_IPC_GLB_VERSION */
struct sof_ipc_fw_version {
	uint16_t major;
	uint16_t minor;
	uint16_t build;
	uint8_t date[11];
	uint8_t time[8];
	uint8_t tag[5];
} __attribute__((packed));


/* FW ready Message - sent by firmware when boot has completed */
struct sof_ipc_fw_ready {
	struct sof_ipc_hdr hdr;
	uint32_t inbox_offset;
	uint32_t outbox_offset;
	uint32_t inbox_size;
	uint32_t outbox_size;
	struct sof_ipc_fw_version version;
	/* TODO: capabilities and features */
} __attribute__((packed));

/*
 * DAI Configuration.
 *
 * Each different DAI type will have it's own structure and IPC cmd.
 */

#define SOF_DAI_FMT_I2S			1 /* I2S mode */
#define SOF_DAI_FMT_RIGHT_J		2 /* Right Justified mode */
#define SOF_DAI_FMT_LEFT_J		3 /* Left Justified mode */
#define SOF_DAI_FMT_DSP_A		4 /* L data MSB after FRM LRC */
#define SOF_DAI_FMT_DSP_B		5 /* L data MSB during FRM LRC */
#define SOF_DAI_FMT_PDM			6 /* Pulse density modulation */

#define SOF_DAI_FMT_CONT		(1 << 4) /* continuous clock */
#define SOF_DAI_FMT_GATED		(0 << 4) /* clock is gated */

#define SOF_DAI_FMT_NB_NF		(0 << 8) /* normal bit clock + frame */
#define SOF_DAI_FMT_NB_IF		(2 << 8) /* normal BCLK + inv FRM */
#define SOF_DAI_FMT_IB_NF		(3 << 8) /* invert BCLK + nor FRM */
#define SOF_DAI_FMT_IB_IF		(4 << 8) /* invert BCLK + FRM */

#define SOF_DAI_FMT_CBM_CFM		(0 << 12) /* codec clk & FRM master */
#define SOF_DAI_FMT_CBS_CFM		(2 << 12) /* codec clk slave & FRM master */
#define SOF_DAI_FMT_CBM_CFS		(3 << 12) /* codec clk master & frame slave */
#define SOF_DAI_FMT_CBS_CFS		(4 << 12) /* codec clk & FRM slave */

#define SOF_DAI_FMT_FORMAT_MASK		0x000f
#define SOF_DAI_FMT_CLOCK_MASK		0x00f0
#define SOF_DAI_FMT_INV_MASK		0x0f00
#define SOF_DAI_FMT_MASTER_MASK		0xf000

/* SSP Configuration Request - SOF_IPC_DAI_SSP_CONFIG */
struct sof_ipc_dai_ssp_params {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
	uint32_t mclk;
	uint32_t bclk;
	uint16_t ssp_id;
	uint16_t mode;
	uint16_t num_slots;
	uint16_t frame_width;
	uint16_t clk_id;
	uint16_t format;	/* SOF_DAI_FMT_ */
} __attribute__((packed));

/* HDA Configuration Request - SOF_IPC_DAI_HDA_CONFIG */
struct sof_ipc_dai_hda_params {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
	uint32_t mclk;
	/* TODO */
} __attribute__((packed));

/* DMIC Configuration Request - SOF_IPC_DAI_DMIC_CONFIG */
struct sof_ipc_dai_dmic_params {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
	uint32_t mclk;
	/* TODO */
} __attribute__((packed));

/*
 * Stream configuration.
 */

/* channel positions - uses same values as ALSA */
enum sof_ipc_chmap {
	SOF_CHMAP_UNKNOWN = 0,
	SOF_CHMAP_NA,		/* N/A, silent */
	SOF_CHMAP_MONO,		/* mono stream */
	SOF_CHMAP_FL,		/* front left */
	SOF_CHMAP_FR,		/* front right */
	SOF_CHMAP_RL,		/* rear left */
	SOF_CHMAP_RR,		/* rear right */
	SOF_CHMAP_FC,		/* front centre */
	SOF_CHMAP_LFE,		/* LFE */
	SOF_CHMAP_SL,		/* side left */
	SOF_CHMAP_SR,		/* side right */
	SOF_CHMAP_RC,		/* rear centre */
	SOF_CHMAP_FLC,		/* front left centre */
	SOF_CHMAP_FRC,		/* front right centre */
	SOF_CHMAP_RLC,		/* rear left centre */
	SOF_CHMAP_RRC,		/* rear right centre */
	SOF_CHMAP_FLW,		/* front left wide */
	SOF_CHMAP_FRW,		/* front right wide */
	SOF_CHMAP_FLH,		/* front left high */
	SOF_CHMAP_FCH,		/* front centre high */
	SOF_CHMAP_FRH,		/* front right high */
	SOF_CHMAP_TC,		/* top centre */
	SOF_CHMAP_TFL,		/* top front left */
	SOF_CHMAP_TFR,		/* top front right */
	SOF_CHMAP_TFC,		/* top front centre */
	SOF_CHMAP_TRL,		/* top rear left */
	SOF_CHMAP_TRR,		/* top rear right */
	SOF_CHMAP_TRC,		/* top rear centre */
	SOF_CHMAP_TFLC,		/* top front left centre */
	SOF_CHMAP_TFRC,		/* top front right centre */
	SOF_CHMAP_TSL,		/* top side left */
	SOF_CHMAP_TSR,		/* top side right */
	SOF_CHMAP_LLFE,		/* left LFE */
	SOF_CHMAP_RLFE,		/* right LFE */
	SOF_CHMAP_BC,		/* bottom centre */
	SOF_CHMAP_BLC,		/* bottom left centre */
	SOF_CHMAP_BRC,		/* bottom right centre */
	SOF_CHMAP_LAST = SOF_CHMAP_BRC,
};

/* common sample rates for use in masks */
#define SOF_RATE_8000	(1 << 0)
#define SOF_RATE_11250	(1 << 1)
#define SOF_RATE_16000	(1 << 2)
#define SOF_RATE_22500	(1 << 3)
#define SOF_RATE_24000	(1 << 4)
#define SOF_RATE_32000	(1 << 5)
#define SOF_RATE_40000	(1 << 6)
#define SOF_RATE_44100	(1 << 7)
#define SOF_RATE_48000	(1 << 8)
#define SOF_RATE_88200	(1 << 9)
#define SOF_RATE_96000	(1 << 10)
#define SOF_RATE_176400	(1 << 11)
#define SOF_RATE_192000	(1 << 12)


/* stream PCM frame format */
enum sof_ipc_frame {
	SOF_IPC_FRAME_S16_LE = 0,
	SOF_IPC_FRAME_S24_4LE,
	SOF_IPC_FRAME_S32_LE,
	/* other formats here */
};

/* stream buffer format */
enum sof_ipc_buffer_format {
	SOF_IPC_BUFFER_INTERLEAVED,
	SOF_IPC_BUFFER_NONINTERLEAVED,
	/* other formats here */
};

/* stream direction */
enum sof_ipc_stream_direction {
	SOF_IPC_STREAM_PLAYBACK = 0,
	SOF_IPC_STREAM_CAPTURE,
};

/* stream ring info */
struct sof_ipc_ring_buffer {
	uint32_t phy_addr;
	uint32_t pages;
	uint32_t size;
	uint32_t offset;
} __attribute__((packed));


/* PCM params info - SOF_IPC_STREAM_PCM_PARAMS */
struct sof_ipc_pcm_params {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
	struct sof_ipc_ring_buffer buffer;
	enum sof_ipc_stream_direction direction;
	enum sof_ipc_frame frame_fmt;
	enum sof_ipc_buffer_format buffer_fmt;
	uint32_t rate;
	uint32_t channels;
	uint32_t frame_size;
	uint32_t period_bytes;	/* 0 means variable */
	uint32_t period_count;	/* 0 means variable */
	enum sof_ipc_chmap channel_map[];
}  __attribute__((packed));


/* compressed vorbis params - SOF_IPC_STREAM_VORBIS_PARAMS */
struct sof_ipc_vorbis_params {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
	struct sof_ipc_ring_buffer buffer;
	enum sof_ipc_stream_direction direction;
	enum sof_ipc_frame frame_fmt;
	enum sof_ipc_buffer_format buffer_fmt;
	/* TODO */
}  __attribute__((packed));


/* free stream - SOF_IPC_STREAM_PCM_PARAMS */
struct sof_ipc_stream {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
} __attribute__((packed));

struct sof_ipc_stream_posn {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
	uint32_t host_posn;
	uint32_t dai_posn;
	uint64_t timestamp;
}  __attribute__((packed));

/*
 * Component Mixers and Controls
 */

struct sof_ipc_ctrl_chan {
	enum sof_ipc_chmap channel;
	uint32_t value;
} __attribute__((packed));

struct sof_ipc_ctrl_values {
	struct sof_ipc_hdr hdr;
	uint32_t comp_id;
	uint32_t num_values;
	struct sof_ipc_ctrl_chan values[];
} __attribute__((packed));


/*
 * Component Buffers
 */

struct sof_ipc_period {
	uint32_t size;		/* period size in bytes */
	uint32_t number;	/* number of periods */
	uint32_t preload_count;	/* how many periods to preload */
} __attribute__((packed));

/* create new component buffer - SOF_IPC_TPLG_BUFFER_NEW */
struct sof_ipc_buffer {
	struct sof_ipc_hdr hdr;
	uint32_t buffer_id;
	uint32_t size;		/* buffer size in bytes */
	struct sof_ipc_period sink_period;
	struct sof_ipc_period source_period;
} __attribute__((packed));

/*
 * Component
 */

/* types of component */
enum sof_comp_type {
	SOF_COMP_NONE = 0,
	SOF_COMP_HOST,
	SOF_COMP_DAI,
	SOF_COMP_SG_HOST,	/* scatter gather variant */
	SOF_COMP_SG_DAI,	/* scatter gather variant */
	SOF_COMP_VOLUME,
	SOF_COMP_MIXER,
	SOF_COMP_MUX,
	SOF_COMP_SRC,
	SOF_COMP_SPLITTER,
	SOF_COMP_TONE,
	SOF_COMP_SWITCH,
};

/* types of DAI */
enum sof_ipc_dai_type {
	SOF_DAI_INTEL_SSP = 0,
	SOF_DAI_INTEL_DMIC,
	SOF_DAI_INTEL_HDA,
};

#define SOF_IPC_MAX_COMP_SIZE	256

/* create new generic component - SOF_IPC_TPLG_COMP_NEW */
struct sof_ipc_comp {
	uint32_t id;
	uint32_t size;
	enum sof_comp_type type;
} __attribute__((packed));

/* generic PCM component data */
struct sof_ipc_pcm_comp {
	uint32_t format;	/* data format */
	uint32_t frames;	/* number of frames to process */
	uint32_t channels;	/* number of channels */
	enum sof_ipc_chmap chmap[0];	/* channel map */
} __attribute__((packed));

/* generic host component */
struct sof_ipc_comp_host {
	struct sof_ipc_hdr hdr;
	struct sof_ipc_comp comp;
	struct sof_ipc_pcm_comp pcm;
	enum sof_ipc_stream_direction direction;
	uint32_t no_irq;	/* dont send periodic IRQ to host/DSP */
	uint32_t dmac_id;
	uint32_t dmac_chan;
	uint32_t dmac_config; /* DMA engine specific */
}  __attribute__((packed));

/* generic DAI component */
struct sof_ipc_comp_dai {
	struct sof_ipc_hdr hdr;
	struct sof_ipc_comp comp;
	struct sof_ipc_pcm_comp pcm;
	enum sof_ipc_stream_direction direction;
	uint32_t index;
	enum sof_ipc_dai_type type;
	uint32_t dmac_id;
	uint32_t dmac_chan;
	uint32_t dmac_config; /* DMA engine specific */
}  __attribute__((packed));

/* generic mixer component */
struct sof_ipc_comp_mixer {
	struct sof_ipc_hdr hdr;
	struct sof_ipc_comp comp;
	struct sof_ipc_pcm_comp pcm;
}  __attribute__((packed));

/* volume ramping types */
enum sof_volume_ramp {
	SOF_VOLUME_LINEAR	= 0,
	SOF_VOLUME_LOG,
	SOF_VOLUME_LINEAR_ZC,
	SOF_VOLUME_LOG_ZC,
};

/* generic volume component */
struct sof_ipc_comp_volume {
	struct sof_ipc_hdr hdr;
	struct sof_ipc_comp comp;
	struct sof_ipc_pcm_comp pcm;
	uint32_t channels;
	int32_t min_value;
	int32_t max_value;
	enum sof_volume_ramp ramp;
	uint32_t initial_ramp;	/* ramp space in ms */
}  __attribute__((packed));

/* generic SRC component */
struct sof_ipc_comp_src {
	struct sof_ipc_hdr hdr;
	struct sof_ipc_comp comp;
	struct sof_ipc_pcm_comp pcm;
	uint32_t in_mask;	/* SOF_RATE_ supported input rates */
	uint32_t out_mask;	/* SOF_RATE_ supported output rates */
} __attribute__((packed));

/* generic MUX component */
struct sof_ipc_comp_mux {
	struct sof_ipc_hdr hdr;
	struct sof_ipc_comp comp;
	struct sof_ipc_pcm_comp pcm;
} __attribute__((packed));

/* generic tone generator component */
struct sof_ipc_comp_tone {
	struct sof_ipc_hdr hdr;
	struct sof_ipc_comp comp;
	struct sof_ipc_pcm_comp pcm;
} __attribute__((packed));

/* frees components, buffers and pipelines
 * SOF_IPC_TPLG_COMP_FREE, SOF_IPC_TPLG_PIPE_FREE, SOF_IPC_TPLG_BUFFER_FREE
 */
struct sof_ipc_free {
	struct sof_ipc_hdr hdr;
	uint32_t id;
} __attribute__((packed));


struct sof_ipc_comp_reply {
	struct sof_ipc_hdr hdr;
	uint32_t id;
	uint32_t offset;
} __attribute__((packed));


/*
 * Pipeline
 */

/* new pipeline - SOF_IPC_TPLG_PIPE_NEW */
struct sof_ipc_pipe_new {
	struct sof_ipc_hdr hdr;
	uint32_t pipeline_id;
	uint32_t core;		/* core we run on */
	uint32_t schedule_us;	/* schedule evey us */
	uint32_t priority;		/* priority level 0 (low) to 10 (max)*/
	uint32_t low_latency;	/* data is copied from end to end in single tick */
}  __attribute__((packed));

/* pipeline construction complete - SOF_IPC_TPLG_PIPE_COMPLETE */
struct sof_ipc_pipe_ready {
	struct sof_ipc_hdr hdr;
	uint32_t pipeline_id;
}  __attribute__((packed));


struct sof_ipc_pipe_free {
	struct sof_ipc_hdr hdr;
	uint32_t pipeline_id;
}  __attribute__((packed));

/* connect two components in pipeline - SOF_IPC_TPLG_COMP_CONNECT */
struct sof_ipc_pipe_comp_connect {
	struct sof_ipc_hdr hdr;
	uint32_t pipeline_id;
	uint32_t source_id;
	uint32_t buffer_id;
	uint32_t sink_id;
}  __attribute__((packed));

/* connect two components in pipeline - SOF_IPC_TPLG_PIPE_CONNECT */
struct sof_ipc_pipe_pipe_connect {
	struct sof_ipc_hdr hdr;
	uint32_t pipeline_source_id;
	uint32_t comp_source_id;
	uint32_t buffer_id;
	uint32_t pipeline_sink_id;
	uint32_t comp_sink_id;
}  __attribute__((packed));


/*
 * PM
 */

/* PM context element */
struct sof_ipc_pm_ctx_elem {
	uint32_t type;
	uint32_t size;
	uint64_t addr;
}  __attribute__((packed));

/* PM context - SOF_IPC_PM_CTX_SAVE, SOF_IPC_PM_CTX_RESTORE,
 * SOF_IPC_PM_CTX_SIZE */
struct sof_ipc_pm_ctx {
	struct sof_ipc_hdr hdr;
	struct sof_ipc_ring_buffer buffer;
	uint32_t num_elems;
	uint32_t size;
	struct sof_ipc_pm_ctx_elem elems[];
};

#endif
