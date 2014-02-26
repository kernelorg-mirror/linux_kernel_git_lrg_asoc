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
#include <sound/rt286.h>

#include "../../pci/hda/hda_codec.h"
#include "rt286.h"

struct rt286_priv {
	struct regmap *regmap;
	struct snd_soc_codec *codec;
	struct rt286_platform_data pdata;
	struct snd_soc_jack *jack;
	struct i2c_client *i2c;
	int sys_clk;
};

static unsigned int rt286_reg_cache[] = {
	[RT286_AUDIO_FUNCTION_GROUP] = 0x0000,
	[RT286_DAC_OUT1] = 0x7f7f,
	[RT286_DAC_OUT2] = 0x7f7f,
	[RT286_SPDIF] = 0x0000,
	[RT286_ADC_IN1] = 0x4343,
	[RT286_ADC_IN2] = 0x4343,
	[RT286_MIC1] = 0x0000,
	[RT286_MIXER_IN] = 0x000b,
	[RT286_MIXER_OUT1] = 0x0002,
	[RT286_MIXER_OUT2] = 0x0000,
	[RT286_DMIC1] = 0x0000,
	[RT286_DMIC2] = 0x0000,
	[RT286_LINE1] = 0x0000,
	[RT286_BEEP] = 0x0000,
	[RT286_VENDOR_REGISTERS] = 0x0000,
	[RT286_SPK_OUT] = 0x8080,
	[RT286_HP_OUT] = 0x8080,
	[RT286_MIXER_IN1] = 0x0000,
	[RT286_MIXER_IN2] = 0x0000,
};

static int rt286_hw_read(void *context, unsigned int reg, unsigned int *value)
{
	struct i2c_client *client = context;
	struct i2c_msg xfer[2];
	int ret;
	unsigned int buf = 0x0;

	if (reg <= 0xff) { /*read cache*/
		*value = rt286_reg_cache[reg];
		return 0;
	}
	reg = cpu_to_be32(reg);
	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 4;
	xfer[0].buf = (u8 *)&reg;

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

	*value = be32_to_cpu(buf);

	return 0;
}

static int rt286_hw_write(void *context, unsigned int reg, unsigned int value)
{
	struct i2c_client *client = context;
	u8 data[4];
	int ret;

	if (reg <= 0xff) { /*write cache*/
		rt286_reg_cache[reg] = value;
		return 0;
	}
	data[0] = (reg >> 24) & 0xff;
	data[1] = (reg >> 16) & 0xff;
	data[2] = (reg >> 8) & 0xff;
	data[3] = reg & 0xff;

	ret = i2c_master_send(client, data, 4);
	if (ret == 4)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}

static int rt286_update_bits(struct snd_soc_codec *codec, unsigned int vid,
				unsigned int nid, unsigned int data,
				unsigned int mask, unsigned int value)
{
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	unsigned int old, new, verb;
	int change, ret;

	verb = VERB_CMD((vid | 0x800), nid, data);
	regmap_read(rt286->regmap, verb, &old);
	new = (old & ~mask) | (value & mask);
	change = old != new;

	if (change) {
		ret = regmap_write(rt286->regmap, verb, 0);
		if (ret < 0) {
			dev_err(codec->dev,
				"Failed to write private reg: %d\n", ret);
			goto err;
		}
	}
	return change;

err:
	return ret;
}

static int rt286_index_write(struct snd_soc_codec *codec,
	unsigned int wid, unsigned int index, unsigned int data)
{
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	int ret;
	unsigned int verb;

	verb = VERB_CMD(AC_VERB_SET_COEF_INDEX, wid, index);
	ret = regmap_write(rt286->regmap, verb, 0);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private addr: %d\n", ret);
		goto err;
	}
	verb = VERB_CMD(AC_VERB_SET_PROC_COEF, wid, data);
	ret = regmap_write(rt286->regmap, verb, 0);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private value: %d\n", ret);
		goto err;
	}

	return 0;
err:
	return ret;
}

static unsigned int rt286_index_read(struct snd_soc_codec *codec,
	unsigned int wid, unsigned int index)
{
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	int ret;
	unsigned int verb, val;

	verb = VERB_CMD(AC_VERB_SET_COEF_INDEX, wid, index);
	ret = regmap_write(rt286->regmap, verb, 0);

	if (ret < 0) {
		dev_err(codec->dev, "Failed to set private addr: %d\n", ret);
		return ret;
	}

	verb = VERB_CMD(AC_VERB_GET_PROC_COEF, wid, index);
	regmap_read(rt286->regmap, verb, &val);
	return val;
}

