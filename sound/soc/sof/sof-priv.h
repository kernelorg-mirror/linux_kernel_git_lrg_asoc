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

#ifndef __SOUND_SOC_SOF_PRIV_H
#define __SOUND_SOC_SOF_PRIV_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <uapi/sound/sof-ipc.h>
#include <uapi/sound/sof-fw.h>
#include <uapi/sound/asoc.h>

/* debug flags */
#define SOF_DBG_REGS	(1 << 1)
#define SOF_DBG_MBOX	(1 << 2)
#define SOF_DBG_TEXT	(1 << 3)
#define SOF_DBG_PCI	(1 << 4)

/* max BARs mmaped devices can use */
#define SND_SOF_BARS	8

/* time in ms for runtime suspend delay */
#define SND_SOF_SUSPEND_DELAY	2000

struct snd_sof_dev;
struct snd_sof_ipc_msg;
struct snd_sof_ipc;
struct snd_sof_debugfs_map;
struct snd_soc_tplg_ops;
struct snd_soc_component;

struct snd_sof_dsp_ops {

	/* probe and remove */
	int (*remove)(struct snd_sof_dev *sof_dev);
	int (*probe)(struct snd_sof_dev *sof_dev);

	/* DSP core boot / reset */
	int (*run)(struct snd_sof_dev *sof_dev);
	int (*stall)(struct snd_sof_dev *sof_dev);
	int (*reset)(struct snd_sof_dev *sof_dev);

	/* DSP PM */
	int (*suspend)(struct snd_sof_dev *sof_dev, int state);
	int (*resume)(struct snd_sof_dev *sof_dev);

	/* DSP clocking */
	int (*set_clk)(struct snd_sof_dev *sof_dev, u32 freq);

	/* Register IO */
	void (*write)(struct snd_sof_dev *sof_dev, void __iomem *addr,
		u32 value);
	u32 (*read)(struct snd_sof_dev *sof_dev, void __iomem *addr);
	void (*write64)(struct snd_sof_dev *sof_dev, void __iomem *addr,
		u64 value);
	u64 (*read64)(struct snd_sof_dev *sof_dev, void __iomem *addr);

	/* memcpy IO */
	void (*block_read)(struct snd_sof_dev *sof_dev,
		void *dest, const volatile void __iomem *src, size_t size);
	void (*block_write)(struct snd_sof_dev *sof_dev,
		volatile void __iomem *dest, const void *src, size_t size);

	/* doorbell */
	irqreturn_t (*irq_handler)(int irq, void *context);
	irqreturn_t (*irq_thread)(int irq, void *context);

	/* mailbox */
	void (*mailbox_read)(struct snd_sof_dev *sof_dev, void *dest,
		void __iomem *addr, size_t bytes);
	void (*mailbox_write)(struct snd_sof_dev *sof_dev, void *src,
		void __iomem *addr, size_t bytes);

	/* ipc */
	int (*tx_msg)(struct snd_sof_dev *sof_dev, struct snd_sof_ipc_msg *msg);
	int (*rx_msg)(struct snd_sof_dev *sof_dev, struct snd_sof_ipc_msg *msg);

	/* debug */
	const struct snd_sof_debugfs_map *debug_map;
	int debug_map_count;
	void (*dbg_dump)(struct snd_sof_dev *sof_dev, u32 flags);

	/* FW loading */
	int (*load_module)(struct snd_sof_dev *sof_dev,
		struct snd_sof_mod_hdr *hdr);

};

struct snd_sof_dfsentry {
	struct dentry *dfsentry;
	size_t size;
	void *buf;
	struct snd_sof_dev *sdev;
};

struct snd_sof_debugfs_map {
	const char *name;
	u32 bar;
	u32 offset;
	u32 size;
};

struct snd_sof_mailbox {
	void __iomem *base;
	size_t size;
};

struct snd_sof_pcm {
	int comp_id;
	struct snd_soc_tplg_pcm pcm;
	struct snd_dma_buffer page_table[2];	/* playback and capture */

	struct mutex mutex;
	struct list_head list;	/* list in snd_sof_dev */
};

struct snd_sof_ipc_msg {
	struct list_head list;

	/* message data */
	u32 header;
	void *msg_data;
	void *reply_data;
	size_t msg_size;
	size_t reply_size;

	wait_queue_head_t waitq;
	bool wait;
	bool complete;
};

