/*
 * Sound Open Firmware IPC Support
 *
 * Copyright (C) 2017, Intel Corporation. All rights reserved.
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

#ifndef __SND_SOF_IPC_H
#define __SND_SOF_IPC_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <sound/asound.h>

#define SND_SOF_MAX_CHANNELS		8
#define SND_SOF_BUILD_HASH_LENGTH	40
#define SND_SOF_INFO_LENGTH		40


/*
 * IPC Doorbell
 */
#define SND_SOF_IPC_CMD_SHIFT		24

/* stream "PCM" ops */
#define SND_SOF_IPC_CMD_HW_PARAMS	(0x1 << SND_SOF_IPC_CMD_SHIFT)
#define SND_SOF_IPC_CMD_HW_FREE		(0x2 << SND_SOF_IPC_CMD_SHIFT)
#define SND_SOF_IPC_CMD_TRIGGER		(0x3 << SND_SOF_IPC_CMD_SHIFT)

/* "kcontrol" ops */
#define SND_SOF_IPC_CMD_VOLUME		(0x10 << SND_SOF_IPC_CMD_SHIFT)
#define SND_SOF_IPC_CMD_MIXER		(0x11 << SND_SOF_IPC_CMD_SHIFT)
#define SND_SOF_IPC_CMD_CTRL		(0x12 << SND_SOF_IPC_CMD_SHIFT)

/* DAI configuration */
#define SND_SOF_IPC_CMD_DAI		(0x20 << SND_SOF_IPC_CMD_SHIFT)

/* Topology */
#define SND_SOF_IPC_CMD_COMP_NEW	(0x30 << SND_SOF_IPC_CMD_SHIFT)
#define SND_SOF_IPC_CMD_COMP_CON	(0x31 << SND_SOF_IPC_CMD_SHIFT)
#define SND_SOF_IPC_CMD_COMP_FREE	(0x32 << SND_SOF_IPC_CMD_SHIFT)
#define SND_SOF_IPC_CMD_PIPE_NEW	(0x33 << SND_SOF_IPC_CMD_SHIFT)
#define SND_SOF_IPC_CMD_PIPE_CON	(0x34 << SND_SOF_IPC_CMD_SHIFT)
#define SND_SOF_IPC_CMD_PIPE_FREE	(0x35 << SND_SOF_IPC_CMD_SHIFT)
#define SND_SOF_IPC_CMD_PIPE_READY	(0x36 << SND_SOF_IPC_CMD_SHIFT)

/* tracing */
#define SND_SOF_IPC_CMD_TRACE_CONF	(0x60 << SND_SOF_IPC_CMD_SHIFT)
#define SND_SOF_IPC_CMD_TRACE_CTRL	(0x61 << SND_SOF_IPC_CMD_SHIFT)

/* PM */
#define SND_SOF_IPC_CMD_PM_CTRL		(0x71 << SND_SOF_IPC_CMD_SHIFT)

/* compund command */
#define SND_SOF_IPC_CMD_COMPOUND	(0xff << SND_SOF_IPC_CMD_SHIFT)

/*
 * Coumpund Message.
 */
struct snd_sof_ipc_compound_elem {
	u32 ipc_cmd;
	u32 offset;
} __attribute__((packed));

struct snd_sof_ipc_compound {
	u32 num_elems;
	struct snd_sof_ipc_compound_elem elem[0];
	/* command structures here */
} __attribute__((packed));


/*
 * Firmware Information.
 */

/* Firmware Ready and FW version */
struct snd_sof_ipc_fw_version {
	u32 inbox_offset;
	u32 outbox_offset;
	u32 inbox_size;
	u32 outbox_size;
	u32 fw_info_size;
	u32 build;
	u32 minor;
	u32 major;
	u32 ipc_version;
	u8 fw_info[SND_SOF_INFO_LENGTH];
	u8 fw_build_hash[SND_SOF_BUILD_HASH_LENGTH];	
} __attribute__((packed));

/*
 * Stream configuration.
 */