static int rt286_index_update_bits(struct snd_soc_codec *codec,
	unsigned int wid, unsigned int index,
	unsigned int mask, unsigned int data)
{
	unsigned int old, new;
	int change, ret;

	old =  rt286_index_read(codec, wid, index);
	new = (old & ~mask) | (data & mask);
	change = old != new;

	if (change) {
		ret = rt286_index_write(codec, wid, index, new);
		if (ret < 0) {
			dev_err(codec->dev,
				"Failed to write private reg: %d\n", ret);
			goto err;
		}
	}
	return change;

err:
	return ret;
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

static const struct reg_default rt286_reg[] = {
	{ RT286_DAC_OUT1, 0x7f7f },
	{ RT286_DAC_OUT2, 0x7f7f },
	{ RT286_SPDIF, 0x0000 },
	{ RT286_ADC_IN1, 0x4343 },
	{ RT286_ADC_IN2, 0x4343 },
	{ RT286_MIC1, 0x0000 },
	{ RT286_MIXER_IN, 0x000b },
	{ RT286_MIXER_OUT1, 0x0002 },
	{ RT286_MIXER_OUT2, 0x0000 },
	{ RT286_SPK_OUT, 0x0000 },
	{ RT286_HP_OUT, 0x0000 },
	{ RT286_MIXER_IN1, 0x0005 },
	{ RT286_MIXER_IN2, 0x0005 },
};

static bool rt286_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case RT286_DAC_OUT1:
	case RT286_DAC_OUT2:
	case RT286_ADC_IN1:
	case RT286_ADC_IN2:
	case RT286_MIC1:
	case RT286_SPDIF:
	case RT286_MIXER_IN:
	case RT286_MIXER_OUT1:
	case RT286_MIXER_OUT2:
	case RT286_DMIC1:
	case RT286_DMIC2:
	case RT286_SPK_OUT:
	case RT286_HP_OUT:
	case RT286_MIXER_IN1:
	case RT286_MIXER_IN2:
		return true;
	default:
		return false;
	}
}

static int rt286_jack_detect(struct snd_soc_codec *codec, bool *hp, bool *mic)
{
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	unsigned int val, buf;
	int i;

	*hp  = false;
	*mic = false;

	if (rt286->pdata.cbj_en) {
		regmap_read(rt286->regmap, RT286_GET_HP_SENSE, &buf);

		*hp = buf & 0x80000000;
		if (*hp) {
			/* power on HV,VERF */
			rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
				RT286_POWER_CTRL1, 0x1001, 0x0);
			/* power LDO1 */
			rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
				RT286_POWER_CTRL2, 0x4, 0x4);
			regmap_write(rt286->regmap, RT286_SET_MIC1_24, 0);
			val = rt286_index_read(codec,
				RT286_VENDOR_REGISTERS, RT286_CBJ_CTRL2);

			msleep(200);
			i = 40;
			while (((val & 0x0800) == 0) && (i > 0)) {
				val = rt286_index_read(codec,
					RT286_VENDOR_REGISTERS,
					RT286_CBJ_CTRL2);
				i--;
				msleep(20);
			}

			if (0x0400 == (val & 0x0700)) {
				*mic = false;

				regmap_write(rt286->regmap,
					RT286_SET_MIC1_20, 0);
				/* power off HV,VERF */
				rt286_index_update_bits(codec,
					RT286_VENDOR_REGISTERS,
					RT286_POWER_CTRL1, 0x1001, 0x1001);
				rt286_index_update_bits(codec,
					RT286_VENDOR_REGISTERS,
					RT286_A_BIAS_CTRL3, 0xc000, 0x0000);
				rt286_index_update_bits(codec,
					RT286_VENDOR_REGISTERS,
					RT286_CBJ_CTRL1, 0x0030, 0x0000);
				rt286_index_update_bits(codec,
					RT286_VENDOR_REGISTERS,
					RT286_A_BIAS_CTRL2, 0xc000, 0x0000);
			} else if ((0x0200 == (val & 0x0700)) ||
				(0x0100 == (val & 0x0700))) {
				*mic = true;
			} else {
				*mic = false;
			}

			rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
						RT286_MISC_CTRL1,
						0x0060, 0x0000);
		} else {
			rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
						RT286_MISC_CTRL1,
						0x0060, 0x0020);
			rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
						RT286_A_BIAS_CTRL3,
						0xc000, 0x8000);
			rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
						RT286_CBJ_CTRL1,
						0x0030, 0x0020);
			rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
						RT286_A_BIAS_CTRL2,
						0xc000, 0x8000);

			*mic = false;
		}
	} else {
		regmap_read(rt286->regmap, RT286_GET_HP_SENSE, &buf);
		*hp = buf & 0x80000000;
		regmap_read(rt286->regmap, RT286_GET_MIC1_SENSE, &buf);
		*mic = buf & 0x80000000;
	}


	/* Clear IRQ */
	rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
				RT286_IRQ_CTRL, 0x1, 0x1);
	return 0;
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

