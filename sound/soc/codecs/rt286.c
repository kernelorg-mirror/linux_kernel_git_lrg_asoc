/*
 * rt286.c  --  RT286 ALSA SoC audio codec driver
 *
 * Copyright 2013 Realtek Semiconductor Corp.
 * Author: Bard Liao <bardliao@realtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/acpi.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>
#include <sound/tlv.h>
#include <sound/jack.h>
#include <linux/workqueue.h>
#include <sound/rt286.h>
#include <sound/hda_verbs.h>

#include "rt286.h"

#define RT286_INDEX_REG_BASE 0x100
#define RT286_INDEX_REG_RANGE 0xff
#define RT286_INDEX_REG_MAX (RT286_INDEX_REG_BASE + RT286_INDEX_REG_RANGE)

struct rt286_priv {
	struct snd_soc_codec *codec;
	struct rt286_platform_data pdata;
	struct i2c_client *i2c;
	struct snd_soc_jack *jack;
	struct delayed_work jack_detect_work;
	int sys_clk;
};

static unsigned int rt286_hw_read(struct snd_soc_codec *codec,
						unsigned int reg)
{
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *client = rt286->i2c;
	struct i2c_msg xfer[2];
	int ret;
	__be32 be_reg;
	unsigned int index, vid, buf = 0x0;

	/*index registers*/
	if (reg >= RT286_INDEX_REG_BASE && reg < RT286_INDEX_REG_MAX) {
		ret = snd_soc_write(codec,
			VERB_CMD(AC_VERB_SET_COEF_INDEX,
				RT286_VENDOR_REGISTERS, 0),
			reg - RT286_INDEX_REG_BASE);

		if (ret < 0) {
			dev_err(codec->dev,
				"Failed to set private addr: %d\n", ret);
			return ret;
		}

		reg = VERB_CMD(AC_VERB_GET_PROC_COEF,
			RT286_VENDOR_REGISTERS, 0);
	}
	reg = reg | 0x80000;
	vid = (reg >> 8) & 0xfff;

	if (AC_VERB_GET_AMP_GAIN_MUTE == (vid & 0xf00)) {
		index = (reg >> 8) & 0xf;
		reg = (reg & ~0xf0f) | index;
	}
	be_reg = cpu_to_be32(reg);

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 4;
	xfer[0].buf = (u8 *)&be_reg;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 4;
	xfer[1].buf = (u8 *)&buf;

	ret = i2c_transfer(client->adapter, xfer, 2);
	if (ret < 0)
		return ret;
	else if (ret != 2)
		return -EIO;

	return be32_to_cpu(buf);
}

