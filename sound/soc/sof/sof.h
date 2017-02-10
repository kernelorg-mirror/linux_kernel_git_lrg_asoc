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

#ifndef __SOUND_SOC_SOF_H
#define __SOUND_SOC_SOF_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <uapi/sound/sof-ipc.h>

/* debug flags */
#define SOF_DBG_REGS	(1 << 1)
#define SOF_DBG_MBOX	(1 << 2)

struct snd_sof_dev;
struct snd_sof_ipc_msg;
struct snd_sof_ipc;
struct snd_sof_debugfs_map;

struct snd_sof_dsp_ops {

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
		u32 *dest, volatile u32 __iomem *src, size_t bytes);
	void (*block_write)(struct snd_sof_dev *sof_dev,
		volatile u32 __iomem *dest, u32 *src, size_t bytes);

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

};

struct snd_sof_dfsentry {
	struct dentry *dfsentry;
	size_t size;
	void *buf;
	struct snd_sof_dev *sdev;
};

struct snd_sof_debugfs_map {
	const char *name;
	u32 offset;
	u32 size;
};

struct snd_sof_ipc_msg {
	void *data;
	size_t size;
	u32 header;
};

struct snd_sof_mailbox {
	void __iomem *base;
	size_t size;
};

struct snd_sof_dev {
	struct device *dev;
	spinlock_t spinlock;

	struct pci_dev *pci;

	struct snd_sof_ipc *ipc;
	struct snd_sof_mailbox inbox;
	struct snd_sof_mailbox outbox;

	/* memroy bases for mmaped DSPs - set by dsp_init() */
	void __iomem *pci_cfg;		/* PCI config space */
	void __iomem *dsp_base;		/* DSP base address */
	void __iomem *reg_base;		/* base address of control regs */
	void __iomem *iram_base;	/* instruction ram - or ram base */
	void __iomem *dram_base;	/* data ram */
	void __iomem *mbox_base;	/* mailbox ram */
	void __iomem *fw_mmap;		/* FW memory mapping to host */

	struct dentry *debugfs_root;

	const struct snd_sof_dsp_ops *ops;
	void *private;			/* core does not touch this */
};


void snd_sof_ipc_process_reply(struct snd_sof_dev *sdev, u32 msg_id);

void snd_sof_ipc_process_notification(struct snd_sof_dev *sdev, u32 msg_id);

void snd_sof_ipc_process_msgs(struct snd_sof_dev *sdev);


#endif