static int rt286_vol_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	unsigned int verb, buf, io = 0x8000;

	if (RT286_ADC_IN1 == mc->reg)
		io = 0;
	verb = VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, mc->reg, (0x2000 | io));
	regmap_read(rt286->regmap, verb, &buf);
	ucontrol->value.integer.value[0] = buf & 0x7f;
	verb = VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, mc->reg, io);
	regmap_read(rt286->regmap, verb, &buf);
	ucontrol->value.integer.value[1] = buf & 0x7f;

	return 0;
}

static int rt286_vol_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	unsigned int verb, vall, valr, io;

	if (RT286_ADC_IN1 == mc->reg)
		io = 0x4000;
	else
		io = 0x8000;
	vall = ucontrol->value.integer.value[0];
	valr = ucontrol->value.integer.value[1];
	if (vall == valr) {
		vall = vall | 0x3000 | io;
		verb = VERB_CMD(AC_VERB_SET_AMP_GAIN_MUTE, mc->reg, vall);
		regmap_write(rt286->regmap, verb, 0);
	} else {
		vall = vall | 0x2000 | io;
		verb = VERB_CMD(AC_VERB_SET_AMP_GAIN_MUTE, mc->reg, vall);
		regmap_write(rt286->regmap, verb, 0);
		valr = valr | 0x1000 | io;
		verb = VERB_CMD(AC_VERB_SET_AMP_GAIN_MUTE, mc->reg, valr);
		regmap_write(rt286->regmap, verb, 0);
	}

	return 0;
}

static int rt286_mic_gain_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	unsigned int verb, buf;

	verb = VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, mc->reg, 0x0);
	regmap_read(rt286->regmap, verb, &buf);
	ucontrol->value.integer.value[0] = buf & 0x3;

	return 0;
}

static int rt286_mic_gain_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	unsigned int verb, val;

	val = ucontrol->value.integer.value[0];
	val = val | 0x7000;
	verb = VERB_CMD(AC_VERB_SET_AMP_GAIN_MUTE, mc->reg, val);
	regmap_write(rt286->regmap, verb, 0);

	return 0;
}

static int rt286_playback_switch_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	unsigned int verb, buf;

	verb = VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, mc->reg, 0xa000);
	regmap_read(rt286->regmap, verb, &buf);
	ucontrol->value.integer.value[0] = !(buf & 0x80);
	verb = VERB_CMD(AC_VERB_GET_AMP_GAIN_MUTE, mc->reg, 0x8000);
	regmap_read(rt286->regmap, verb, &buf);
	ucontrol->value.integer.value[1] = !(buf & 0x80);

	return 0;
}

static int rt286_playback_switch_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
	    (struct soc_mixer_control *)kcontrol->private_value;
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	unsigned int verb, vall, valr, val = 0;

	vall = (!ucontrol->value.integer.value[0] << 7);
	valr = (!ucontrol->value.integer.value[1] << 7);
	if (vall)
		val |= 0x8000;
	if (valr)
		val |= 0x0080;

	if (vall == valr) {
		vall = vall | 0xb000;
		verb = VERB_CMD(AC_VERB_SET_AMP_GAIN_MUTE, mc->reg, vall);
		regmap_write(rt286->regmap, verb, 0);
	} else {
		vall = vall | 0xa000;
		verb = VERB_CMD(AC_VERB_SET_AMP_GAIN_MUTE, mc->reg, vall);
		regmap_write(rt286->regmap, verb, 0);
		valr = valr | 0x9000;
		verb = VERB_CMD(AC_VERB_SET_AMP_GAIN_MUTE, mc->reg, valr);
		regmap_write(rt286->regmap, verb, 0);
	}
	snd_soc_update_bits(codec, mc->reg, 0x8080, val);

	return 0;
}

