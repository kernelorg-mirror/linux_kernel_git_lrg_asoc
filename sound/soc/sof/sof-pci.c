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

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <sound/pcm.h>
#include <sound/sof-intel.h>
#include "sst-acpi.h"
#include "sst-dsp.h"
#include "sst-dsp-priv.h"
#include "../skylake/skl.h"

static int use_sof = 1; /* tmp enable SOF always atm */

struct sof_pci_machine_pdata sof_pci_dmic_data;
EXPORT_SYMBOL(sof_pci_dmic_data);

struct sst_pci_priv {
	struct pci_dev *pci;
	struct snd_sof_pdata *sof_pdata;
	struct sst_acpi_mach *mach;
	struct platform_device *pdev_pcm;
};

static void sst_pci_fw_cb(const struct firmware *fw, void *context)
{

	struct sst_pci_priv *priv = context;
	struct device *dev = &priv->pci->dev;
	struct snd_sof_pdata *sof_pdata = priv->sof_pdata;
	struct sst_acpi_mach *mach = priv->mach;

	sof_pdata->fw = fw;
	if (!fw) {
		dev_err(dev, "Cannot load firmware %s\n", mach->fw_filename);
		return;
	}

	/* register PCM and DAI driver */
	priv->pdev_pcm =
		platform_device_register_data(dev, "sof-audio", -1,
					      sof_pdata, SND_SOF_INTEL_SIZE);
	if (IS_ERR(priv->pdev_pcm)) {
		dev_err(dev, "Cannot register device sof-audio. Error %d\n",
			(int)PTR_ERR(priv->pdev_pcm));
	}

	return;
}

/* use sound open firmware */
static int sst_pci_sof_probe(struct pci_dev *pci,
		     const struct pci_device_id *pci_id)
{
	struct device *dev = &pci->dev;
	struct sst_acpi_mach *mach;
	struct sst_acpi_mach reef_blind;
	struct snd_sof_pdata *sof_pdata;
	struct sst_pci_priv *priv;
	int ret = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	sof_pdata = devm_kzalloc(dev, SND_SOF_INTEL_SIZE, GFP_KERNEL);
	if (sof_pdata == NULL)
		return -ENOMEM;

	ret = pci_enable_device(pci);
	if (ret < 0)
		return ret;

	ret = pci_request_regions(pci, "Audio DSP");
	if (ret < 0)
		return ret;
#if 0
//	bus->addr = pci_resource_start(pci, 0);
//	bus->remap_addr = pci_ioremap_bar(pci, 0);
//	if (bus->remap_addr == NULL) {
//		dev_err(bus->dev, "ioremap error\n");
//		return -ENXIO;
//	}

	pci_set_master(pci);
	synchronize_irq(pci->irq);

	/* allow 64bit DMA address if supported by H/W */
	if (!dma_set_mask(bus->dev, DMA_BIT_MASK(64))) {
		dma_set_coherent_mask(bus->dev, DMA_BIT_MASK(64));
	} else {
		dma_set_mask(bus->dev, DMA_BIT_MASK(32));
		dma_set_coherent_mask(bus->dev, DMA_BIT_MASK(32));
	}

	/*
	 * Clear bits 0-2 of PCI register TCSEL (at offset 0x44)
	 * TCSEL == Traffic Class Select Register, which sets PCI express QOS
	 * Ensuring these bits are 0 clears playback static on some HD Audio
	 * codecs.
	 * The PCI register TCSEL is defined in the Intel manuals.
	 */
	sof_pci_update_pci_byte(skl->pci, AZX_PCIREG_TCSEL, 0x07, 0);

	/*
	 * While performing reset, controller may not come back properly causing
	 * issues, so recommendation is to set CGCTL.MISCBDCGE to 0 then do reset
	 * (init chip) and then again set CGCTL.MISCBDCGE to 1
	 */
	sof_pci_enable_miscbdcge(bus->dev, false);
	ret = snd_hdac_bus_init_chip(bus, full_reset);
	sof_pci_enable_miscbdcge(bus->dev, true);

	device_disable_async_suspend(bus->dev);
#endif
	/* TODO: read NHLT */

	/* find machine */
	mach = sst_acpi_find_machine((struct sst_acpi_mach *)pci_id->driver_data);
	if (mach == NULL) {
		dev_err(dev, "No matching ASoC machine driver found - using blind\n");
		sof_pdata->drv_name = "reef-blind";
		mach->fw_filename = "intel/reef-byt.ri";
		mach = &reef_blind;
	}


	sof_pdata->id = pci_id->device;
	sof_pdata->name = pci_name(pci);
	sof_pdata->irq = pci->irq;
	priv->sof_pdata = sof_pdata;
	priv->mach = mach;

	/* BAR 0 is HDA controller */
	sof_pdata->elem[SND_SOF_INTEL_LPE_BASE].value = pci_resource_start(pci, 0);
	sof_pdata->elem[SND_SOF_INTEL_LPE_SIZE].value = pci_resource_len(pci, 0);

	pci_set_drvdata(pci, priv);

	/* register machine driver */
	sof_pdata->pdev_mach =
		platform_device_register_data(dev, mach->drv_name, -1,
					      sof_pdata, sizeof(*sof_pdata));
	if (IS_ERR(sof_pdata->pdev_mach))
		return PTR_ERR(sof_pdata->pdev_mach);

	/* continue SST probing after firmware is loaded */
	ret = request_firmware_nowait(THIS_MODULE, true, mach->fw_filename,
				      dev, GFP_KERNEL, priv, sst_pci_fw_cb);
	if (ret)
		platform_device_unregister(sof_pdata->pdev_mach);

	return ret;
}