static int rt286_hw_write(struct snd_soc_codec *codec,
				unsigned int reg, unsigned int value)
{
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	struct i2c_client *client = rt286->i2c;
	u8 data[4];
	int ret;

	/*index registers*/
	if (reg >= RT286_INDEX_REG_BASE && reg < RT286_INDEX_REG_MAX) {
		ret = snd_soc_write(codec,
			VERB_CMD(AC_VERB_SET_COEF_INDEX,
				RT286_VENDOR_REGISTERS, 0),
			reg - RT286_INDEX_REG_BASE);
		if (ret < 0) {
			dev_err(codec->dev,
				"Failed to set private addr: %d\n", ret);
			return -EIO;
		}
		reg = VERB_CMD(AC_VERB_SET_PROC_COEF,
			RT286_VENDOR_REGISTERS, 0);
	}

	data[0] = (reg >> 24) & 0xff;
	data[1] = (reg >> 16) & 0xff;
	data[2] = ((reg >> 8) & 0xff) | ((value >> 8) & 0xff);
	data[3] = value & 0xff;

	ret = i2c_master_send(client, data, 4);
	if (ret == 4)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int rt286_support_power_controls[] = {
	RT286_DAC_OUT1,
	RT286_DAC_OUT2,
	RT286_ADC_IN1,
	RT286_ADC_IN2,
	RT286_MIC1,
	RT286_DMIC1,
	RT286_DMIC2,
	RT286_SPK_OUT,
	RT286_HP_OUT,
};
#define RT286_POWER_REG_LEN ARRAY_SIZE(rt286_support_power_controls)

static int rt286_jack_detect(struct snd_soc_codec *codec, bool *hp, bool *mic)
{
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	unsigned int val, buf;
	int i;

	*hp = false;
	*mic = false;

	if (rt286->pdata.cbj_en) {
		buf = snd_soc_read(codec, RT286_GET_HP_SENSE);
		*hp = buf & 0x80000000;
		if (*hp) {
			/* power on HV,VERF */
			snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
				RT286_POWER_CTRL1, 0x1001, 0x0);
			/* power LDO1 */
			snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
				RT286_POWER_CTRL2, 0x4, 0x4);
			snd_soc_write(codec, RT286_SET_MIC1, 0x24);
			val = snd_soc_read(codec, RT286_INDEX_REG_BASE +
							RT286_CBJ_CTRL2);

			msleep(200);
			i = 40;
			while (((val & 0x0800) == 0) && (i > 0)) {
				val = snd_soc_read(codec,
					RT286_INDEX_REG_BASE +
					RT286_CBJ_CTRL2);
				i--;
				msleep(20);
			}

			if (0x0400 == (val & 0x0700)) {
				*mic = false;

				snd_soc_write(codec,
					RT286_SET_MIC1, 0x20);
				/* power off HV,VERF */
				snd_soc_update_bits(codec,
					RT286_INDEX_REG_BASE +
					RT286_POWER_CTRL1, 0x1001, 0x1001);
				snd_soc_update_bits(codec,
					RT286_INDEX_REG_BASE +
					RT286_A_BIAS_CTRL3, 0xc000, 0x0000);
				snd_soc_update_bits(codec,
					RT286_INDEX_REG_BASE +
					RT286_CBJ_CTRL1, 0x0030, 0x0000);
				snd_soc_update_bits(codec,
					RT286_INDEX_REG_BASE +
					RT286_A_BIAS_CTRL2, 0xc000, 0x0000);
			} else if ((0x0200 == (val & 0x0700)) ||
				(0x0100 == (val & 0x0700))) {
				*mic = true;
				snd_soc_update_bits(codec,
					RT286_INDEX_REG_BASE +
					RT286_A_BIAS_CTRL3, 0xc000, 0x8000);
				snd_soc_update_bits(codec,
					RT286_INDEX_REG_BASE +
					RT286_CBJ_CTRL1, 0x0030, 0x0020);
				snd_soc_update_bits(codec,
					RT286_INDEX_REG_BASE +
					RT286_A_BIAS_CTRL2, 0xc000, 0x8000);
			} else {
				*mic = false;
			}

			snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
						RT286_MISC_CTRL1,
						0x0060, 0x0000);
		} else {
			snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
						RT286_MISC_CTRL1,
						0x0060, 0x0020);
			snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
						RT286_A_BIAS_CTRL3,
						0xc000, 0x8000);
			snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
						RT286_CBJ_CTRL1,
						0x0030, 0x0020);
			snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
						RT286_A_BIAS_CTRL2,
						0xc000, 0x8000);

			*mic = false;
		}
	} else {
		buf = snd_soc_read(codec, RT286_GET_HP_SENSE);
		*hp = buf & 0x80000000;
		buf = snd_soc_read(codec, RT286_GET_MIC1_SENSE);
		*mic = buf & 0x80000000;
	}

	return 0;
}