static const struct snd_kcontrol_new rt286_snd_controls[] = {
	SOC_DOUBLE_EXT_TLV("DAC0 Playback Volume", RT286_DAC_OUT1,
			   8, 0, 0x7f, 0,
			   rt286_vol_get, rt286_vol_put,
			   out_vol_tlv),
	SOC_DOUBLE_EXT_TLV("ADC0 Capture Volume", RT286_ADC_IN1,
			   8, 0, 0x7f, 0,
			   rt286_vol_get, rt286_vol_put,
			   out_vol_tlv),
	SOC_SINGLE_EXT_TLV("AMIC Volume", RT286_MIC1, 0, 0x3, 0,
			   rt286_mic_gain_get, rt286_mic_gain_put,
			   mic_vol_tlv),
	SOC_DOUBLE_EXT("Headphone Playback Switch", RT286_HP_OUT,
			   15, 8, 1, 1, rt286_playback_switch_get,
			   rt286_playback_switch_put),
	SOC_DOUBLE_EXT("Speaker Playback Switch", RT286_SPK_OUT,
			   15, 8, 1, 1, rt286_playback_switch_get,
			   rt286_playback_switch_put),
};

/* Digital Mixer */
static const struct snd_kcontrol_new rt286_front_mix[] = {
	SOC_DAPM_SINGLE("DAC Switch", RT286_MIXER_OUT1,
			RT286_M_FRONT_DAC_SFT, 1, 1),
	SOC_DAPM_SINGLE("RECMIX Switch", RT286_MIXER_OUT1,
			RT286_M_FRONT_REC_SFT, 1, 1),
};

/* Analog Input Mixer */
static const struct snd_kcontrol_new rt286_rec_mix[] = {
	SOC_DAPM_SINGLE("Beep Switch", RT286_MIXER_IN,
			RT286_M_REC_BEEP_SFT, 1, 1),
	SOC_DAPM_SINGLE("Line1 Switch", RT286_MIXER_IN,
			RT286_M_REC_LINE1_SFT, 1, 1),
	SOC_DAPM_SINGLE("Mic1 Switch", RT286_MIXER_IN,
			RT286_M_REC_MIC1_SFT, 1, 1),
	SOC_DAPM_SINGLE("I2S Switch", RT286_MIXER_IN,
			RT286_M_REC_I2S_SFT, 1, 1),
};

static const struct snd_kcontrol_new spo_enable_control =
	SOC_DAPM_SINGLE("Switch", RT286_SPK_OUT,
		RT286_M_SPK_MUX_SFT, 1, 1);

static const struct snd_kcontrol_new hpo_enable_control =
	SOC_DAPM_SINGLE("Switch", RT286_HP_OUT,
		RT286_M_HP_MUX_SFT, 1, 1);

/* ADC0 source */
static const char * const rt286_adc_src[] = {
	"Mic", "Dmic"
};

static int rt286_adc_values[] = {
	0, 5,
};

static SOC_VALUE_ENUM_SINGLE_DECL(
	rt286_adc0_enum, RT286_MIXER_IN1, RT286_ADC_SEL_SFT,
	RT286_ADC_SEL_MASK, rt286_adc_src, rt286_adc_values);

static const struct snd_kcontrol_new rt286_adc0_mux =
	SOC_DAPM_VALUE_ENUM("ADC 0 source", rt286_adc0_enum);

static SOC_VALUE_ENUM_SINGLE_DECL(
	rt286_adc1_enum, RT286_MIXER_IN2, RT286_ADC_SEL_SFT,
	RT286_ADC_SEL_MASK, rt286_adc_src, rt286_adc_values);

static const struct snd_kcontrol_new rt286_adc1_mux =
	SOC_DAPM_VALUE_ENUM("ADC 1 source", rt286_adc1_enum);

/* HP-OUT source */
static const char * const rt286_hpo_src[] = {
	"Front", "Surround"
};