/* channel positions */
enum snd_sof_chmap {
	SND_SOF_CHMAP_UNKNOWN = 0,
	SND_SOF_CHMAP_NA,		/* N/A, silent */
	SND_SOF_CHMAP_MONO,	/* mono stream */
	/* this follows the alsa-lib mixer channel value + 3 */
	SND_SOF_CHMAP_FL,		/* front left */
	SND_SOF_CHMAP_FR,		/* front right */
	SND_SOF_CHMAP_RL,		/* rear left */
	SND_SOF_CHMAP_RR,		/* rear right */
	SND_SOF_CHMAP_FC,		/* front center */
	SND_SOF_CHMAP_LFE,	/* LFE */
	SND_SOF_CHMAP_SL,		/* side left */
	SND_SOF_CHMAP_SR,		/* side right */
	SND_SOF_CHMAP_RC,		/* rear center */
	/* new definitions */
	SND_SOF_CHMAP_FLC,	/* front left center */
	SND_SOF_CHMAP_FRC,	/* front right center */
	SND_SOF_CHMAP_RLC,	/* rear left center */
	SND_SOF_CHMAP_RRC,	/* rear right center */
	SND_SOF_CHMAP_FLW,	/* front left wide */
	SND_SOF_CHMAP_FRW,	/* front right wide */
	SND_SOF_CHMAP_FLH,	/* front left high */
	SND_SOF_CHMAP_FCH,	/* front center high */
	SND_SOF_CHMAP_FRH,	/* front right high */
	SND_SOF_CHMAP_TC,		/* top center */
	SND_SOF_CHMAP_TFL,	/* top front left */
	SND_SOF_CHMAP_TFR,	/* top front right */
	SND_SOF_CHMAP_TFC,	/* top front center */
	SND_SOF_CHMAP_TRL,	/* top rear left */
	SND_SOF_CHMAP_TRR,	/* top rear right */
	SND_SOF_CHMAP_TRC,	/* top rear center */
	/* new definitions for UAC2 */
	SND_SOF_CHMAP_TFLC,	/* top front left center */
	SND_SOF_CHMAP_TFRC,	/* top front right center */
	SND_SOF_CHMAP_TSL,	/* top side left */
	SND_SOF_CHMAP_TSR,	/* top side right */
	SND_SOF_CHMAP_LLFE,	/* left LFE */
	SND_SOF_CHMAP_RLFE,	/* right LFE */
	SND_SOF_CHMAP_BC,		/* bottom center */
	SND_SOF_CHMAP_BLC,	/* bottom left center */
	SND_SOF_CHMAP_BRC,	/* bottom right center */
	SND_SOF_CHMAP_LAST = SND_SOF_CHMAP_BRC,
};

enum snd_soc_stream_type {
	SND_SOF_STREAM_PCM = 0,
	SND_SOF_STREAM_VORBIS = 1,
};

/* Stream buffer info */
struct snd_sof_ipc_stream_buffer {
	u64 phy_addr;	/* PHY base address of page tables */
	u32 num_pages;	
	u32 size;	/* can be less than num_pages * PAGE_SIZE */
	u32 offset;
} __attribute__((packed));

/* pcm format configuration */
struct snd_sof_ipc_pcm_format {
	u32 rate;	/* sample rate */
	u32 format;	/* sample format */
	enum snd_sof_chmap[SND_SOF_MAX_CHANNELS];	/* channel map */
} __attribute__((packed));

/* Stream HW Params Request */
struct snd_sof_ipc_stream_hw_params_req {
	u32 stream_id;		/* PCM number */
	enum snd_soc_stream_type type;
	struct snd_sof_ipc_stream_buffer buffer;
	union {
		struct snd_sof_ipc_pcm_format pcm;
	};
} __attribute__((packed));

/* Stream runtime information */
struct snd_sof_stream_info {
	u32 dma_posn;		/* dma position in frames */
	u32 dai_posn;		/* dai position readback offset */
	u64 audio_tstamp;	/* audio timestamp */
	u64 system_tstamp;	/* system timestamp */
};

/* HW Params Reply */
struct snd_sof_ipc_stream_hw_params_reply {
	u32 stream_id;
	u32 stream_info_offset;	/* offset to struct snd_sof_stream_info */
	
} __attribute__((packed));

/* Stream Free Request */
struct snd_sof_ipc_stream_hw_free_req {
	u32 stream_id;
} __attribute__((packed));

/* Stream set position */
struct snd_sof_ipc_stream_trigger {
	u32 stream_id;
	u32 trigger_cmd;
	u32 position;  /* TDOD should this be in frames */
} __attribute__((packed));

/*
 * DAI configuration.
 */

enum snd_sof_dai_type {
	SND_SOF_DAI_SSP = 0,
	SND_SOF_DAI_DMIC = 1,
	SND_SOF_DAI_HDA = 2,
};

struct snd_sof_ipc_ssp_config {
	u32 mclk;
	u32 bclk;
	u32 format;
	u32 clock_source;
	u32 mode;
	u32 num_slots;
	u32 slot_size;
} __attribute__((packed));

struct snd_sof_ipc_dmic_config {
	u32 clk;
} __attribute__((packed));

struct snd_sof_ipc_hda_config {
	u32 clk;
} __attribute__((packed));

/* Device Configuration Request */
struct snd_sof_ipc_dai_config {
	u32 dai_id;
	enum snd_sof_dai_type type;
	union {
		struct snd_sof_ipc_ssp_config ssp;
		struct snd_sof_ipc_dmic_config dmic;
		struct snd_sof_ipc_hda_config hda;
	};
} __attribute__((packed));


