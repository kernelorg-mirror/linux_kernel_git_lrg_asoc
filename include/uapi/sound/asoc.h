/*
 * linux/uapi/sound/asoc.h -- ALSA SoC Firmware Controls and DAPM
 *
 * Copyright (C) 2012 Texas Instruments Inc.
 * Copyright (C) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Simple file API to load FW that includes mixers, coefficients, DAPM graphs,
 * algorithms, equalisers, DAIs, widgets etc.
*/

#ifndef __LINUX_UAPI_SND_ASOC_H
#define __LINUX_UAPI_SND_ASOC_H

#include <linux/types.h>
#include <uapi/sound/asound.h>

/*
 * Numeric IDs for stock mixer types that are used to map and enumerate FW
 * based mixers.
 */
#define SOC_CONTROL_ID_PUT(p)	((p & 0xff) << 16)
#define SOC_CONTROL_ID_GET(g)	((g & 0xff) << 8)
#define SOC_CONTROL_ID_INFO(i)	((i & 0xff) << 0)
#define SOC_CONTROL_ID(g, p, i)	\
	(SOC_CONTROL_ID_PUT(p) | SOC_CONTROL_ID_GET(g) |\
	SOC_CONTROL_ID_INFO(i))

#define SOC_CONTROL_GET_ID_PUT(id)	((id & 0xff0000) >> 16)
#define SOC_CONTROL_GET_ID_GET(id)	((id & 0x00ff00) >> 8)
#define SOC_CONTROL_GET_ID_INFO(id)	((id & 0x0000ff) >> 0)

/* individual kcontrol info types - can be mixed with other types */
#define SOC_CONTROL_TYPE_EXT		0	/* driver defined */
#define SOC_CONTROL_TYPE_VOLSW		1
#define SOC_CONTROL_TYPE_VOLSW_SX	2
#define SOC_CONTROL_TYPE_VOLSW_S8	3
#define SOC_CONTROL_TYPE_VOLSW_XR_SX	4
#define SOC_CONTROL_TYPE_ENUM		6
#define SOC_CONTROL_TYPE_ENUM_EXT	7
#define SOC_CONTROL_TYPE_BYTES		8
#define SOC_CONTROL_TYPE_BOOL_EXT	9
#define SOC_CONTROL_TYPE_ENUM_VALUE	10
#define SOC_CONTROL_TYPE_RANGE		11
#define SOC_CONTROL_TYPE_STROBE		12
#define SOC_CONTROL_TYPE_BYTES_EXT	13
#define SOC_CONTROL_TYPE_VOLSW_EXT	14

/* convenience macros for compound control type mappings */
#define SOC_CONTROL_IO_VOLSW \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_VOLSW, \
		SOC_CONTROL_TYPE_VOLSW, \
		SOC_CONTROL_TYPE_VOLSW)
#define SOC_CONTROL_IO_VOLSW_SX \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_VOLSW_SX, \
		SOC_CONTROL_TYPE_VOLSW_SX, \
		SOC_CONTROL_TYPE_VOLSW)
#define SOC_CONTROL_IO_VOLSW_S8 \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_VOLSW_S8, \
		SOC_CONTROL_TYPE_VOLSW_S8, \
		SOC_CONTROL_TYPE_VOLSW_S8)
#define SOC_CONTROL_IO_VOLSW_XR_SX \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_VOLSW_XR_SX, \
		SOC_CONTROL_TYPE_VOLSW_XR_SX, \
		SOC_CONTROL_TYPE_VOLSW_XR_SX)
#define SOC_CONTROL_IO_EXT \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_VOLSW)
#define SOC_CONTROL_IO_ENUM \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_ENUM, \
		SOC_CONTROL_TYPE_ENUM, \
		SOC_CONTROL_TYPE_ENUM)
#define SOC_CONTROL_IO_ENUM_EXT \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_ENUM_EXT)
#define SOC_CONTROL_IO_BYTES \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_BYTES, \
		SOC_CONTROL_TYPE_BYTES, \
		SOC_CONTROL_TYPE_BYTES)
#define SOC_CONTROL_IO_BOOL_EXT \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_BOOL_EXT)
#define SOC_CONTROL_IO_ENUM_VALUE \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_ENUM_VALUE, \
		SOC_CONTROL_TYPE_ENUM_VALUE, \
		SOC_CONTROL_TYPE_ENUM)
#define SOC_CONTROL_IO_RANGE \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_RANGE, \
		SOC_CONTROL_TYPE_RANGE, \
		SOC_CONTROL_TYPE_RANGE)