static SOC_ENUM_SINGLE_DECL(rt286_hpo_enum, RT286_HP_OUT,
				  RT286_HP_SEL_SFT, rt286_hpo_src);

static const struct snd_kcontrol_new rt286_hpo_mux =
SOC_DAPM_ENUM("HPO source", rt286_hpo_enum);

/* SPK-OUT source */
static const char * const rt286_spo_src[] = {
	"Front", "Surround"
};

static SOC_ENUM_SINGLE_DECL(rt286_spo_enum, RT286_SPK_OUT,
				  RT286_SPK_SEL_SFT, rt286_spo_src);

static const struct snd_kcontrol_new rt286_spo_mux =
SOC_DAPM_ENUM("SPO source", rt286_spo_enum);

/* SPDIF source */
static const char * const rt286_spdif_src[] = {
	"PCM-IN 0", "PCM-IN 1", "SP-OUT", "PP"
};

static SOC_ENUM_SINGLE_DECL(rt286_spdif_enum, RT286_SPDIF,
				  RT286_SPDIF_SEL_SFT, rt286_spdif_src);

static const struct snd_kcontrol_new rt286_spdif_mux =
SOC_DAPM_ENUM("SPDIF source", rt286_spdif_enum);


static int rt286_spk_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt286->regmap, RT286_SET_SPK_D0, 0);
		regmap_write(rt286->regmap, RT286_ENABLE_SPK, 0);
		regmap_write(rt286->regmap, RT286_SPK_EAPD_HIGH, 0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt286->regmap, RT286_SPK_EAPD_LOW, 0);
		regmap_write(rt286->regmap, RT286_DISABLE_SPK, 0);
		regmap_write(rt286->regmap, RT286_SET_SPK_D3, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt286_hp_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	unsigned int val, buf;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_read(rt286->regmap, RT286_HP_OUT, &buf);
		val = buf & 0x8080;
		switch (val) {
		case 0x0:
			regmap_write(rt286->regmap, RT286_UNMUTE_HPO, 0);
			break;
		case 0x8000:
			regmap_write(rt286->regmap, RT286_UNMUTE_HPO_R, 0);
			break;
		case 0x0080:
			regmap_write(rt286->regmap, RT286_UNMUTE_HPO_L, 0);
			break;
		}
		if (val != 0x8080)
			mdelay(100);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt286->regmap, RT286_MUTE_HPO, 0);
		break;

	default:
		return 0;
	}

	return 0;
}

static int rt286_set_power_event(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt286->regmap, RT286_SET_D0(w->reg), 0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt286->regmap, RT286_SET_D3(w->reg), 0);
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
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt286->regmap, RT286_SET_DMIC1_D0, 0);
		regmap_write(rt286->regmap, RT286_ENABLE_DMIC1, 0);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt286->regmap, RT286_DISABLE_DMIC1, 0);
		regmap_write(rt286->regmap, RT286_SET_DMIC1_D3, 0);
		break;
	default:
		return 0;
	}

	return 0;
}

static int rt286_hp_pow_event(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		regmap_write(rt286->regmap, RT286_SET_HPO_D0, 0);
		regmap_write(rt286->regmap, RT286_ENABLE_HPO, 0);
		break;

	case SND_SOC_DAPM_PRE_PMD:
		regmap_write(rt286->regmap, RT286_DISABLE_HPO, 0);
		regmap_write(rt286->regmap, RT286_SET_HPO_D3, 0);
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
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e;
	unsigned int verb, val, buf;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		if (rt286->pdata.cbj_en) {
			rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
				RT286_A_BIAS_CTRL3, 0xc000, 0x8000);
			rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
				RT286_CBJ_CTRL1, 0x0030, 0x0020);
			rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
				RT286_A_BIAS_CTRL2, 0xc000, 0x8000);
		}
		regmap_write(rt286->regmap, RT286_SET_D0(w->reg), 0);
		rt286_update_bits(codec, AC_VERB_SET_AMP_GAIN_MUTE,
			w->reg, 0, 0x7080, 0x7000);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		rt286_update_bits(codec, AC_VERB_SET_AMP_GAIN_MUTE,
			w->reg, 0, 0x7080, 0x7080);
		regmap_write(rt286->regmap, RT286_SET_D3(w->reg), 0);
		break;
	case SND_SOC_DAPM_POST_REG:
		e = (struct soc_enum *)kcontrol->private_value;
		regmap_read(rt286->regmap, e->reg, &buf);
		val = buf & RT286_ADC_SEL_MASK;
		verb = VERB_CMD(AC_VERB_SET_CONNECT_SEL, e->reg, val);
		regmap_write(rt286->regmap, verb, 0);
		break;
	default:
		return 0;
	}

	return 0;
}

