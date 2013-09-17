/*
 *  haswell-ult.c - Intel Haswell Audio
 *
 *  Copyright (C) 2013	Intel Corp
 *
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include <linux/pci.h>

#include "sst_dsp.h"
#include "sst_hsw_ipc.h"
#include "sst_hsw_pcm.h"

#if defined CONFIG_HSW_ULT_RT286
#include "../codecs/rt286.h"
#else
#include "../codecs/rt5640.h"
#endif

#define SST_HSWULT_PCI_ID	0x9c36

/* card private data */
struct haswell_data {
	struct platform_device *hsw_pcm_pdev;
	struct sst_hsw *hsw;
};

//static unsigned int hs_switch;
static unsigned int lo_dac;

/* sound card controls */
static const char *headset_switch_text[] = {"Earpiece", "Headset"};
static const char *lo_text[] = {"Headset", "IHF", "None"};
static const struct soc_enum headset_enum =
	SOC_ENUM_SINGLE_EXT(2, headset_switch_text);
static const struct soc_enum lo_enum =
	SOC_ENUM_SINGLE_EXT(3, lo_text);

#if 0
/* jack detection voltage zones */
static void hsw_jack_enable_mic_bias(struct snd_soc_codec *codec)
{
	snd_soc_dapm_force_enable_pin(&codec->dapm, "micbias1");
	snd_soc_dapm_sync(&codec->dapm);
}

static void hsw_jack_disable_mic_bias(struct snd_soc_codec *codec)
{
	//TODO: There's micbias2 in codec driver but not used for hw
	snd_soc_dapm_disable_pin(&codec->dapm, "micbias1");
	snd_soc_dapm_sync(&codec->dapm);
}
#endif

static int headset_get_switch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	return 0;
}

static int headset_set_switch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
#if 0
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (ucontrol->value.integer.value[0]) {
		pr_debug("hs_set HS path\n");
		snd_soc_dapm_enable_pin(&codec->dapm, "Headphones");
		snd_soc_dapm_disable_pin(&codec->dapm, "EPOUT");
	} else {
		pr_debug("hs_set EP path\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphones");
		snd_soc_dapm_enable_pin(&codec->dapm, "EPOUT");
	}
	snd_soc_dapm_sync(&codec->dapm);
#endif
	return 0;
}

static int lo_get_switch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = lo_dac;
	return 0;
}