static void rt286_jack_detect_work(struct work_struct *work)
{
	struct rt286_priv *rt286 =
		container_of(work, struct rt286_priv, jack_detect_work);
	int status = 0;

	bool hp = false;
	bool mic = false;

	rt286_jack_detect(rt286->codec, &hp, &mic);

	if (hp == true)
		status |= SND_JACK_HEADPHONE;

	if (mic == true)
		status |= SND_JACK_MICROPHONE;

	snd_soc_jack_report(rt286->jack, status,
		SND_JACK_MICROPHONE | SND_JACK_HEADPHONE);
}

int rt286_mic_detect(struct snd_soc_codec *codec, struct snd_soc_jack *jack)
{
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	rt286->jack = jack;

	/* Send an initial empty report */
	snd_soc_jack_report(rt286->jack, 0,
		SND_JACK_MICROPHONE | SND_JACK_HEADPHONE);

	return 0;
}
EXPORT_SYMBOL_GPL(rt286_mic_detect);

static const DECLARE_TLV_DB_SCALE(out_vol_tlv, -6350, 50, 0);
static const DECLARE_TLV_DB_SCALE(mic_vol_tlv, 0, 1000, 0);



static const struct snd_kcontrol_new rt286_snd_controls[] = {
	SOC_DOUBLE_R_TLV("DAC0 Playback Volume", RT286_DACL_GAIN,
			    RT286_DACR_GAIN, 0, 0x7f, 0, out_vol_tlv),
	SOC_DOUBLE_R_TLV("ADC0 Capture Volume", RT286_ADCL_GAIN,
			    RT286_ADCR_GAIN, 0, 0x7f, 0, out_vol_tlv),
	SOC_SINGLE_TLV("AMIC Volume", RT286_MIC_GAIN,
			    0, 0x3, 0, mic_vol_tlv),
	SOC_DOUBLE_R("Speaker Playback Switch", RT286_SPOL_GAIN,
			    RT286_SPOR_GAIN, RT286_MUTE_SFT, 1, 1),
};

/* Digital Mixer */
static const struct snd_kcontrol_new rt286_front_mix[] = {
	SOC_DAPM_SINGLE("DAC Switch",  RT286_F_DAC_SWITCH,
			RT286_MUTE_SFT, 1, 1),
	SOC_DAPM_SINGLE("RECMIX Switch", RT286_F_RECMIX_SWITCH,
			RT286_MUTE_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt286_rec_mix[] = {
	SOC_DAPM_SINGLE("Mic1 Switch", RT286_REC_MIC_SWITCH,
			RT286_MUTE_SFT, 1, 1),
	SOC_DAPM_SINGLE("I2S Switch", RT286_REC_I2S_SWITCH,
			RT286_MUTE_SFT, 1, 1),
	SOC_DAPM_SINGLE("Line1 Switch", RT286_REC_LINE_SWITCH,
			RT286_MUTE_SFT, 1, 1),
	SOC_DAPM_SINGLE("Beep Switch", RT286_REC_BEEP_SWITCH,
			RT286_MUTE_SFT, 1, 1),
};

static const struct snd_kcontrol_new spo_enable_control =
	SOC_DAPM_SINGLE("Switch", RT286_SET_PIN_SPK,
			RT286_SET_PIN_SFT, 1, 0);

static const struct snd_kcontrol_new hpol_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT286_HPOL_GAIN,
			RT286_MUTE_SFT, 1, 1);

static const struct snd_kcontrol_new hpor_enable_control =
	SOC_DAPM_SINGLE_AUTODISABLE("Switch", RT286_HPOR_GAIN,
			RT286_MUTE_SFT, 1, 1);

/* ADC0 source */
static const char * const rt286_adc_src[] = {
	"Mic", "RECMIX", "Dmic"
};

static const int rt286_adc_values[] = {
	0, 4, 5,
};

static SOC_VALUE_ENUM_SINGLE_DECL(
	rt286_adc0_enum, RT286_ADC0_MUX, RT286_ADC_SEL_SFT,
	RT286_ADC_SEL_MASK, rt286_adc_src, rt286_adc_values);

static const struct snd_kcontrol_new rt286_adc0_mux =
	SOC_DAPM_VALUE_ENUM("ADC 0 source", rt286_adc0_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(
	rt286_adc1_enum, RT286_ADC1_MUX, RT286_ADC_SEL_SFT,
	RT286_ADC_SEL_MASK, rt286_adc_src, rt286_adc_values);

static const struct snd_kcontrol_new rt286_adc1_mux =
	SOC_DAPM_VALUE_ENUM("ADC 1 source", rt286_adc1_enum);

static const char * const rt286_dac_src[] = {
	"Front", "Surround"
};
/* HP-OUT source */
static SOC_ENUM_SINGLE_DECL(rt286_hpo_enum, RT286_HPO_MUX,
				  0, rt286_dac_src);

static const struct snd_kcontrol_new rt286_hpo_mux =
SOC_DAPM_ENUM("HPO source", rt286_hpo_enum);

/* SPK-OUT source */
static SOC_ENUM_SINGLE_DECL(rt286_spo_enum, RT286_SPK_MUX,
				  0, rt286_dac_src);

static const struct snd_kcontrol_new rt286_spo_mux =
SOC_DAPM_ENUM("SPO source", rt286_spo_enum);

static int rt286_spk_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_write(codec,
			RT286_SPK_EAPD, RT286_SET_EAPD_HIGH);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_write(codec,
			RT286_SPK_EAPD, RT286_SET_EAPD_LOW);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt286_set_dmic1_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_write(codec, RT286_SET_PIN_DMIC1, 0x20);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_write(codec, RT286_SET_PIN_DMIC1, 0);
		break;
	default:
		return 0;
	}

	return 0;
}