struct snd_sof_hda_stream {
	void __iomem *pphc_addr;
	void __iomem *pplc_addr; // do we need this ?
	void __iomem *spib_addr;
	void __iomem *fifo_addr;
	void __iomem *drsm_addr;
	u32 dpib;
	u32 lpib;
};

#define SOF_HDA_PLAYBACK_STREAMS	8
#define SOF_HDA_CAPTURE_STREAMS		8

struct snd_sof_hda_dev {
	struct snd_sof_hda_stream pstream[SOF_HDA_PLAYBACK_STREAMS];
	struct snd_sof_hda_stream cstream[SOF_HDA_CAPTURE_STREAMS];
};

struct snd_sof_dev {
	struct device *dev;
	struct device *parent;
	spinlock_t spinlock;

	/* ASoC components */
	struct snd_soc_platform_driver plat_drv;

	wait_queue_head_t boot_wait;
	bool boot_complete;

	struct pci_dev *pci;

	struct snd_sof_pdata *pdata;
	const struct snd_sof_dsp_ops *ops;

	/* IPC */
	struct snd_sof_ipc *ipc;
	struct snd_sof_mailbox inbox;
	struct snd_sof_mailbox outbox;
	u64 irq_status;
	int ipc_irq;

	/* front end - platform specific */
	union {
		struct snd_sof_hda_dev hda;
	};

	/* memory bases for mmaped DSPs - set by dsp_init() */
	void __iomem *bar[SND_SOF_BARS];		/* DSP base address */

	struct dentry *debugfs_root;

	/* firmware loader */
	int cl_bar;

	/* topology */
	struct snd_soc_tplg_ops *tplg_ops;
	struct list_head pcm_list;
	struct list_head kcontrol_list;
	struct snd_soc_component *component;

	/* IPC timeouts in ms */
	int ipc_timeout;
	int boot_timeout;

	void *private;			/* core does not touch this */
};

/*
 * Device Level.
 */

void snd_sof_shutdown(struct device *dev);
int snd_sof_runtime_suspend(struct device *dev);
int snd_sof_runtime_resume(struct device *dev);
int snd_sof_resume(struct device *dev);
int snd_sof_suspend(struct device *dev);
int snd_sof_suspend_late(struct device *dev);

void snd_sof_new_platform_drv(struct snd_sof_dev *sdev);

/*
 * Firmware loading.
 */
int snd_sof_load_firmware(struct snd_sof_dev *sdev,
	const struct firmware *fw);
int snd_sof_run_firmware(struct snd_sof_dev *sdev);
int snd_sof_parse_module_memcpy(struct snd_sof_dev *sdev,
	struct snd_sof_mod_hdr *module);
void snd_sof_fw_unload(struct snd_sof_dev *sdev);


/*
 * IPC APIs.
 */

struct snd_sof_ipc *snd_sof_ipc_init(struct snd_sof_dev *sdev);
void snd_sof_ipc_free(struct snd_sof_dev *sdev);
void snd_sof_ipc_process_reply(struct snd_sof_dev *sdev, u32 msg_id);
void snd_sof_ipc_process_notification(struct snd_sof_dev *sdev, u32 msg_id);
void snd_sof_ipc_process_msgs(struct snd_sof_dev *sdev);
int snd_sof_ipc_stream_pcm_params(struct snd_sof_dev *sdev,
	struct sof_ipc_pcm_params *params);

/*
 * Topology.
 */
int snd_sof_init_topology(struct snd_sof_dev *sdev,
	struct snd_soc_tplg_ops *ops);
int snd_sof_load_topology(struct snd_sof_dev *sdev, const char *file);
void snd_sof_free_topology(struct snd_sof_dev *sdev);
	

/*
 * Trace/debug
 */
int snd_sof_init_trace(struct snd_sof_dev *sdev);
int snd_sof_dbg_init(struct snd_sof_dev *sdev);
void snd_sof_free_debug(struct snd_sof_dev *sdev);

/*
 * Platform specific ops.
 */

extern struct snd_sof_dsp_ops snd_sof_byt_ops;
extern struct snd_sof_dsp_ops snd_sof_cht_ops;
extern struct snd_sof_dsp_ops snd_sof_hsw_ops;
extern struct snd_sof_dsp_ops snd_sof_bdw_ops;
extern struct snd_sof_dsp_ops snd_sof_bxt_ops;

/*
 * ASoC components.
 */
extern const struct snd_soc_component_driver sof_dai_component;

#endif
