/*
 *  sst_adsp_ipc.c - Intel SST ADSP FW ABI
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

#ifndef __SST_ADSP_IPC_H
#define __SST_ADSP_IPC_H

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>

#define SST_HSW_NO_CHANNELS	2
#define SST_HSW_MAX_DX_REGIONS	14

/* Stream Allocate Path ID */
enum sst_hsw_stream_path_id {
	SST_HSW_STREAM_PATH_SSP0_OUT = 0,
	SST_HSW_STREAM_PATH_SSP0_IN = 1,
	SST_HSW_STREAM_PATH_MAX_PATH_ID = 2,
};

/* Stream Allocate Stream Type */
enum sst_hsw_stream_type {
	SST_HSW_STREAM_TYPE_RENDER = 0,
	SST_HSW_STREAM_TYPE_SYSTEM = 1,
	SST_HSW_STREAM_TYPE_CAPTURE = 2,
	SST_HSW_STREAM_TYPE_LOOPBACK = 3,
	SST_HSW_STREAM_TYPE_MAX_STREAM_TYPE = 4,
};

/* Stream Allocate Stream Format */
enum sst_hsw_stream_format {
	SST_HSW_STREAM_FORMAT_PCM_FORMAT = 0,
	SST_HSW_STREAM_FORMAT_MP3_FORMAT = 1,
	SST_HSW_STREAM_FORMAT_AAC_FORMAT = 2,
	SST_HSW_STREAM_FORMAT_MAX_FORMAT_ID = 3,
};

/* Device ID */
enum sst_hsw_device_id {
	SST_HSW_DEVICE_SSP_0   = 0,
	SST_HSW_DEVICE_SSP_1   = 1,
};

/* Device Master Clock Frequency */
enum sst_hsw_device_mclk {
	SST_HSW_DEVICE_MCLK_OFF         = 0,
	SST_HSW_DEVICE_MCLK_FREQ_6_MHZ  = 1,
	SST_HSW_DEVICE_MCLK_FREQ_12_MHZ = 2,
	SST_HSW_DEVICE_MCLK_FREQ_24_MHZ = 3,
};

/* Device Clock Master */
enum sst_hsw_device_mode {
	SST_HSW_DEVICE_CLOCK_SLAVE   = 0,
	SST_HSW_DEVICE_CLOCK_MASTER  = 1,
};
/* Audio Curve Type */
enum sst_hsw_ipc_volume_curve_type {
	SST_HSW_AUDIO_CURVE_TYPE_NONE = 0,
	SST_HSW_AUDIO_CURVE_TYPE_FADE = 1,
};

/* DX Power State */
enum sst_hsw_dx_state {
	SST_HSW_DX_STATE_D0     = 0,
	SST_HSW_DX_STATE_D1     = 1,
	SST_HSW_DX_STATE_D3     = 3,
	SST_HSW_DX_STATE_MAX  = 3,
};

/* DX State Type */
enum sst_hsw_dx_type {
	SST_HSW_DX_TYPE_FW_IMAGE = 0,
	SST_HSW_DX_TYPE_MEMORY_DUMP = 1
};

/* Volume Curve Type*/
enum sst_hsw_volume_curve {
	SST_HSW_VOLUME_CURVE_NONE = 0,
	SST_HSW_VOLUME_CURVE_FADE = 1
};

/* Sample ordering */
enum sst_hsw_interleaving {
	SST_HSW_INTERLEAVING_PER_CHANNEL = 0, /* [s1_ch1...s1_chN,...,sM_ch1...sM_chN] */
	SST_HSW_INTERLEAVING_PER_SAMPLE  = 1, /* [s1_ch1...sM_ch1,...,s1_chN...sM_chN] */
};

