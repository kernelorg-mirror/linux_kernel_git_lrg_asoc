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

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <sound/core.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include "sof-priv.h"
#include "ops.h"

#if 0
int sst_hsw_dsp_init(struct device *dev, struct sst_pdata *pdata)
{
	struct sst_hsw_ipc_fw_version version;
	struct sst_hsw *hsw;
	struct sst_generic_ipc *ipc;
	struct sst_dsp_device *dsp_dev;
	int ret;

	dev_dbg(dev, "initialising Audio DSP IPC\n");

	hsw = devm_kzalloc(dev, sizeof(*hsw), GFP_KERNEL);
	if (hsw == NULL)
		return -ENOMEM;

	hsw->dev = dev;

	ipc = &hsw->ipc;
	ipc->dev = dev;

	/* set up ops depending on hardware */
	switch (pdata->id) {
	case SST_DEV_ID_BYT:
		/* Baytrail */
		dsp_dev = &byt_dev;
		dsp_dev->thread_context = hsw;
		ipc->ops.tx_msg = byt_tx_msg;
		ipc->ops.shim_dbg = byt_shim_dbg;
		ipc->ops.tx_data_copy = hsw_tx_data_copy;
		ipc->ops.reply_msg_match = hsw_reply_msg_match;
		ipc->ops.is_dsp_busy = byt_is_dsp_busy;
		ipc->ops.dsp_notify = byt_notify;
		break;
	case SST_DEV_ID_LYNX_POINT:
	case SST_DEV_ID_WILDCAT_POINT:
		/* Haswell / Broadwell */
		dsp_dev = &hsw_dev;
		dsp_dev->thread_context = hsw;
		ipc->ops.tx_msg = hsw_tx_msg;
		ipc->ops.shim_dbg = hsw_shim_dbg;
		ipc->ops.tx_data_copy = hsw_tx_data_copy;
		ipc->ops.reply_msg_match = hsw_reply_msg_match;
		ipc->ops.is_dsp_busy = hsw_is_dsp_busy;
		ipc->ops.dsp_notify = hsw_notify;
		break;
	default:
		ret = -EINVAL;
		dev_err(dev, "error: unsupported DSP ID 0x%x\n", pdata->id);
		goto ipc_init_err;
	}

	ipc->tx_data_max_size = IPC_MAX_MAILBOX_BYTES;
	ipc->rx_data_max_size = IPC_MAX_MAILBOX_BYTES;

	ret = sst_ipc_init(ipc);
	if (ret != 0)
		goto ipc_init_err;

	INIT_LIST_HEAD(&hsw->stream_list);
	init_waitqueue_head(&hsw->boot_wait);

	/* init SST shim */
	hsw->dsp = sst_dsp_new(dev, dsp_dev, pdata);
	if (hsw->dsp == NULL) {
		ret = -ENODEV;
		goto dsp_new_err;
	}

	ipc->dsp = hsw->dsp;

	/* allocate DMA buffer for context storage */
	hsw->dx_context = dma_alloc_coherent(hsw->dsp->dma_dev,
		SST_HSW_DX_CONTEXT_SIZE, &hsw->dx_context_paddr, GFP_KERNEL);
	if (hsw->dx_context == NULL) {
		ret = -ENOMEM;
		goto dma_err;
	}

	/* keep the DSP in reset state for base FW loading */
	sst_dsp_reset(hsw->dsp);

	/* load base module and other modules in base firmware image */
	ret = sst_hsw_module_load(hsw, SST_HSW_MODULE_BASE_FW, 0, "Base");
	if (ret < 0)
		goto fw_err;

	/* try to load module waves */
	sst_hsw_module_load(hsw, SST_HSW_MODULE_WAVES, 0, "intel/IntcPP01.bin");

	/* allocate scratch mem regions */
	ret = sst_block_alloc_scratch(hsw->dsp);
	if (ret < 0)
		goto boot_err;

	/* init param buffer */
	sst_hsw_reset_param_buf(hsw);

	/* wait for DSP boot completion */
	sst_dsp_boot(hsw->dsp);
	ret = wait_event_timeout(hsw->boot_wait, hsw->boot_complete,
		msecs_to_jiffies(IPC_BOOT_MSECS));
	if (ret == 0) {
		ret = -EIO;
		ipc->ops.shim_dbg(ipc, "DSP boot timeout");
		goto boot_err;
	}

	hsw_debugfs_init(hsw);

	/* init module state after boot */
	sst_hsw_init_module_state(hsw);

	/* get the FW version */
	sst_hsw_fw_get_version(hsw, &version);

	/* get the globalmixer */
	ret = sst_hsw_mixer_get_info(hsw);
	if (ret < 0) {
		dev_err(hsw->dev, "error: failed to get stream info\n");
		goto boot_err;
	}

	pdata->dsp = hsw;
	return 0;

boot_err:
	sst_dsp_reset(hsw->dsp);
	sst_fw_free_all(hsw->dsp);
fw_err:
	dma_free_coherent(hsw->dsp->dma_dev, SST_HSW_DX_CONTEXT_SIZE,
			hsw->dx_context, hsw->dx_context_paddr);
dma_err:
	sst_dsp_free(hsw->dsp);
dsp_new_err:
	sst_ipc_fini(ipc);
ipc_init_err:
	return ret;
}
EXPORT_SYMBOL_GPL(sst_hsw_dsp_init);

