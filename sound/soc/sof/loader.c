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

#if 0
static int hsw_parse_module(struct snd_soc_sof *sof,
	struct fw_module_header *module)
{
	struct dma_block_info *block;
	int count, ret;
	void __iomem *ram;


	dev_dbg(sof->dev, "new module sign 0x%s size 0x%x blocks 0x%x type 0x%x\n",
		module->signature, module->mod_size,
		module->blocks, module->type);
	dev_dbg(sof->dev, " entrypoint 0x%x\n", module->entry_point);
	dev_dbg(sof->dev, " persistent 0x%x scratch 0x%x\n",
		module->info.persistent_size, module->info.scratch_size);

	block = (void *)module + sizeof(*module);

	for (count = 0; count < module->blocks; count++) {

		if (block->size <= 0) {
			dev_err(sof->dev,
				"error: block %d size invalid\n", count);
			return -EINVAL;
		}

		switch (block->type) {
		case SST_HSW_IRAM:
			ram = dsp->addr.lpe;
			mod->offset =
				block->ram_offset + dsp->addr.iram_offset;
			mod->type = SST_MEM_IRAM;
			break;
		case SST_HSW_DRAM:
		case SST_HSW_REGS:
			ram = dsp->addr.lpe;
			mod->offset = block->ram_offset + dsp->addr.dram_offset;
			mod->type = SST_MEM_DRAM;
			break;
		default:
			dev_err(sof->dev, "error: bad type 0x%x for block 0x%x\n",
				block->type, count);
			return -EINVAL;
		}

		mod->size = block->size;
		mod->data = (void *)block + sizeof(*block);
		mod->data_offset = mod->data - fw->dma_buf;

		dev_dbg(sof->dev, "module block %d type 0x%x "
			"size 0x%x ==> ram %p offset 0x%x\n",
			count, mod->type, block->size, ram,
			block->ram_offset);

		// COPY block

		block = (void *)block + sizeof(*block) + block->size;
	}
	mod->state = SST_MODULE_STATE_LOADED;

	return 0;
}

static int hsw_parse_fw_image(struct snd_soc_sof *sof)
{
	struct fw_header *header;
	struct fw_module_header *module;
	int ret, count;

	/* Read the header information from the data pointer */
	header = (struct fw_header *)sst_fw->dma_buf;

	/* verify FW */
	if ((strncmp(header->signature, SST_HSW_FW_SIGN, 4) != 0) ||
		(sst_fw->size != header->file_size + sizeof(*header))) {
		dev_err(sof->dev, "error: invalid fw sign/filesize mismatch got 0x%x expected 0x%lx\n",
			sst_fw->size, header->file_size + sizeof(*header));
		return -EINVAL;
	}

	dev_dbg(sof->dev, "header size=0x%x modules=0x%x fmt=0x%x size=%zu\n",
		header->file_size, header->modules,
		header->file_format, sizeof(*header));

	/* parse each module */
	module = (void *)sst_fw->dma_buf + sizeof(*header);
	for (count = 0; count < header->modules; count++) {

		/* module */
		ret = hsw_parse_module(sof, module);
		if (ret < 0) {
			dev_err(sof->dev, "error: invalid module %d\n", count);
			return ret;
		}
		module = (void *)module + sizeof(*module) + module->mod_size;
	}

	return 0;
}

#endif