static int rt286_adc_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	unsigned int nid;

	nid = (w->reg >> 20) & 0xff;
	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		snd_soc_update_bits(codec,
			VERB_CMD(AC_VERB_SET_AMP_GAIN_MUTE, nid, 0),
			0x7080, 0x7000);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		snd_soc_update_bits(codec,
			VERB_CMD(AC_VERB_SET_AMP_GAIN_MUTE, nid, 0),
			0x7080, 0x7080);
		break;
	default:
		return 0;
	}

	return 0;
}

static const struct snd_soc_dapm_widget rt286_dapm_widgets[] = {
	/* Input Lines */
	SND_SOC_DAPM_INPUT("DMIC1 Pin"),
	SND_SOC_DAPM_INPUT("DMIC2 Pin"),
	SND_SOC_DAPM_INPUT("MIC1"),
	SND_SOC_DAPM_INPUT("LINE1"),
	SND_SOC_DAPM_INPUT("Beep"),

	/* DMIC */
	SND_SOC_DAPM_PGA_E("DMIC1", RT286_SET_POWER(RT286_DMIC1), 0, 1,
		NULL, 0, rt286_set_dmic1_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA("DMIC2", RT286_SET_POWER(RT286_DMIC2), 0, 1,
		NULL, 0),
	SND_SOC_DAPM_SUPPLY("DMIC Receiver", SND_SOC_NOPM,
		0, 0, NULL, 0),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIX", SND_SOC_NOPM, 0, 0,
		rt286_rec_mix, ARRAY_SIZE(rt286_rec_mix)),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC 0", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC 1", NULL, SND_SOC_NOPM, 0, 0),

	/* ADC Mux */
	SND_SOC_DAPM_MUX_E("ADC 0 Mux", RT286_SET_POWER(RT286_ADC_IN1), 0, 1,
		&rt286_adc0_mux, rt286_adc_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_MUX_E("ADC 1 Mux", RT286_SET_POWER(RT286_ADC_IN2), 0, 1,
		&rt286_adc1_mux, rt286_adc_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU),

	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),

	/* Output Side */
	/* DACs */
	SND_SOC_DAPM_DAC("DAC 0", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC("DAC 1", NULL, SND_SOC_NOPM, 0, 0),

	/* Output Mux */
	SND_SOC_DAPM_MUX("SPK Mux", SND_SOC_NOPM, 0, 0, &rt286_spo_mux),
	SND_SOC_DAPM_MUX("HPO Mux", SND_SOC_NOPM, 0, 0, &rt286_hpo_mux),

	SND_SOC_DAPM_SUPPLY("HP Power", RT286_SET_PIN_HPO,
		RT286_SET_PIN_SFT, 0, NULL, 0),

	/* Output Mixer */
	SND_SOC_DAPM_MIXER("Front", RT286_SET_POWER(RT286_DAC_OUT1), 0, 1,
			rt286_front_mix, ARRAY_SIZE(rt286_front_mix)),
	SND_SOC_DAPM_PGA("Surround", RT286_SET_POWER(RT286_DAC_OUT2), 0, 1,
			NULL, 0),

	/* Output Pga */
	SND_SOC_DAPM_SWITCH_E("SPO", SND_SOC_NOPM, 0, 0,
		&spo_enable_control, rt286_spk_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SWITCH("HPO L", SND_SOC_NOPM, 0, 0,
		&hpol_enable_control),
	SND_SOC_DAPM_SWITCH("HPO R", SND_SOC_NOPM, 0, 0,
		&hpor_enable_control),

	/* Output Lines */
	SND_SOC_DAPM_OUTPUT("SPOL"),
	SND_SOC_DAPM_OUTPUT("SPOR"),
	SND_SOC_DAPM_OUTPUT("HPO Pin"),
	SND_SOC_DAPM_OUTPUT("SPDIF"),
};

static const struct snd_soc_dapm_route rt286_dapm_routes[] = {
	{"DMIC1", NULL, "DMIC1 Pin"},
	{"DMIC2", NULL, "DMIC2 Pin"},
	{"DMIC1", NULL, "DMIC Receiver"},
	{"DMIC2", NULL, "DMIC Receiver"},

	{"RECMIX", "Beep Switch", "Beep"},
	{"RECMIX", "Line1 Switch", "LINE1"},
	{"RECMIX", "Mic1 Switch", "MIC1"},

	{"ADC 0 Mux", "Dmic", "DMIC1"},
	{"ADC 0 Mux", "RECMIX", "RECMIX"},
	{"ADC 0 Mux", "Mic", "MIC1"},
	{"ADC 1 Mux", "Dmic", "DMIC2"},
	{"ADC 1 Mux", "RECMIX", "RECMIX"},
	{"ADC 1 Mux", "Mic", "MIC1"},

	{"ADC 0", NULL, "ADC 0 Mux"},
	{"ADC 1", NULL, "ADC 1 Mux"},

	{"AIF1TX", NULL, "ADC 0"},
	{"AIF2TX", NULL, "ADC 1"},

	{"DAC 0", NULL, "AIF1RX"},
	{"DAC 1", NULL, "AIF2RX"},

	{"Front", "DAC Switch", "DAC 0"},
	{"Front", "RECMIX Switch", "RECMIX"},

	{"Surround", NULL, "DAC 1"},

	{"SPK Mux", "Front", "Front"},
	{"SPK Mux", "Surround", "Surround"},

	{"HPO Mux", "Front", "Front"},
	{"HPO Mux", "Surround", "Surround"},

	{"SPO", "Switch", "SPK Mux"},
	{"HPO L", "Switch", "HPO Mux"},
	{"HPO R", "Switch", "HPO Mux"},
	{"HPO L", NULL, "HP Power"},
	{"HPO R", NULL, "HP Power"},

	{"SPOL", NULL, "SPO"},
	{"SPOR", NULL, "SPO"},
	{"HPO Pin", NULL, "HPO L"},
	{"HPO Pin", NULL, "HPO R"},
};

static int rt286_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	unsigned int val = 0;
	int d_len_code;

	switch (params_rate(params)) {
	/* bit 14 0:48K 1:44.1K */
	case 44100:
		val |= 0x4000;
		break;
	case 48000:
		break;
	default:
		dev_err(codec->dev, "Unsupported sample rate %d\n",
					params_rate(params));
		return -EINVAL;
	}
	switch (rt286->sys_clk) {
	case 12288000:
	case 24576000:
		if (params_rate(params) != 48000) {
			dev_err(codec->dev, "Sys_clk is not matched (%d %d)\n",
					params_rate(params), rt286->sys_clk);
			return -EINVAL;
		}
		break;
	case 11289600:
	case 22579200:
		if (params_rate(params) != 44100) {
			dev_err(codec->dev, "Sys_clk is not matched (%d %d)\n",
					params_rate(params), rt286->sys_clk);
			return -EINVAL;
		}
		break;
	}

	if (params_channels(params) <= 16) {
		/* bit 3:0 Number of Channel */
		val |= (params_channels(params) - 1);
	} else {
		dev_err(codec->dev, "Unsupported channels %d\n",
					params_channels(params));
		return -EINVAL;
	}

	d_len_code = 0;
	switch (params_format(params)) {
	/* bit 6:4 Bits per Sample */
	case SNDRV_PCM_FORMAT_S16_LE:
		d_len_code = 0;
		val |= (0x1 << 4);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		d_len_code = 2;
		val |= (0x4 << 4);
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		d_len_code = 1;
		val |= (0x2 << 4);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		d_len_code = 2;
		val |= (0x3 << 4);
		break;
	case SNDRV_PCM_FORMAT_S8:
		break;
	default:
		return -EINVAL;
	}
	snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
		RT286_I2S_CTRL1, 0x0018, d_len_code << 3);
	dev_dbg(codec->dev, "format val = 0x%x\n", val);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		snd_soc_update_bits(codec, RT286_DAC_FORMAT, 0x407f, val);
	else
		snd_soc_update_bits(codec, RT286_ADC_FORMAT, 0x407f, val);

	return 0;
}