static int rt286_out_mux_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = w->codec;
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	unsigned int verb, val, buf;

	switch (event) {
	case SND_SOC_DAPM_POST_REG:
		regmap_read(rt286->regmap, e->reg, &buf);
		val =  buf & 0x1;
		verb = VERB_CMD(AC_VERB_SET_CONNECT_SEL, e->reg, val);
		regmap_write(rt286->regmap, verb, 0);
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
	SND_SOC_DAPM_PGA_E("DMIC1", RT286_DMIC1, 15, 0,
		NULL, 0, rt286_set_dmic1_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_PGA_E("DMIC2", RT286_DMIC2, 15, 0,
		NULL, 0, rt286_set_power_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SUPPLY("DMIC Receiver", SND_SOC_NOPM,
		0, 0, NULL, 0),

	/* REC Mixer */
	SND_SOC_DAPM_MIXER("RECMIX", SND_SOC_NOPM, 0, 0,
		rt286_rec_mix, ARRAY_SIZE(rt286_rec_mix)),

	/* ADCs */
	SND_SOC_DAPM_ADC("ADC 0", NULL, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_ADC("ADC 1", NULL, SND_SOC_NOPM, 0, 0),

	/* ADC Mux */
	SND_SOC_DAPM_MUX_E("ADC 0 Mux", RT286_ADC_IN1, 15, 0,
		&rt286_adc0_mux, rt286_adc_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_REG),
	SND_SOC_DAPM_MUX_E("ADC 1 Mux", RT286_ADC_IN2, 15, 0,
		&rt286_adc1_mux, rt286_adc_event, SND_SOC_DAPM_PRE_PMD |
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_REG),


	/* Audio Interface */
	SND_SOC_DAPM_AIF_IN("AIF1RX", "AIF1 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF1TX", "AIF1 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_IN("AIF2RX", "AIF2 Playback", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2TX", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),


	/* Output Side */

	/* DACs */
	SND_SOC_DAPM_DAC_E("DAC 0", NULL, RT286_DAC_OUT1,
			   15, 0, rt286_set_power_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC_E("DAC 1", NULL, RT286_DAC_OUT2,
			   15, 0, rt286_set_power_event,
			   SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),

	/* Output Mux */
	SND_SOC_DAPM_MUX_E("SPK Mux", SND_SOC_NOPM, 0, 0,
			 &rt286_spo_mux,
			 rt286_out_mux_event,
			 SND_SOC_DAPM_POST_REG),

	SND_SOC_DAPM_MUX_E("HPO Mux", SND_SOC_NOPM, 0, 0,
			 &rt286_hpo_mux,
			 rt286_out_mux_event,
			 SND_SOC_DAPM_POST_REG),

	SND_SOC_DAPM_SUPPLY("HP Power", SND_SOC_NOPM,
		0, 0, rt286_hp_pow_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),

	SND_SOC_DAPM_MUX("SPDIF Mux", SND_SOC_NOPM, 0, 0,
			 &rt286_spdif_mux),

	/* Output Mixer */
	SND_SOC_DAPM_MIXER("Front", SND_SOC_NOPM, 0, 0,
			   rt286_front_mix, ARRAY_SIZE(rt286_front_mix)),
	SND_SOC_DAPM_PGA("Surround", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* Output Pga */
	SND_SOC_DAPM_SWITCH_E("SPO", SND_SOC_NOPM, 0, 0,
		&spo_enable_control, rt286_spk_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),
	SND_SOC_DAPM_SWITCH_E("HPO", SND_SOC_NOPM, 0, 0,
		&hpo_enable_control, rt286_hp_event,
		SND_SOC_DAPM_PRE_PMD | SND_SOC_DAPM_POST_PMU),

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
	{"ADC 0 Mux", "Mic", "MIC1"},
	{"ADC 1 Mux", "Dmic", "DMIC2"},
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
	{"HPO", "Switch", "HPO Mux"},
	{"HPO", NULL, "HP Power"},

	{"SPOL", NULL, "SPO"},
	{"SPOR", NULL, "SPO"},
	{"HPO Pin", NULL, "HPO"},
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
	rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
		RT286_I2S_CTRL1, 0x0018, d_len_code << 3);
	dev_dbg(codec->dev, "format val = 0x%x\n", val);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		rt286_update_bits(codec, AC_VERB_SET_STREAM_FORMAT,
			RT286_DAC_OUT1, 0, 0x407f, val);
	else
		rt286_update_bits(codec, AC_VERB_SET_STREAM_FORMAT,
			RT286_ADC_IN1, 0, 0x407f, val);

	return 0;
}

static int rt286_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL1, 0x800, 0x800);
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL1, 0x800, 0x0);
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL1, 0x300, 0x0);
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL1, 0x300, 0x1 << 8);
		break;
	case SND_SOC_DAIFMT_DSP_A:
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL1, 0x300, 0x2 << 8);
		break;
	case SND_SOC_DAIFMT_DSP_B:
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL1, 0x300, 0x3 << 8);
		break;
	default:
		return -EINVAL;
	}
	/* bit 15 Stream Type 0:PCM 1:Non-PCM */
	rt286_update_bits(codec, AC_VERB_SET_STREAM_FORMAT,
		RT286_DAC_OUT1, 0, 0x8000, 0x0);
	rt286_update_bits(codec, AC_VERB_SET_STREAM_FORMAT,
		RT286_ADC_IN1, 0, 0x8000, 0x0);

	return 0;
}