/* Channel indices */
enum sst_hsw_channel_index {
	SST_HSW_CHANNEL_LEFT            = 0,
	SST_HSW_CHANNEL_CENTER          = 1,
	SST_HSW_CHANNEL_RIGHT           = 2,
	SST_HSW_CHANNEL_LEFT_SURROUND   = 3,
	SST_HSW_CHANNEL_CENTER_SURROUND = 3,
	SST_HSW_CHANNEL_RIGHT_SURROUND  = 4,
	SST_HSW_CHANNEL_LFE             = 7,
	SST_HSW_CHANNEL_INVALID         = 0xF,
};

/* List of supported channel maps. */
enum sst_hsw_channel_config {
	SST_HSW_CHANNEL_CONFIG_MONO      = 0, /**< One channel only. */
	SST_HSW_CHANNEL_CONFIG_STEREO    = 1, /**< L & R. */
	SST_HSW_CHANNEL_CONFIG_2_POINT_1 = 2, /**< L, R & LFE; PCM only. */
	SST_HSW_CHANNEL_CONFIG_3_POINT_0 = 3, /**< L, C & R; MP3 & AAC only. */
	SST_HSW_CHANNEL_CONFIG_3_POINT_1 = 4, /**< L, C, R & LFE; PCM only. */
	SST_HSW_CHANNEL_CONFIG_QUATRO    = 5, /**< L, R, Ls & Rs; PCM only. */
	SST_HSW_CHANNEL_CONFIG_4_POINT_0 = 6, /**< L, C, R & Cs; MP3 & AAC only. */
	SST_HSW_CHANNEL_CONFIG_5_POINT_0 = 7, /**< L, C, R, Ls & Rs. */
	SST_HSW_CHANNEL_CONFIG_5_POINT_1 = 8, /**< L, C, R, Ls, Rs & LFE. */
	SST_HSW_CHANNEL_CONFIG_DUAL_MONO = 9, /**< One channel replicated in two. */
	SST_HSW_CHANNEL_CONFIG_INVALID
};

/* List of supported ADSP sample rates */
enum sample_frequency {
	SST_HSW_FS_8000HZ   = 8000,
	SST_HSW_FS_11025HZ  = 11025,
	SST_HSW_FS_12000HZ  = 12000, /** Mp3, AAC, SRC only. */
	SST_HSW_FS_16000HZ  = 16000,
	SST_HSW_FS_22050HZ  = 22050,
	SST_HSW_FS_24000HZ  = 24000, /** Mp3, AAC, SRC only. */
	SST_HSW_FS_32000HZ  = 32000,
	SST_HSW_FS_44100HZ  = 44100,
	SST_HSW_FS_48000HZ  = 48000, /**< Default. */
	SST_HSW_FS_64000HZ  = 64000, /** AAC, SRC only. */
	SST_HSW_FS_88200HZ  = 88200, /** AAC, SRC only. */
	SST_HSW_FS_96000HZ  = 96000, /** AAC, SRC only. */
	SST_HSW_FS_128000HZ = 128000, /** SRC only. */
	SST_HSW_FS_176400HZ = 176400, /** SRC only. */
	SST_HSW_FS_192000HZ = 192000, /** SRC only. */
	SST_HSW_FS_INVALID
};

/** List of supported bit depths. */
enum bitdepth {
	SST_HSW_DEPTH_8BIT  = 8,
	SST_HSW_DEPTH_16BIT = 16,
	SST_HSW_DEPTH_24BIT = 24, /**< Default. */
	SST_HSW_DEPTH_32BIT = 32,
	SST_HSW_DEPTH_INVALID = 33,
};

struct sst_hsw;
struct sst_hsw_stream;
struct sst_pdata;

/* FW version */
struct sst_hsw_ipc_fw_version {
	uint8_t build;
	uint8_t minor;
	uint8_t major;
	uint8_t type;
} __attribute__((packed));

/* Debug Dump Log Reply */
struct sst_hsw_ipc_debug_log_reply {
	uint32_t log_buffer_begining;
	uint32_t log_buffer_size;
} __attribute__((packed));