static int lo_set_switch(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *ucontrol)
{
#if 0
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (ucontrol->value.integer.value[0] == lo_dac)
		return 0;

	/* we dont want to work with last state of lineout so just enable all
	 * pins and then disable pins not required
	 */
	//lo_enable_out_pins(codec);
	switch (ucontrol->value.integer.value[0]) {
	case 1:
		pr_debug("set hs  path\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "Headphones");
		snd_soc_dapm_disable_pin(&codec->dapm, "EPOUT");
		break;

	case 2:
		pr_debug("set spkr path\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "IHFOUTL");
		snd_soc_dapm_disable_pin(&codec->dapm, "IHFOUTR");
		break;

	case 3:
		pr_debug("set null path\n");
		snd_soc_dapm_disable_pin(&codec->dapm, "LINEOUTL");
		snd_soc_dapm_disable_pin(&codec->dapm, "LINEOUTR");
		break;
	}
	snd_soc_dapm_sync(&codec->dapm);
	lo_dac = ucontrol->value.integer.value[0];
#endif
	return 0;
}

static const struct snd_kcontrol_new hsw_snd_controls[] = {
	SOC_ENUM_EXT("Playback Switch", headset_enum,
			headset_get_switch, headset_set_switch),
	SOC_ENUM_EXT("Lineout Mux", lo_enum,
			lo_get_switch, lo_set_switch),
};

static const struct snd_soc_dapm_widget hsw_widgets[] = {
	SND_SOC_DAPM_HP("Headphones", NULL),
	SND_SOC_DAPM_MIC("Mic", NULL),
};

static const struct snd_soc_dapm_route hsw_map[] = {
#if defined CONFIG_HSW_ULT_RT286
	{"Headphones", NULL, "SPOR"},
	{"Headphones", NULL, "SPOL"},
	{"MIC1", NULL, "Mic"},
#else
	{"Headphones", NULL, "HPOR"},
	{"Headphones", NULL, "HPOL"},
	{"IN2P", NULL, "Mic"},
#endif
	/* CODEC BE connections */
	{"SSP0 CODEC IN", NULL, "AIF1 Capture"},
	{"AIF1 Playback", NULL, "SSP0 CODEC OUT"},
};

static int hswult_ssp0_fixup(struct snd_soc_pcm_runtime *rtd,
			struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);

	/* The ADSP will covert the FE rate to 48k, stereo */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP0 to 16 bit */
	snd_mask_set(&params->masks[SNDRV_PCM_HW_PARAM_FORMAT -
				    SNDRV_PCM_HW_PARAM_FIRST_MASK],
				    SNDRV_PCM_FORMAT_S16_LE);
	return 0;
}

static int haswell_startup(struct snd_pcm_substream *substream)
{
	return 0;
}

static void haswell_shutdown(struct snd_pcm_substream *substream)
{
}

static int haswell_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	/* Set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec DAI configuration\n");
		return ret;
	}

#if defined CONFIG_HSW_ULT_RT286
	ret = snd_soc_dai_set_sysclk(codec_dai, RT286_SCLK_S_MCLK, 24000000,
		SND_SOC_CLOCK_IN);
#else
	ret = snd_soc_dai_set_sysclk(codec_dai, RT5640_SCLK_S_MCLK, 12288000,
		SND_SOC_CLOCK_IN);
#endif
	if (ret < 0) {
		dev_err(rtd->dev, "can't set codec sysclk configuration\n");
		return ret;
	}
#if defined CONFIG_HSW_ULT_RT5640
	snd_soc_update_bits(rtd->codec, 0x83, 0xffff, 0x8000);
#endif
	return ret;
}

static struct snd_soc_ops haswell_ops = {
	.startup = haswell_startup,
	.hw_params = haswell_hw_params,
	.shutdown = haswell_shutdown,
};

static int haswell_compr_set_params(struct snd_compr_stream *compr)
{
	return 0;
}

static struct snd_soc_compr_ops haswell_compr_ops = {
	.set_params = haswell_compr_set_params,
};

static int haswell_rtd_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	struct sst_hsw_pcm *hsw_pcm = dev_get_platdata(rtd->platform->dev);
	struct sst_hsw *hsw = hsw_pcm->hsw;
	int ret;

	/* Set ADSP SSP port settings */
	ret = sst_hsw_device_set_config(hsw, SST_HSW_DEVICE_SSP_0,
		SST_HSW_DEVICE_MCLK_FREQ_24_MHZ, SST_HSW_DEVICE_CLOCK_MASTER, 9);
	if (ret < 0) {
		dev_err(rtd->dev, "failed to set device config\n");
		return ret;
	}

	//TODO: Add Jack detection here
	snd_soc_dapm_new_controls(dapm, hsw_widgets, ARRAY_SIZE(hsw_widgets));

	/* Set up the map */
	snd_soc_dapm_add_routes(dapm, hsw_map, ARRAY_SIZE(hsw_map));

	/* always connected */
	snd_soc_dapm_enable_pin(dapm, "Headphones");
	snd_soc_dapm_enable_pin(dapm, "Mic");

	ret = snd_soc_add_codec_controls(codec, hsw_snd_controls,
				ARRAY_SIZE(hsw_snd_controls));
	if (ret) {
		pr_err("soc_add_controls failed %d", ret);
		return ret;
	}

	//TODO: Disable unused pins atm
//	snd_soc_dapm_disable_pin(dapm, "Headphones");
//	snd_soc_dapm_disable_pin(dapm, "LINEOUTL");
//	snd_soc_dapm_disable_pin(dapm, "LINEOUTR");

//	snd_soc_dapm_disable_pin(dapm, "LINEINL");
//	snd_soc_dapm_disable_pin(dapm, "LINEINR");
	return 0;
}

/* haswell digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link haswell_dais[] = {
	/* Front End DAI links */
	{
		.name = "System",
		.stream_name = "System Playback",
		.cpu_dai_name = "System Pin",
		.platform_name = "hsw-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.init = haswell_rtd_init,
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
	},
	{
		.name = "Offload0",
		.stream_name = "Offload0 Playback",
		.cpu_dai_name = "Offload0 Pin",
		.platform_name = "hsw-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.compr_ops = &haswell_compr_ops,
	},
	{
		.name = "Offload1",
		.stream_name = "Offload1 Playback",
		.cpu_dai_name = "Offload1 Pin",
		.platform_name = "hsw-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_playback = 1,
		.compr_ops = &haswell_compr_ops,
	},
	{
		.name = "Loopback",
		.stream_name = "Loopback",
		.cpu_dai_name = "Loopback Pin",
		.platform_name = "hsw-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
	},
	{
		.name = "Capture",
		.stream_name = "Capture",
		.cpu_dai_name = "Capture Pin",
		.platform_name = "hsw-pcm-audio",
		.dynamic = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.trigger = {SND_SOC_DPCM_TRIGGER_POST, SND_SOC_DPCM_TRIGGER_POST},
		.dpcm_capture = 1,
	},

	/* Back End DAI links */
	{
		/* SSP0 - Codec */
		.name = "Codec",
		.be_id = 0,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "snd-soc-dummy",
		.no_pcm = 1,
#if defined CONFIG_HSW_ULT_RT286
		.codec_name = "rt286.0-001c",
		.codec_dai_name = "rt286-aif1",
#else
		.codec_name = "rt5640.0-001c",
		.codec_dai_name = "rt5640-aif1",
#endif
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		.be_hw_params_fixup = hswult_ssp0_fixup,
		.ops = &haswell_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
	{
		/* SSP1 - BT */
		.name = "SSP1-Codec",
		.be_id = 1,
		.cpu_dai_name = "snd-soc-dummy-dai",
		.platform_name = "snd-soc-dummy",
		.no_pcm = 1,
		.codec_name = "snd-soc-dummy",
		.codec_dai_name = "snd-soc-dummy-dai",
		.ignore_suspend = 1,
		.ignore_pmdown_time = 1,
		//.be_hw_params_fixup = hswult_ssp1_fixup,
	},
};