static int sst_pci_sof_remove(struct pci_dev *pci)
{
	struct sst_pci_priv *priv = pci_get_drvdata(pci);
	struct snd_sof_pdata *sof_pdata = priv->sof_pdata;

	platform_device_unregister(sof_pdata->pdev_mach);
	if (!IS_ERR_OR_NULL(priv->pdev_pcm))
		platform_device_unregister(priv->pdev_pcm);
	release_firmware(sof_pdata->fw);

	return 0;
}


static const struct dev_pm_ops sof_pci_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(snd_soc_sof_suspend, snd_soc_sof_resume)
	SET_RUNTIME_PM_OPS(snd_soc_sof_runtime_suspend, snd_soc_sof_runtime_resume, NULL)
	.suspend_late = snd_sof_pci_suspend_late,
};

static int sof_pci_probe(struct pci_dev *pci,
		     const struct pci_device_id *pci_id)
{
	return sst_pci_sof_probe(pci, pci_id);
}

static void sof_pci_shutdown(struct pci_dev *pci)
{
	snd_soc_sof_shutdown(pci->dev);
}

static void sof_pci_remove(struct pci_dev *pci)
{
	snd_soc_sof_remove(pci->dev);
}

static struct sst_acpi_mach sof_bxt_machines[] = {
	{ "INT343A", "bxt_alc298s_i2s", "intel/reef-bxt.ri", NULL, NULL, NULL },
	{ "DLGS7219", "bxt_da7219_max98357a_i2s", "intel/reef-bxt.ri", NULL, NULL, NULL },
};


/* PCI IDs */
static const struct pci_device_id sof_pci_ids[] = {
	/* BXT-P */
	{ PCI_DEVICE(0x8086, 0x5a98),
		.driver_data = (unsigned long)&sof_bxt_machines},
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, sof_pci_ids);

/* pci_driver definition */
static struct pci_driver snd_soc_sof_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = sof_pci_ids,
	.probe = sof_pci_probe,
	.remove = sof_pci_remove,
	.shutdown = sof_pci_shutdown,
	.driver = {
		.pm = &sof_pci_pm,
	},
};
module_pci_driver(snd_soc_sof_pci_driver);