static int rt286_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL1, 0x800, 0x800);
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL1, 0x800, 0x0);
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL1, 0x300, 0x0);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL1, 0x300, 0x1 << 8);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL1, 0x300, 0x2 << 8);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL1, 0x300, 0x3 << 8);
		break;
	default:
		return -EINVAL;
	}
	/* bit 15 Stream Type 0:PCM 1:Non-PCM */
	snd_soc_update_bits(codec, RT286_DAC_FORMAT, 0x8000, 0);
	snd_soc_update_bits(codec, RT286_ADC_FORMAT, 0x8000, 0);

	return 0;
}

static int rt286_set_dai_sysclk(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s freq=%d\n", __func__, freq);

	if (RT286_SCLK_S_MCLK == clk_id) {
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL2, 0x0100, 0x0);
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_PLL_CTRL1, 0x20, 0x20);
	} else {
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL2, 0x0100, 0x0100);
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_PLL_CTRL, 0x4, 0x4);
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_PLL_CTRL1, 0x20, 0x0);
	}

	switch (freq) {
	case 19200000:
		if (RT286_SCLK_S_MCLK == clk_id) {
			dev_err(codec->dev, "Should not use MCLK\n");
			return -EINVAL;
		}
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL2, 0x40, 0x40);
		break;
	case 24000000:
		if (RT286_SCLK_S_MCLK == clk_id) {
			dev_err(codec->dev, "Should not use MCLK\n");
			return -EINVAL;
		}
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL2, 0x40, 0x0);
		break;
	case 12288000:
	case 11289600:
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL2, 0x8, 0x0);
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_CLK_DIV, 0xfc1e, 0x0004);
		break;
	case 24576000:
	case 22579200:
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL2, 0x8, 0x8);
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_CLK_DIV, 0xfc1e, 0x5406);
		break;
	default:
		dev_err(codec->dev, "Unsupported system clock\n");
		return -EINVAL;
	}

	rt286->sys_clk = freq;

	return 0;
}