/* haswell audio machine driver */
static struct snd_soc_card haswell = {
	.name = "Haswell-ULT",
	.owner = THIS_MODULE,
	.dai_link = haswell_dais,
	.num_links = ARRAY_SIZE(haswell_dais),
};

static acpi_status hsw_audio_walk_resources(struct acpi_resource *res,
	void *context)
{
	struct sst_pdata *pdata = context;
	struct acpi_resource_extended_irq *pirq;
	struct acpi_resource_fixed_memory32 *pmem;

	switch (res->type) {
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		pirq = &res->data.extended_irq;
		pdata->irq = pirq->interrupts[0];
		return AE_OK;

	case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
		pmem = &res->data.fixed_memory32;
		pdata->address[pdata->num_regions] = pmem->address;
		pdata->length[pdata->num_regions] = pmem->address_length;
		pdata->num_regions++;
		return AE_OK;

	default:
	case ACPI_RESOURCE_TYPE_END_TAG:
		return AE_OK;
	}

	return AE_CTRL_TERMINATE;
}

static int hsw_audio_add(struct acpi_device *acpi)
{
	struct snd_soc_card *card = &haswell;
	struct haswell_data *pdata;
	struct sst_pdata sst_pdata;
	struct sst_hsw_pcm *pcm_plat_data;
	struct device *dev = &acpi->dev;
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (pdata == NULL)
		return -ENOMEM;

	/* this is managed by the platform driver core */
	pcm_plat_data = kzalloc(sizeof(*pcm_plat_data), GFP_KERNEL);
	if (pcm_plat_data == NULL)
		return -ENOMEM;

	memset(&sst_pdata, 0, sizeof(struct sst_pdata));
	acpi_walk_resources(acpi->handle, METHOD_NAME__CRS,
			    hsw_audio_walk_resources, &sst_pdata);

	/* initialise IPC and DSP */
	pdata->hsw = sst_hsw_dsp_init(dev, &sst_pdata);
	if (pdata->hsw == NULL) {
		kfree(pcm_plat_data);
		return -ENODEV;
	}

	/* register haswell PCM and DAI driver */
	pcm_plat_data->hsw = pdata->hsw;
	pdata->hsw_pcm_pdev = platform_device_register_data(dev,
		"hsw-pcm-audio", -1, pcm_plat_data, sizeof(*pcm_plat_data));
	if (IS_ERR(pdata->hsw_pcm_pdev))
		return PTR_ERR(pdata->hsw_pcm_pdev);

	/* register Haswell card */
	card->dev = dev;
	dev_set_drvdata(dev, card);
	snd_soc_card_set_drvdata(card, pdata);
	ret = snd_soc_register_card(card);
	if (ret) {
		platform_device_unregister(pdata->hsw_pcm_pdev);
		sst_hsw_dsp_free(pdata->hsw);
		dev_err(dev, "snd_soc_register_card() failed: %d\n", ret);
	}

	return ret;
}

static int hsw_audio_remove(struct acpi_device *acpi)
{
	struct snd_soc_card *card = dev_get_drvdata(&acpi->dev);
	struct haswell_data *pdata = snd_soc_card_get_drvdata(card);

	snd_soc_unregister_card(card);
	platform_device_unregister(pdata->hsw_pcm_pdev);
	sst_hsw_dsp_free(pdata->hsw);

	return 0;
}

// TODO: do we need this atm ?
static void hsw_audio_notify(struct acpi_device *dev, u32 event)
{
}

static struct acpi_device_id hswult_acpi_match[] = {
	{ "INT33C8", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, hswult_acpi_match);

static struct acpi_driver hsw_acpi_audio = {
	.owner = THIS_MODULE,
	.name = "hsw-ult-audio",
	.class = "hsw-ult-audio",
	.ids = hswult_acpi_match,
	.ops = {
		.add = hsw_audio_add,
		.remove = hsw_audio_remove,
		.notify = hsw_audio_notify,
	},
};

static int __init haswell_init(void)
{
	return acpi_bus_register_driver(&hsw_acpi_audio);
}

static void __exit haswell_exit(void)
{
	acpi_bus_unregister_driver(&hsw_acpi_audio);
}

module_init(haswell_init);
module_exit(haswell_exit);

/* Module information */
MODULE_AUTHOR("Liam Girdwood, Xingchao Wang");
MODULE_DESCRIPTION("Haswell ULT DSP Audio");
MODULE_LICENSE("GPL v2");