#define SOC_CONTROL_IO_STROBE \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_STROBE, \
		SOC_CONTROL_TYPE_STROBE, \
		SOC_CONTROL_TYPE_STROBE)
#define SOC_CONTROL_IO_BYTES_EXT \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_BYTES_EXT)
#define SOC_CONTROL_IO_VOLSW_EXT \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_VOLSW)

/* individual widget kcontrol info types - can be mixed with other types */
#define SOC_DAPM_TYPE_VOLSW		64
#define SOC_DAPM_TYPE_ENUM_DOUBLE	65
#define SOC_DAPM_TYPE_ENUM_VIRT		66
#define SOC_DAPM_TYPE_ENUM_VALUE	67
#define SOC_DAPM_TYPE_PIN		68
#define SOC_DAPM_TYPE_ENUM_EXT		69

/* convenience macros for compound widget kcontrol type mappings */
#define SOC_DAPM_IO_VOLSW \
	SOC_CONTROL_ID(SOC_DAPM_TYPE_VOLSW, \
		SOC_DAPM_TYPE_VOLSW, \
		SOC_DAPM_TYPE_VOLSW)
#define SOC_DAPM_IO_ENUM_DOUBLE \
	SOC_CONTROL_ID(SOC_DAPM_TYPE_ENUM_DOUBLE, \
		SOC_DAPM_TYPE_ENUM_DOUBLE, \
		SOC_CONTROL_TYPE_ENUM)
#define SOC_DAPM_IO_ENUM_VIRT \
	SOC_CONTROL_ID(SOC_DAPM_TYPE_ENUM_VIRT, \
		SOC_DAPM_TYPE_ENUM_VIRT, \
		SOC_CONTROL_TYPE_ENUM)
#define SOC_DAPM_IO_ENUM_VALUE \
	SOC_CONTROL_ID(SOC_DAPM_TYPE_ENUM_VALUE, \
		SOC_DAPM_TYPE_ENUM_VALUE, \
		SOC_CONTROL_TYPE_ENUM)
#define SOC_DAPM_IO_PIN \
	SOC_CONTROL_ID(SOC_DAPM_TYPE_PIN, \
		SOC_DAPM_TYPE_PIN, \
		SOC_DAPM_TYPE_PIN)
#define SOC_DAPM_IO_ENUM_EXT \
	SOC_CONTROL_ID(SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_EXT, \
		SOC_CONTROL_TYPE_ENUM)

/* dapm widget types */
enum snd_soc_dapm_type {
	snd_soc_dapm_input = 0,		/* input pin */
	snd_soc_dapm_output,		/* output pin */
	snd_soc_dapm_mux,		/* selects 1 analog signal from many inputs */
	snd_soc_dapm_mixer,		/* mixes several analog signals together */
	snd_soc_dapm_mixer_named_ctl,	/* mixer with named controls */
	snd_soc_dapm_pga,		/* programmable gain/attenuation (volume) */
	snd_soc_dapm_out_drv,		/* output driver */
	snd_soc_dapm_adc,		/* analog to digital converter */
	snd_soc_dapm_dac,		/* digital to analog converter */
	snd_soc_dapm_micbias,		/* microphone bias (power) */
	snd_soc_dapm_mic,		/* microphone */
	snd_soc_dapm_hp,		/* headphones */
	snd_soc_dapm_spk,		/* speaker */
	snd_soc_dapm_line,		/* line input/output */
	snd_soc_dapm_switch,		/* analog switch */
	snd_soc_dapm_vmid,		/* codec bias/vmid - to minimise pops */
	snd_soc_dapm_pre,		/* machine specific pre widget - exec first */
	snd_soc_dapm_post,		/* machine specific post widget - exec last */
	snd_soc_dapm_supply,		/* power/clock supply */
	snd_soc_dapm_regulator_supply,	/* external regulator */
	snd_soc_dapm_clock_supply,	/* external clock */
	snd_soc_dapm_aif_in,		/* audio interface input */
	snd_soc_dapm_aif_out,		/* audio interface output */
	snd_soc_dapm_siggen,		/* signal generator */
	snd_soc_dapm_dai_in,		/* link to DAI structure */
	snd_soc_dapm_dai_out,
	snd_soc_dapm_dai_link,		/* link between two DAI structures */
	snd_soc_dapm_kcontrol,		/* Auto-disabled kcontrol */
};