/* Stream glitch position */
struct sst_hsw_ipc_stream_glitch_position {
	uint32_t glitch_type;
	uint32_t present_pos;
	uint32_t write_pos;
} __attribute__((packed));

/* Stream get position */
struct sst_hsw_ipc_stream_get_position {
	uint32_t position;
	uint32_t fw_cycle_count;
} __attribute__((packed));

/* Stream set position */
struct sst_hsw_ipc_stream_set_position {
	uint32_t position;
	uint32_t end_of_buffer;
} __attribute__((packed));

/* Stream Free Request */
struct sst_hsw_ipc_stream_free_req {
	uint8_t stream_id;
	uint8_t reserved[3];
} __attribute__((packed));

/* Set Volume Request */
struct sst_hsw_ipc_volume_req {
	uint32_t channel;
	uint32_t target_volume;
	uint64_t curve_duration;
	uint32_t curve_type;
} __attribute__((packed));

/* Device Configuration Request */
struct sst_hsw_ipc_device_config_req {
	uint32_t ssp_interface;
	uint32_t clock_frequency;
	uint32_t mode;
	uint16_t clock_divider;
	uint16_t reserved;
} __attribute__((packed));

/* Audio Data formats */
struct sst_hsw_audio_data_format_ipc {
	uint32_t frequency;
	uint32_t bitdepth;
	uint32_t map;
	uint32_t config;
	uint32_t style;
	uint8_t ch_num;
	uint8_t valid_bit;
	uint8_t reserved[2];
} __attribute__((packed));

/* Stream ring info */
struct sst_hsw_ipc_stream_ring {
	uint32_t ring_pt_address;
	uint32_t num_pages;
	uint32_t ring_size;
	uint32_t ring_offset;
	uint32_t ring_first_pfn;
} __attribute__((packed));

/* Stream Allocate Request */
struct sst_hsw_ipc_stream_alloc_req {
	uint8_t path_id;
	uint8_t stream_type;
	uint8_t format_id;
	uint8_t reserved;
	struct sst_hsw_audio_data_format_ipc format;
	struct sst_hsw_ipc_stream_ring ringinfo;
} __attribute__((packed));

/* Stream Allocate Reply */
struct sst_hsw_ipc_stream_alloc_reply {
	uint32_t stream_hw_id;
	uint32_t mixer_hw_id; // returns rate ????
	uint32_t read_position_register_address;
	uint32_t presentation_position_register_address;
	uint32_t peak_meter_register_address[SST_HSW_NO_CHANNELS];
	uint32_t volume_register_address[SST_HSW_NO_CHANNELS];
} __attribute__((packed));

/* Get Mixer Stream Info */
struct sst_hsw_ipc_stream_info_reply {
	uint32_t mixer_hw_id;
	uint32_t peak_meter_register_address[SST_HSW_NO_CHANNELS];
	uint32_t volume_register_address[SST_HSW_NO_CHANNELS];
} __attribute__((packed));

/* DX State Request */
struct sst_hsw_ipc_dx_req {
        uint8_t state;
	uint8_t reserved[3];
} __attribute__((packed));

/* DX State Reply Memory Info Item */
struct sst_hsw_ipc_dx_memory_item {
	uint32_t offset;
	uint32_t size;
	uint32_t source;
} __attribute__((packed));

/* DX State Reply */
struct sst_hsw_ipc_dx_reply {
	uint32_t entries_no;
	struct sst_hsw_ipc_dx_memory_item mem_info[SST_HSW_MAX_DX_REGIONS];
} __attribute__((packed));

struct sst_hsw_ipc_fw_version;

/* SST Init & Free */
struct sst_hsw *sst_hsw_new(struct device *dev, const u8 *fw, size_t fw_length,
	u32 fw_offset);
void sst_hsw_free(struct sst_hsw *hsw);
int sst_hsw_fw_get_version(struct sst_hsw *hsw,
	struct sst_hsw_ipc_fw_version *version);
u32 create_channel_map(enum sst_hsw_channel_config config);

