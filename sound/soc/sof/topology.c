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
#include <sound/soc-topology.h>
#include <sound/soc.h>
#include <uapi/sound/sof-ipc.h>
#include "sof-priv.h"

/* external kcontrol init - used for any driver specific init */
static int sof_control_load(struct snd_soc_component *scomp,
	struct snd_kcontrol_new *kc, struct snd_soc_tplg_ctl_hdr *hdr)
{
	return 0;
}

static int sof_control_unload(struct snd_soc_component *scomp,
	struct snd_soc_dobj *dobj)
{
	return 0;
}

/* external widget init - used for any driver specific init */
static int sof_widget_load(struct snd_soc_component *scomp,
	struct snd_soc_dapm_widget *w,
	struct snd_soc_tplg_dapm_widget *tw)
{
	return 0;
}

static int sof_widget_unload(struct snd_soc_component *scomp,
	struct snd_soc_dobj *dobj)
{
	return 0;
}

/* FE DAI - used for any driver specific init */
static int sof_dai_load(struct snd_soc_component *scomp,
	struct snd_soc_dai_driver *dai_drv,
	struct snd_soc_tplg_pcm *pcm)
{
	struct snd_sof_dev *sdev = snd_soc_component_get_drvdata(scomp);
	struct snd_sof_pcm *spcm;

	spcm = kzalloc(sizeof(*spcm), GFP_KERNEL);
	if (spcm == NULL)
		return -ENOMEM;

	spcm->pcm = *pcm;
	spcm->comp_id = pcm->pcm_id;
	dai_drv->dobj.private = sdev;
	mutex_init(&spcm->mutex);
	list_add(&spcm->list, &sdev->pcm_list);
		
	return 0;
}

static int sof_dai_unload(struct snd_soc_component *scomp,
	struct snd_soc_dobj *dobj)
{
	struct snd_sof_pcm *spcm = dobj->private;

	list_del(&spcm->list);
	kfree(&spcm);

	return 0;
}

/* DAI link - used for any driver specific init */
static int sof_link_load(struct snd_soc_component *scomp,
	struct snd_soc_dai_link *link)
{
	return 0;
}

static int sof_link_unload(struct snd_soc_component *scomp,
	struct snd_soc_dobj *dobj)
{
	return 0;
}

/* completion - called at completion of firmware loading */
static void sof_complete(struct snd_soc_component *scomp)
{
}

/* manifest - optional to inform component of manifest */
static int sof_manifest(struct snd_soc_component *scomp,
	struct snd_soc_tplg_manifest *man)
{
	return 0;
}

/* vendor specific kcontrol handlers available for binding */
static const struct snd_soc_tplg_kcontrol_ops sof_io_ops[] = {
{},
};

/* vendor specific bytes ext handlers available for binding */
static const struct snd_soc_tplg_bytes_ext_ops sof_bytes_ext_ops[] = {
{},
};

static struct snd_soc_tplg_ops sof_tplg_ops = {

	/* external kcontrol init - used for any driver specific init */
	.control_load	= sof_control_load,
	.control_unload	= sof_control_unload,

	/* external widget init - used for any driver specific init */
	.widget_load	= sof_widget_load,
	.widget_unload	= sof_widget_unload,

	/* FE DAI - used for any driver specific init */
	.dai_load	= sof_dai_load,
	.dai_unload	= sof_dai_unload,

	/* DAI link - used for any driver specific init */
	.link_load	= sof_link_load,
	.link_unload	= sof_link_unload,

	/* completion - called at completion of firmware loading */
	.complete	= sof_complete,

	/* manifest - optional to inform component of manifest */
	.manifest	= sof_manifest,

	/* vendor specific kcontrol handlers available for binding */
	.io_ops 	= sof_io_ops,
	.io_ops_count	= ARRAY_SIZE(sof_io_ops),

	/* vendor specific bytes ext handlers available for binding */
	.bytes_ext_ops	= sof_bytes_ext_ops,
	.bytes_ext_ops_count	= ARRAY_SIZE(sof_bytes_ext_ops),
};

int snd_soc_sof_init_topology(struct snd_sof_dev *sdev,
	struct snd_soc_tplg_ops *ops)
{
	/* TODO: support linked list of topologies */
	sdev->tplg_ops = ops;
	return 0;
}
EXPORT_SYMBOL(snd_soc_sof_init_topology);

int snd_soc_sof_load_topology(struct snd_sof_dev *sdev, const char *file)
{
	const struct firmware *fw;
	struct snd_soc_tplg_hdr *hdr;
	int ret;

	ret = request_firmware(&fw, file, sdev->dev);
	if (ret < 0) {
		dev_err(sdev->dev, "error: tplg %s load failed with %d\n",
				file, ret);
		return ret;
	}

	hdr = (struct snd_soc_tplg_hdr *)fw->data;
	ret = snd_soc_tplg_component_load(sdev->component,
					&sof_tplg_ops, fw, hdr->index);
	if (ret < 0) {
		dev_err(sdev->dev, "error: tplg component load failed %d\n", ret);
		release_firmware(fw);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(snd_soc_sof_load_topology);