static int rt286_set_bclk_ratio(struct snd_soc_dai *dai, unsigned int ratio)
{
	struct snd_soc_codec *codec = dai->codec;

	dev_dbg(codec->dev, "%s ratio=%d\n", __func__, ratio);
	if (50 == ratio)
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL1, 0x1000, 0x1000);
	else
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
			RT286_I2S_CTRL1, 0x1000, 0x0);


	return 0;
}

static int rt286_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (SND_SOC_BIAS_STANDBY == codec->dapm.bias_level)
			snd_soc_write(codec,
				RT286_SET_AUDIO_POWER, AC_PWRST_D0);
		break;

	case SND_SOC_BIAS_STANDBY:
		snd_soc_write(codec,
			RT286_SET_AUDIO_POWER, AC_PWRST_D3);
		break;

	default:
		break;
	}
	codec->dapm.bias_level = level;

	return 0;
}

static irqreturn_t rt286_irq(int irq, void *data)
{
	struct rt286_priv *rt286 = data;
	bool hp = false;
	bool mic = false;
	int status = 0;

	rt286_jack_detect(rt286->codec, &hp, &mic);

	/* Clear IRQ */
	snd_soc_update_bits(rt286->codec, RT286_INDEX_REG_BASE +
					RT286_IRQ_CTRL, 0x1, 0x1);

	if (hp == true)
		status |= SND_JACK_HEADPHONE;

	if (mic == true)
		status |= SND_JACK_MICROPHONE;

	snd_soc_jack_report(rt286->jack, status,
		SND_JACK_MICROPHONE | SND_JACK_HEADPHONE);

	pm_wakeup_event(&rt286->i2c->dev, 300);

	return IRQ_HANDLED;
}