/* Stream Mixer Controls - */
int sst_hsw_stream_mute(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 channel);
int sst_hsw_stream_unmute(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 channel);

int sst_hsw_stream_set_volume(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 channel, u32 volume);
int sst_hsw_stream_get_volume(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 channel, u32 *volume);

int sst_hsw_stream_set_volume_curve(struct sst_hsw *hsw,
	struct sst_hsw_stream *stream, u64 curve_duration,
	enum sst_hsw_volume_curve curve);

/* Global Mixer Controls - */
int sst_hsw_mixer_mute(struct sst_hsw *hsw, u32 stage_id, u32 channel);
int sst_hsw_mixer_unmute(struct sst_hsw *hsw, u32 stage_id, u32 channel);

int sst_hsw_mixer_set_volume(struct sst_hsw *hsw, u32 stage_id, u32 channel,
	u32 volume);
int sst_hsw_mixer_get_volume(struct sst_hsw *hsw, u32 stage_id, u32 channel,
	u32 *volume);

int sst_hsw_mixer_set_volume_curve(struct sst_hsw *hsw,
	u64 curve_duration, enum sst_hsw_volume_curve curve);

/* Stream API */
struct sst_hsw_stream *sst_hsw_stream_new(struct sst_hsw *hsw, int id,
	u32 (*get_write_position)(struct sst_hsw_stream *stream, void *data),
	void *data);

int sst_hsw_stream_free(struct sst_hsw *hsw, struct sst_hsw_stream *stream);

/* Stream Configuration */
int sst_hsw_stream_format(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum sst_hsw_stream_path_id path_id,
	enum sst_hsw_stream_type stream_type,
	enum sst_hsw_stream_format format_id);

int sst_hsw_stream_buffer(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 ring_pt_address, u32 num_pages,
	u32 ring_size, u32 ring_offset, u32 ring_first_pfn);

int sst_hsw_stream_commit(struct sst_hsw *hsw, struct sst_hsw_stream *stream);

int sst_hsw_stream_set_valid(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 bits);
int sst_hsw_stream_set_rate(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum sample_frequency rate);
int sst_hsw_stream_set_bits(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum bitdepth bits);
int sst_hsw_stream_set_channels(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u8 channels);
int sst_hsw_stream_set_map_config(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 map, enum sst_hsw_channel_config config);
int sst_hsw_stream_set_style(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	enum sst_hsw_interleaving style);

/* Stream ALSA trigger operations */
int sst_hsw_stream_pause(struct sst_hsw *hsw, struct sst_hsw_stream *stream, int wait);
int sst_hsw_stream_resume(struct sst_hsw *hsw, struct sst_hsw_stream *stream, int wait);
int sst_hsw_stream_reset(struct sst_hsw *hsw, struct sst_hsw_stream *stream);

/* Stream pointer positions */
int sst_hsw_stream_get_read_pos(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 *position);
int sst_hsw_stream_get_write_pos(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 *position);
int sst_hsw_stream_set_write_position(struct sst_hsw *hsw, struct sst_hsw_stream *stream,
	u32 stage_id, u32 position);
int sst_hsw_get_dsp_position(struct sst_hsw *hsw, struct sst_hsw_stream *stream);

/* HW port config */
int sst_hsw_device_set_config(struct sst_hsw *hsw,
	enum sst_hsw_device_id dev, enum sst_hsw_device_mclk mclk,
	enum sst_hsw_device_mode mode, u32 clock_divider);

/* DX Config */
int sst_hsw_dx_set_state(struct sst_hsw *hsw,
	enum sst_hsw_dx_state state, struct sst_hsw_ipc_dx_reply *dx);
int sst_hsw_dx_get_state(struct sst_hsw *hsw, u32 item,
	u32 *offset, u32 *size, u32 *source);

/* init */
struct sst_hsw *sst_hsw_dsp_init(struct device *dev, struct sst_pdata *pdata);
void sst_hsw_dsp_free(struct sst_hsw *hsw);

#endif