/* Header magic number and string sizes */
#define SND_SOC_TPLG_MAGIC		0x41536F43 /* ASoC */

/* string sizes */
#define SND_SOC_TPLG_NUM_TEXTS		16

/* ABI version */
#define SND_SOC_TPLG_ABI_VERSION	0x2

/*
 * File and Block header data types.
 * Add new generic and vendor types to end of list.
 * Generic types are handled by the core whilst vendors types are passed
 * to the component drivers for handling.
 */
#define SND_SOC_TPLG_MIXER		1
#define SND_SOC_TPLG_DAPM_GRAPH		2
#define SND_SOC_TPLG_DAPM_WIDGET	3
#define SND_SOC_TPLG_BE			4
#define SND_SOC_TPLG_FE			5
#define SND_SOC_TPLG_COEFF		6
#define SND_SOC_TPLG_MANIFEST		7
#define SND_SOC_TPLG_CC			8
#define SND_SOC_TPLG_TYPE_MAX	SND_SOC_TPLG_CC

/* vendor block IDs - please add new vendor types to end */
#define SND_SOC_TPLG_VENDOR_FW		1000
#define SND_SOC_TPLG_VENDOR_CONFIG	1001
#define SND_SOC_TPLG_VENDOR_COEFF	1002
#define SND_SOC_TPLG_VENDOR_CODEC	1003

/*
 * File and Block Header.
 * This header preceeds all object and object arrays below.
 */
struct snd_soc_tplg_hdr {
	__le32 magic;
	__le32 abi;		/* ABI version */
	__le32 version;		/* optional vendor specific version details */
	__le32 type;		/* SND_SOC_TPLG_TYPE_ */
	__le32 vendor_type;	/* optional vendor specific type info */
	__le32 size;		/* data bytes, excluding this header */
	__le32 id;		/* identifier for block */
	char reserved[128];
} __attribute__((packed));

/*
 * Manifest. List totals for each payload type. Not used in parsing, but will
 * be passed to the component driver before any other objects in order for any
 * global componnent resource allocations.
 */
struct snd_soc_tplg_manifest {
	__le32 control_elems;	/* number of control elements */
	__le32 widget_elems;	/* number of widget elements */
	__le32 graph_elems;	/* number of graph elements */
	__le32 dai_elems;	/* number of DAI elements */
	__le32 dai_link_elems;	/* number of DAI link elements */
	char reserved[128];
} __attribute__((packed));

/*
 * Element Header. Defines number of elements in an object array.
 * This prceeeds al mixers, widgets, graph elems, BEs, FEs, CC links.
 */
struct snd_soc_tplg_elems {
	__le32 count; /* number of elements */
	/* elements start here */
} __attribute__((packed));

/*
 * Private data.
 * All topology objects may have private data that can be used by the driver or
 * firmware. Core will ignore this data.
 */
struct snd_soc_tplg_private {
	__le32 length;
	char data[0];
} __attribute__((packed));

/*
 * Kcontrol TLV data.
 */
struct snd_soc_tplg_ctl_tlv {
	__le32 numid;	/* control element numeric identification */
	__le32 length;	/* in bytes aligned to 4 */
	char reserved[16];
	/* tlv data starts here */
} __attribute__((packed));

/*
 * kcontrol header
 */
struct snd_soc_tplg_control_hdr {
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	__le32 index;
	__le32 access;
	__le32 tlv_size;
	char reserved[8];
} __attribute__((packed));

/*
 * Mixer kcontrol.
 */
struct snd_soc_tplg_mixer_control {
	struct snd_soc_tplg_control_hdr hdr;
	__s32 min;
	__s32 max;
	__s32 platform_max;
	__le32 reg;
	__le32 rreg;
	__le32 shift;
	__le32 rshift;
	__le32 invert;
	char reserved[64];
	struct snd_soc_tplg_private priv;
} __attribute__((packed));

/*
 * Enumerated kcontrol
 */