void sst_hsw_dsp_free(struct device *dev, struct sst_pdata *pdata)
{
	struct sst_hsw *hsw = pdata->dsp;

	snd_dma_free_pages(&hsw->trace_dma_descriptor);
	snd_dma_free_pages(&hsw->dtrace_buffer);
	sst_dsp_reset(hsw->dsp);
	sst_fw_free_all(hsw->dsp);
	dma_free_coherent(hsw->dsp->dma_dev, SST_HSW_DX_CONTEXT_SIZE,
			hsw->dx_context, hsw->dx_context_paddr);
	sst_dsp_free(hsw->dsp);
	sst_ipc_fini(&hsw->ipc);
}
EXPORT_SYMBOL_GPL(sst_hsw_dsp_free);

#endif


void snd_soc_sof_shutdown(struct device *dev)
{
}
EXPORT_SYMBOL(snd_soc_sof_shutdown);

static int sof_probe(struct platform_device *pdev)
{
	struct snd_sof_pdata *plat_data = dev_get_platdata(&pdev->dev);
	struct snd_sof_dev *sdev;
	int ret;

	sdev = devm_kzalloc(&pdev->dev, sizeof(*sdev), GFP_KERNEL);
	if (sdev == NULL)
		return -ENOMEM;

	sdev->dev = &pdev->dev;
	sdev->parent = plat_data->dev;
	sdev->ops = plat_data->machine->ops;
	sdev->pdata = plat_data;
	INIT_LIST_HEAD(&sdev->pcm_list);
	INIT_LIST_HEAD(&sdev->kcontrol_list);
	dev_set_drvdata(&pdev->dev, sdev);

	/* probe the DSP hardware */
	ret = snd_soc_sof_probe(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to probe DSP %d\n", ret);
		return ret;
	}

	/* register any debug/trace capabilities */
	ret = snd_soc_sof_init_debug(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to init DSP trace/debug %d\n", ret);
		return ret;
	}

	/* init the IPC */
	sdev->ipc = snd_sof_ipc_init(sdev);
	if (sdev->ipc < 0) {
		dev_err(sdev->dev, "failed to init DSP IPC %d\n", ret);
		return ret;
	}

	/* load the firmware */
	ret = snd_soc_sof_load_firmware(sdev, plat_data->fw);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to load DSP firmware %d\n", ret);
		return ret;
	}

	/* boot the firmware */
	ret = snd_soc_sof_run_firmware(sdev);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to boot DSP firmware %d\n", ret);
		return ret;
	}

	/* load the topology */
	ret = snd_soc_sof_load_topology(sdev, plat_data->machine->tplg_filename);
	if (ret < 0) {
		dev_err(sdev->dev, "failed to load DSP topology %d\n", ret);
		return ret;
	}

	/* now register audio DSP platform driver */
	ret = snd_soc_register_platform(&pdev->dev, &sof_soc_platform);
	if (ret < 0) {
		dev_err(sdev->dev,
			"failed to register DSP platform driver %d\n", ret);
		return ret;
	}


	return 0;
}

static int sof_remove(struct platform_device *pdev)
{
#if 0
	struct sst_pdata *sst_pdata = dev_get_platdata(&pdev->dev);

	snd_soc_unregister_platform(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);
	sst_hsw_dsp_free(&pdev->dev, sst_pdata);
#endif
	return 0;
}


static struct platform_driver sof_driver = {
	.driver = {
		.name = "sof-audio",
		//.pm = &sof_pm,
	},

	.probe = sof_probe,
	.remove = sof_remove,
};
module_platform_driver(sof_driver);

MODULE_AUTHOR("Liam Girdwood");
MODULE_DESCRIPTION("Sound Open Firmware (Reef) Core");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS("platform:sof-audio");