static int rt286_set_dai_sysclk(struct snd_soc_dai *dai,
				int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);

	dev_dbg(codec->dev, "%s freq=%d\n", __func__, freq);

	if (RT286_SCLK_S_MCLK == clk_id) {
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL2, 0x0100, 0x0);
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_PLL_CTRL1, 0x20, 0x20);
	} else {
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL2, 0x0100, 0x0100);
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_PLL_CTRL, 0x4, 0x4);
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_PLL_CTRL1, 0x20, 0x0);
	}

	switch (freq) {
	case 19200000:
	case 24000000:
		if (RT286_SCLK_S_MCLK == clk_id) {
			dev_err(codec->dev, "Should not use MCLK\n");
			return -EINVAL;
		}
		break;
	case 12288000:
	case 11289600:
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL2, 0x8, 0x0);
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_CLK_DIV, 0xfc1e, 0x0004);
		break;
	case 24576000:
	case 22579200:
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL2, 0x8, 0x8);
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_CLK_DIV, 0xfc1e, 0x5406);
		break;
	default:
		dev_err(codec->dev, "Unsupported system clock\n");
		return -EINVAL;
	}

	rt286->sys_clk = freq;

	return 0;
}

static int rt286_set_dai_dfs(struct snd_soc_dai *dai, unsigned int fs)
{
	struct snd_soc_codec *codec = dai->codec;

	dev_dbg(codec->dev, "%s fs=%d\n", __func__, fs);
	if (50 == fs)
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL1, 0x1000, 0x1000);
	else
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
			RT286_I2S_CTRL1, 0x1000, 0x0);


	return 0;
}

