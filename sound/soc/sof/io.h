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

#ifndef __SOUND_SOC_SOF_IO_H
#define __SOUND_SOC_SOF_IO_H

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <uapi/sound/sof-ipc.h>
#include "sof.h"

/* control */
static inline int snd_sof_dsp_run(struct snd_sof_dev *sof_dev)
{
	if (sof_dev->ops->run)
		return sof_dev->ops->run(sof_dev);
	else
		return 0;
}

static inline int snd_sof_dsp_stall(struct snd_sof_dev *sof_dev)
{
	if (sof_dev->ops->stall)
		return sof_dev->ops->stall(sof_dev);
	else
		return 0;
}

static inline int snd_sof_dsp_reset(struct snd_sof_dev *sof_dev)
{
	if (sof_dev->ops->reset)
		return sof_dev->ops->reset(sof_dev);
	else
		return 0;
}

/* power management */
static inline int snd_sof_dsp_resume(struct snd_sof_dev *sof_dev)
{
	if (sof_dev->ops->resume)
		return sof_dev->ops->resume(sof_dev);
	else
		return 0;
}

static inline int snd_sof_dsp_suspend(struct snd_sof_dev *sof_dev, int state)
{
	if (sof_dev->ops->suspend)
		return sof_dev->ops->suspend(sof_dev, state);
	else
		return 0;
}

static inline int snd_sof_dsp_set_clk(struct snd_sof_dev *sof_dev, u32 freq)
{
	if (sof_dev->ops->set_clk)
		return sof_dev->ops->set_clk(sof_dev, freq);
	else
		return 0;
}

/* register IO */
static inline void snd_sof_dsp_write(struct snd_sof_dev *sof_dev, u32 offset,
	u32 value)
{
	if (sof_dev->ops->write)
		sof_dev->ops->write(sof_dev, sof_dev->reg_base + offset, value);
}

static inline void snd_sof_dsp_write64(struct snd_sof_dev *sof_dev,
	u32 offset, u64 value)
{
	if (sof_dev->ops->write64)
		sof_dev->ops->write64(sof_dev, sof_dev->reg_base + offset, value);
}

static inline u32 snd_sof_dsp_read(struct snd_sof_dev *sof_dev, u32 offset)
{
	if (sof_dev->ops->read)
		return sof_dev->ops->read(sof_dev, sof_dev->reg_base + offset);
	else
		return 0;
}

static inline u64 snd_sof_dsp_read64(struct snd_sof_dev *sof_dev,
	u32 offset)
{
	if (sof_dev->ops->read64)
		return sof_dev->ops->read64(sof_dev, sof_dev->reg_base + offset);
	else
		return 0;
}

/* block IO */
static inline void snd_sof_dsp_block_read(struct snd_sof_dev *sof_dev,
		void *dest, void __iomem *src, size_t bytes)
{
	if (sof_dev->ops->block_read)
		sof_dev->ops->block_read(sof_dev, dest, src, bytes);
}

static inline void snd_sof_dsp_block_write(struct snd_sof_dev *sof_dev,
		void __iomem *dest, void *src, size_t bytes)
{
	if (sof_dev->ops->block_write)
		sof_dev->ops->block_write(sof_dev, dest, src, bytes);
}

/* mailbox */
static inline void snd_sof_dsp_mailbox_read(struct snd_sof_dev *sof_dev,
	void __iomem *addr, void *message, size_t bytes)
{
	if (sof_dev->ops->mailbox_read)
		sof_dev->ops->mailbox_read(sof_dev, addr, message, bytes);
}

static inline void snd_sof_dsp_mailbox_write(struct snd_sof_dev *sof_dev,
	void __iomem *addr, void *message, size_t bytes)
{
	if (sof_dev->ops->mailbox_write)
		sof_dev->ops->mailbox_write(sof_dev, addr, message, bytes);
}

/* ipc */
static inline int snd_sof_dsp_tx_msg(struct snd_sof_dev *sof_dev,
	struct snd_sof_ipc_msg *msg)
{
	if (sof_dev->ops->tx_msg)
		return sof_dev->ops->tx_msg(sof_dev, msg);
	else
		return 0;
}

static inline int snd_sof_dsp_rx_msg(struct snd_sof_dev *sof_dev,
	struct snd_sof_ipc_msg *msg)
{
	if (sof_dev->ops->rx_msg)
		return sof_dev->ops->rx_msg(sof_dev, msg);
	else
		return 0;
}

int snd_sof_dsp_update_bits_unlocked(struct snd_sof_dev *sdev, u32 offset,
				u32 mask, u32 value);

int snd_sof_dsp_update_bits64_unlocked(struct snd_sof_dev *sdev, u32 offset,
				u64 mask, u64 value);

/* This is for registers bits with attribute RWC */
void snd_sof_dsp_update_bits_forced_unlocked(struct snd_sof_dev *sdev, u32 offset,
				u32 mask, u32 value);

int snd_sof_dsp_update_bits(struct snd_sof_dev *sdev, u32 offset,
				u32 mask, u32 value);

int snd_sof_dsp_update_bits64(struct snd_sof_dev *sdev, u32 offset,
				u64 mask, u64 value);

/* This is for registers bits with attribute RWC */
void snd_sof_dsp_update_bits_forced(struct snd_sof_dev *sdev, u32 offset,
				u32 mask, u32 value);

int snd_sof_pci_update_bits_unlocked(struct snd_sof_dev *sdev, u32 offset,
				u32 mask, u32 value);

int snd_sof_pci_update_bits(struct snd_sof_dev *sdev, u32 offset,
				u32 mask, u32 value);

#endif