static int rt286_probe(struct snd_soc_codec *codec)
{
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	int i, ret;

	snd_soc_write(codec, RT286_SET_AUDIO_POWER, AC_PWRST_D3);

	for (i = 0; i < RT286_POWER_REG_LEN; i++)
		snd_soc_write(codec,
			RT286_SET_POWER(rt286_support_power_controls[i]),
			AC_PWRST_D1);

	if (!rt286->pdata.cbj_en) {
		snd_soc_write(codec, RT286_INDEX_REG_BASE +
				RT286_CBJ_CTRL2, 0x0000);
		snd_soc_write(codec, RT286_INDEX_REG_BASE +
				RT286_MIC1_DET_CTRL, 0x0816);
		snd_soc_write(codec, RT286_INDEX_REG_BASE +
				RT286_MISC_CTRL1, 0x0000);
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
				RT286_CBJ_CTRL1, 0xf000, 0xb000);
	} else {
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
				RT286_CBJ_CTRL1, 0xf000, 0x5000);
	}

	mdelay(10);

	if (!rt286->pdata.gpio2_en)
		snd_soc_write(codec, RT286_SET_DMIC2_DEFAULT, 0x4000);
	else
		snd_soc_write(codec, RT286_SET_DMIC2_DEFAULT, 0);

	mdelay(10);

	/*Power down LDO2*/
	snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
				RT286_POWER_CTRL2, 0x8, 0x0);

	codec->dapm.bias_level = SND_SOC_BIAS_OFF;
	rt286->codec = codec;

	if (rt286->i2c->irq) {
		snd_soc_update_bits(codec, RT286_INDEX_REG_BASE +
					RT286_IRQ_CTRL, 0x2, 0x2);

		INIT_DELAYED_WORK(&rt286->jack_detect_work,
					rt286_jack_detect_work);
		schedule_delayed_work(&rt286->jack_detect_work,
					msecs_to_jiffies(1250));

		ret = request_threaded_irq(rt286->i2c->irq, NULL, rt286_irq,
			IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "rt286", rt286);
		if (ret != 0) {
			dev_err(codec->dev,
				"Failed to reguest IRQ: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int rt286_remove(struct snd_soc_codec *codec)
{
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	cancel_delayed_work_sync(&rt286->jack_detect_work);

	return 0;
}

#define RT286_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)
#define RT286_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

static const struct snd_soc_dai_ops rt286_aif_dai_ops = {
	.hw_params = rt286_hw_params,
	.set_fmt = rt286_set_dai_fmt,
	.set_sysclk = rt286_set_dai_sysclk,
	.set_bclk_ratio = rt286_set_bclk_ratio,
};

static struct snd_soc_dai_driver rt286_dai[] = {
	{
	.name = "rt286-aif1",
	.id = RT286_AIF1,
	.playback = {
		.stream_name = "AIF1 Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = RT286_STEREO_RATES,
		.formats = RT286_FORMATS,
	},
	.capture = {
		.stream_name = "AIF1 Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = RT286_STEREO_RATES,
		.formats = RT286_FORMATS,
	},
	.ops = &rt286_aif_dai_ops,
	.symmetric_rates = 1,
	},
	{
		.name = "rt286-aif2",
		.id = RT286_AIF2,
		.playback = {
			.stream_name = "AIF2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT286_STEREO_RATES,
			.formats = RT286_FORMATS,
		},
		.capture = {
			.stream_name = "AIF2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = RT286_STEREO_RATES,
			.formats = RT286_FORMATS,
		},
		.ops = &rt286_aif_dai_ops,
		.symmetric_rates = 1,
	},

};

static struct snd_soc_codec_driver soc_codec_dev_rt286 = {
	.probe = rt286_probe,
	.remove = rt286_remove,
	.set_bias_level = rt286_set_bias_level,
	.idle_bias_off = true,
	.write = rt286_hw_write,
	.read = rt286_hw_read,
	.controls = rt286_snd_controls,
	.num_controls = ARRAY_SIZE(rt286_snd_controls),
	.dapm_widgets = rt286_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt286_dapm_widgets),
	.dapm_routes = rt286_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt286_dapm_routes),
};

static const struct i2c_device_id rt286_i2c_id[] = {
	{"rt286", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, rt286_i2c_id);

static const struct acpi_device_id rt286_acpi_match[] = {
	{ "INT343A", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, rt286_acpi_match);

static int rt286_i2c_probe(struct i2c_client *i2c,
			   const struct i2c_device_id *id)
{
	struct rt286_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt286_priv *rt286;
	int ret;

	rt286 = devm_kzalloc(&i2c->dev,	sizeof(*rt286),
				GFP_KERNEL);
	if (NULL == rt286)
		return -ENOMEM;

	rt286->i2c = i2c;
	i2c_set_clientdata(i2c, rt286);

	if (pdata)
		rt286->pdata = *pdata;

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt286,
				     rt286_dai, ARRAY_SIZE(rt286_dai));

	return ret;
}

static int rt286_i2c_remove(struct i2c_client *i2c)
{
	struct rt286_priv *rt286 = i2c_get_clientdata(i2c);

	if (i2c->irq)
		free_irq(i2c->irq, rt286);
	snd_soc_unregister_codec(&i2c->dev);

	return 0;
}


struct i2c_driver rt286_i2c_driver = {
	.driver = {
		   .name = "rt286",
		   .owner = THIS_MODULE,
		   .acpi_match_table = ACPI_PTR(rt286_acpi_match),
		   },
	.probe = rt286_i2c_probe,
	.remove = rt286_i2c_remove,
	.id_table = rt286_i2c_id,
};

module_i2c_driver(rt286_i2c_driver);

MODULE_DESCRIPTION("ASoC RT286 driver");
MODULE_AUTHOR("Bard Liao <bardliao@realtek.com>");
MODULE_LICENSE("GPL");