struct snd_soc_tplg_enum_control {
	struct snd_soc_tplg_control_hdr hdr;
	__le32 reg;
	__le32 shift_l;
	__le32 shift_r;
	__le32 items;
	__le32 mask;
	__le32 count;
	char reserved[64];
	char texts[SND_SOC_TPLG_NUM_TEXTS][SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	__le32 values[SND_SOC_TPLG_NUM_TEXTS * SNDRV_CTL_ELEM_ID_NAME_MAXLEN / 4];
	struct snd_soc_tplg_private priv;
} __attribute__((packed));

/*
 * Bytes kcontrol
 */
struct snd_soc_tplg_bytes_ext {
	struct snd_soc_tplg_control_hdr hdr;
	__s32 max;
	struct snd_soc_tplg_private priv;
} __attribute__((packed));

/*
 * DAPM Graph Element
 */
struct snd_soc_tplg_dapm_graph_elem {
	char sink[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	char control[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	char source[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
} __attribute__((packed));

/*
 * DAPM Widget.
 */
struct snd_soc_tplg_dapm_widget {
	__le32 id;		/* snd_soc_dapm_type */
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	char sname[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];

	__s32 reg;		/* negative reg = no direct dapm */
	__le32 shift;		/* bits to shift */
	__le32 mask;		/* non-shifted mask */
	__u32 invert:1;		/* invert the power bit */
	__u32 ignore_suspend:1;	/* kept enabled over suspend */
	__u16 event_flags;
	__u16 event_type;
	__u16 num_kcontrols;
	char reserved[64];
	struct snd_soc_tplg_private priv;
	/*
	 * kcontrols that relate to this widget
	 * follow here after widget private data
	 */
} __attribute__((packed));

/*
 * Coefficient File Data.
 */
struct snd_soc_file_coeff_data {
	__le32 count;	/* in elems */
	__le32 size;	/* total data size */
	__le32 id;	/* associated mixer ID */
	char reserved[16];
	/* data here */
} __attribute__((packed));

/*
 * Stream Capabilities
 */
struct snd_soc_tplg_stream_caps {
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	__le64 formats;		/* supported formats SNDRV_PCM_FMTBIT_* */
	__le32 rates;		/* supported rates SNDRV_PCM_RATE_* */
	__le32 rate_min;	/* min rate */
	__le32 rate_max;	/* max rate */
	__le32 channels_min;	/* min channels */
	__le32 channels_max;	/* max channels */
	char reserved[64];
} __attribute__((packed));

/*
 * FE or BE Stream configuration supported by SW/FW
 */
struct snd_soc_tplg_stream {
	__le64 format;		/* SNDRV_PCM_FMTBIT_* */
	__le32 rate;		/* SNDRV_PCM_RATE_* */
	__le32 channels;	/* channels */
	__le32 tdm_slot;	/* optional BE bitmask of supported TDM slots */
	char reserved[8];
} __attribute__((packed));

/*
 * Duplex stream configuration supported by SW/FW.
 */
struct snd_soc_tplg_stream_config {
	struct snd_soc_tplg_stream playback;
	struct snd_soc_tplg_stream capture;
	__le32 dai_fmt;			/* SND_SOC_DAIFMT_ - BE only */
	char reserved[8];
} __attribute__((packed));

/*
 * PCM FE Elements. Describes SW/FW specific features of PCM FE.
 */
struct snd_soc_tplg_pcm_fe {
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	__le32 id;		/* Type used to match dais and set ops */
	__le32 compress:1;	/* 1 = compressed; 0 = PCM */
	__le32 playback:1;	/* If playback stream supported this dai */
	__le32 capture:1;	/* If capture stream supported this dai */
	__le32 num_configs;	/* number of configs */
	char reserved[64];
	struct snd_soc_tplg_stream_caps caps[2];	/* capabilities */
	struct snd_soc_tplg_stream_config config[0];	/* supported SW/FW configs */
} __attribute__((packed));

/*
 * BE Elements. Describes SW/FW specific features of BE.
 */
struct snd_soc_tplg_pcm_be {
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	__le32 id;			/* unique BE ID - used to match */
	__le32 playback:1;		/* supports playback in DPCM mode */
	__le32 capture:1;		/* supports capture in DPCM mode */
	__le32 nonatomic:1;		/* uses non atomic trigger */
	__le16 trigger[2];		/* SND_SOC_DPCM_  playback and capture */
	__le32 num_configs;		/* number of configs */
	char reserved[64];
	struct snd_soc_tplg_stream_caps caps[2];	/* capabilities */
	struct snd_soc_tplg_stream_config config[0];	/* supported SW/FW configs */
}__attribute__((packed));

/*
 * Codec <-> codec style link - describes capablities for link
 */
struct snd_soc_tplg_pcm_cc {
	char name[SNDRV_CTL_ELEM_ID_NAME_MAXLEN];
	__le32 id;	/* for matching */
	char reserved[32];
	struct snd_soc_tplg_stream_config config;
};

#endif