static int rt286_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct rt286_priv *rt286 = snd_soc_codec_get_drvdata(codec);

	switch (level) {
	case SND_SOC_BIAS_PREPARE:
		if (SND_SOC_BIAS_STANDBY == codec->dapm.bias_level)
			regmap_write(rt286->regmap, RT286_SET_AUDIO_D0, 0);
		break;

	case SND_SOC_BIAS_STANDBY:
		regmap_write(rt286->regmap, RT286_SET_AUDIO_D3, 0);
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

	rt286_jack_detect(rt286->codec, &hp, &mic);

	int status = 0;
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

	regmap_write(rt286->regmap, RT286_SET_AUDIO_D3, 0);
	for (i = 0; i < RT286_POWER_REG_LEN; i++) {
		regmap_write(rt286->regmap,
			RT286_SET_D3(rt286_support_power_controls[i]), 0);
	}

	if (!rt286->pdata.cbj_en) {
		rt286_index_write(codec, RT286_VENDOR_REGISTERS,
				RT286_CBJ_CTRL2, 0x0000);
		rt286_index_write(codec, RT286_VENDOR_REGISTERS,
				RT286_MIC1_DET_CTRL, 0x0816);
		rt286_index_write(codec, RT286_VENDOR_REGISTERS,
				RT286_MISC_CTRL1, 0x0000);
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
				RT286_CBJ_CTRL1, 0xf000, 0xb000);
	} else {
		rt286_index_update_bits(codec, RT286_VENDOR_REGISTERS,
				RT286_CBJ_CTRL1, 0xf000, 0x5000);
	}

	mdelay(10);

	if (!rt286->pdata.gpio2_en)
		regmap_write(rt286->regmap, RT286_SET_DMIC2_DEFAULT_4000, 0);
	else
		regmap_write(rt286->regmap, RT286_SET_DMIC2_DEFAULT_0, 0);

	mdelay(10);

	/*Power down LDO2*/
	rt286_index_update_bits(codec,
		RT286_VENDOR_REGISTERS, RT286_POWER_CTRL2, 0x8, 0x0);

	codec->dapm.bias_level = SND_SOC_BIAS_OFF;
	rt286->codec = codec;

	if (rt286->i2c->irq) {
		rt286_index_update_bits(codec,
			RT286_VENDOR_REGISTERS, RT286_IRQ_CTRL, 0x2, 0x2);
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

#define RT286_STEREO_RATES (SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000)
#define RT286_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
			SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S8)

struct snd_soc_dai_ops rt286_aif_dai_ops = {
	.hw_params = rt286_hw_params,
	.set_fmt = rt286_set_dai_fmt,
	.set_sysclk = rt286_set_dai_sysclk,
	.set_dfs = rt286_set_dai_dfs,
};

struct snd_soc_dai_driver rt286_dai[] = {
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
};

static struct snd_soc_codec_driver soc_codec_dev_rt286 = {
	.probe = rt286_probe,
	.set_bias_level = rt286_set_bias_level,
	.idle_bias_off = true,
	.controls = rt286_snd_controls,
	.num_controls = ARRAY_SIZE(rt286_snd_controls),
	.dapm_widgets = rt286_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rt286_dapm_widgets),
	.dapm_routes = rt286_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(rt286_dapm_routes),
};

static const struct regmap_config rt286_regmap = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_write = rt286_hw_write,
	.reg_read = rt286_hw_read,
	.readable_reg = rt286_readable_register,
	.cache_type = REGCACHE_NONE,
};

static const struct i2c_device_id rt286_i2c_id[] = {
	{"rt286", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, rt286_i2c_id);

static struct acpi_device_id rt286_acpi_match[] = {
	{ "INT33CA", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, rt286_acpi_match);

static int rt286_i2c_probe(struct i2c_client *i2c,
			   const struct i2c_device_id *id)
{
	struct rt286_platform_data *pdata = dev_get_platdata(&i2c->dev);
	struct rt286_priv *rt286;
	struct device *dev = &i2c->dev;
	int ret;

	rt286 = devm_kzalloc(&i2c->dev,
				sizeof(struct rt286_priv),
				GFP_KERNEL);
	if (NULL == rt286)
		return -ENOMEM;

	rt286->regmap = devm_regmap_init(dev, NULL, i2c, &rt286_regmap);
	if (IS_ERR(rt286->regmap)) {
		ret = PTR_ERR(rt286->regmap);
		dev_err(&i2c->dev, "Failed to allocate register map: %d\n",
			ret);
		return ret;
	}

	rt286->i2c = i2c;
	i2c_set_clientdata(i2c, rt286);

	if (pdata)
		rt286->pdata = *pdata;

	ret = devm_snd_soc_register_codec(&i2c->dev, &soc_codec_dev_rt286,
				     rt286_dai, ARRAY_SIZE(rt286_dai));

	return ret;
}

struct i2c_driver rt286_i2c_driver = {
	.driver = {
		   .name = "rt286",
		   .owner = THIS_MODULE,
		   .acpi_match_table = ACPI_PTR(rt286_acpi_match),
		   },
	.probe = rt286_i2c_probe,
	.id_table = rt286_i2c_id,
};

module_i2c_driver(rt286_i2c_driver);

MODULE_DESCRIPTION("ASoC RT286 driver");
MODULE_AUTHOR("Bard Liao <bardliao@realtek.com>");
MODULE_LICENSE("GPL");