/*
 * Volume controls.
 */

/* Volume Curve Type*/
enum snd_sof_volume_curve {
	SND_SOF_VOLUME_CURVE_NONE = 0,
	SND_SOF_VOLUME_CURVE_FADE = 1
};

/* Set Volume */
struct snd_sof_ipc_volume_set {
	u32 volume_id;
	u32 channel_mask;
	u32 target_volume;
	u32 curve_duration;
	enum snd_sof_volume_curve curve_type;
} __attribute__((packed));

/* Get Volume - can also be read by mailbox IO */
struct snd_sof_ipc_volume_get {
	u32 volume_id;
	u32 volume[SND_SOF_MAX_CHANNELS];
} __attribute__((packed));


/*
 * Mixer configuration.
 */

/* Set/Get Mixer */
struct snd_sof_ipc_mixer {
	u32 mixer_id;
	u32 channel_mask;
} __attribute__((packed));


/*
 * Component control - component control that does not fit into regular IPC.
 */
struct snd_sof_ipc_control {
	u32 comp_id;
	u32 ctrl_id;
	u32 channel_mask;
	u32 size;
	u8 data[0];
} __attribute__((packed));

/*
 * Power Management.
 */

/* DX Power State */
enum snd_sof_pm_state {
	SND_SOF_PM_D0	= 0,
	SND_SOF_PM_D1	= 1,
	SND_SOF_PM_D3	= 3,
};

/* DX State Request */
struct snd_sof_ipc_pm_req {
	u32 state;
} __attribute__((packed));

/* DX State Reply Memory Info Item */
struct snd_sof_ipc_pm_elem {
	u32 offset;
	u32 size;
	u32 source;
} __attribute__((packed));

/* DX State Reply */
struct snd_sof_ipc_pm_reply {
	u32 elems;
	struct snd_sof_ipc_pm_elem elem[0];
} __attribute__((packed));


/*
 * Component
 */

/* types of component */
enum snd_sof_comp_type {
	SND_SOF_COMP_HOST = 0,
	SND_SOF_COMP_DAI,
	SND_SOF_COMP_SG_HOST,	/* scatter gather varient */
	SND_SOF_COMP_SG_DAI,	/* scatter gather varient */
	SND_SOF_COMP_VOLUME,
	SND_SOF_COMP_MIXER,
	SND_SOF_COMP_SRC,
};

/* generic PCM component data */
struct snd_sof_ipc_pcm_comp {
	u32 format;	/* data format */
	u32 frames;	/* number of frames to process */
	u32 channels;	/* number of channels */
	enum snd_sof_chmap[0];	/* channel map */
} __attribute__((packed));

/* generic host componeent */
struct snd_sof_ipc_comp_host {
	struct snd_sof_ipc_pcm_comp pcm;
	uint32_t num_periods;
	uint32_t preload_period_count;
	uint32_t dmac_id;
	uint32_t dmac_chan;
	uint32_t dmac_config; /* DMA engine specific */
}  __attribute__((packed));

/* generic DAI componeent */
struct snd_sof_ipc_comp_dai {
	struct snd_sof_ipc_pcm_comp pcm;
	uint32_t num_periods;
	uint32_t dmac_id;
	uint32_t dmac_chan;
	uint32_t dmac_config; /* DMA engine specific */	
}  __attribute__((packed));

/* generic mixer componeent */
struct snd_sof_ipc_comp_mixer {
	struct snd_sof_ipc_pcm_comp pcm;
}  __attribute__((packed));

/* generic volume componeent */
struct snd_sof_ipc_comp_volume {
	struct snd_sof_ipc_pcm_comp pcm;
	int32_t min_value;
	int32_t max_value;
}  __attribute__((packed));

/* generic new component */
struct snd_sof_ipc_comp_new {
	u32 comp_id;
	u32 pipline_id;
	enum snd_sof_comp_type type;
	union {
		struct snd_sof_ipc_comp_volume volume;
		struct snd_sof_ipc_comp_host host;
		struct snd_sof_ipc_comp_dai dai;
		struct snd_sof_ipc_comp_mixer mixer;
		struct snd_sof_ipc_comp_src src;
	};
} __attribute__((packed));

struct snd_sof_ipc_comp_free {
	u32 comp_id;
} __attribute__((packed));

/*
 * Pipeline
 */

/* new pipeline */
struct snd_sof_ipc_pipe_new {
	u32 pipeline_id;
	u32 core;		/* core we run on */
	u32 schedule_us;	/* schedule evey us */
	u32 priority;		/* priority level 0 (low) to 10 (max)*/
}  __attribute__((packed));

struct snd_sof_ipc_pipe_ready {
	u32 pipeline_id;
}  __attribute__((packed));

struct snd_sof_ipc_pipe_free {
	u32 pipeline_id;
}  __attribute__((packed));

#endif
