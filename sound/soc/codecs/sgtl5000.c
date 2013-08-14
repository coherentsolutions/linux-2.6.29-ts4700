/*
 * sgtl5000.c  --  SGTL5000 ALSA SoC Audio driver
 *
 * Copyright 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include <mach/hardware.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "sgtl5000.h"


struct sgtl5000_priv {
	int sysclk;
	int master;
	int fmt;
	int rev;
	int lrclk;
	int capture_channels;
	int playback_active;
	int capture_active;
	struct snd_pcm_substream *master_substream;
	struct snd_pcm_substream *slave_substream;
};

static int sgtl5000_set_bias_level(struct snd_soc_codec *codec,
				   enum snd_soc_bias_level level);

#define SGTL5000_MAX_CACHED_REG SGTL5000_CHIP_SHORT_CTRL
static u16 sgtl5000_regs[(SGTL5000_MAX_CACHED_REG >> 1) + 1];

static unsigned int sgtl5000_read_reg_cache(struct snd_soc_codec *codec,
					    unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	unsigned int offset = reg >> 1;
	if (offset >= ARRAY_SIZE(sgtl5000_regs))
		return -EINVAL;
	pr_debug("r r:%02x,v:%04x\n", reg, cache[offset]);
	return cache[offset];
}

static unsigned int sgtl5000_hw_read(struct snd_soc_codec *codec,
				     unsigned int reg)
{
	struct i2c_client *client = codec->control_data;
	int i2c_ret;
	u16 value;
	u8 buf0[2], buf1[2];
	u16 addr = client->addr;
	u16 flags = client->flags;
	struct i2c_msg msg[2] = {
		{addr, flags, 2, buf0},
		{addr, flags | I2C_M_RD, 2, buf1},
	};

//	printk("sgtl5000_hw_read()\n");

	buf0[0] = (reg & 0xff00) >> 8;
	buf0[1] = reg & 0xff;
	i2c_ret = i2c_transfer(client->adapter, msg, 2);
	if (i2c_ret < 0) {
		pr_err("%s: read reg error : Reg 0x%02x\n", __func__, reg);
		return 0;
	}

	value = buf1[0] << 8 | buf1[1];

	pr_debug("r r:%02x,v:%04x\n", reg, value);
	return value;
}

static unsigned int sgtl5000_read(struct snd_soc_codec *codec, unsigned int reg)
{
   //printk("sgtl5000_read()\n");

	if ((reg == SGTL5000_CHIP_ID) ||
	    (reg == SGTL5000_CHIP_ADCDAC_CTRL) ||
	    (reg == SGTL5000_CHIP_ANA_STATUS) ||
	    (reg > SGTL5000_MAX_CACHED_REG))
		return sgtl5000_hw_read(codec, reg);
	else
		return sgtl5000_read_reg_cache(codec, reg);
}

static inline void sgtl5000_write_reg_cache(struct snd_soc_codec *codec,
					    u16 reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	unsigned int offset = reg >> 1;
	if (offset < ARRAY_SIZE(sgtl5000_regs))
		cache[offset] = value;
}

static int sgtl5000_write(struct snd_soc_codec *codec, unsigned int reg,
			  unsigned int value)
{
	struct i2c_client *client = codec->control_data;
	u16 addr = client->addr;
	u16 flags = client->flags;
	u8 buf[4];
	int i2c_ret;
	struct i2c_msg msg = { addr, flags, 4, buf };

//	printk("sgtl5000_write() reg: 0x%04X, value: 0x%04X\n", reg, value);

	sgtl5000_write_reg_cache(codec, reg, value);
	pr_debug("w r:%02x,v:%04x\n", reg, value);
	buf[0] = (reg & 0xff00) >> 8;
	buf[1] = reg & 0xff;
	buf[2] = (value & 0xff00) >> 8;
	buf[3] = value & 0xff;

	i2c_ret = i2c_transfer(client->adapter, &msg, 1);
	if (i2c_ret < 0) {
		pr_err("%s: write reg error : Reg 0x%02x = 0x%04x\n",
		       __func__, reg, value);
		return -EIO;
	}

	return i2c_ret;
}

static void sgtl5000_sync_reg_cache(struct snd_soc_codec *codec)
{
	int reg;
	for (reg = 0; reg <= SGTL5000_MAX_CACHED_REG; reg += 2)
		sgtl5000_write_reg_cache(codec, reg,
					 sgtl5000_hw_read(codec, reg));
}

static int sgtl5000_restore_reg(struct snd_soc_codec *codec, unsigned int reg)
{
	unsigned int cached_val, hw_val;

	cached_val = sgtl5000_read_reg_cache(codec, reg);
	hw_val = sgtl5000_hw_read(codec, reg);

	if (hw_val != cached_val)
		return sgtl5000_write(codec, reg, cached_val);

	return 0;
}

static int all_reg[] = {
	SGTL5000_CHIP_ID,
	SGTL5000_CHIP_DIG_POWER,
	SGTL5000_CHIP_CLK_CTRL,
	SGTL5000_CHIP_I2S_CTRL,
	SGTL5000_CHIP_SSS_CTRL,
	SGTL5000_CHIP_ADCDAC_CTRL,
	SGTL5000_CHIP_DAC_VOL,
	SGTL5000_CHIP_PAD_STRENGTH,
	SGTL5000_CHIP_ANA_ADC_CTRL,
	SGTL5000_CHIP_ANA_HP_CTRL,
	SGTL5000_CHIP_ANA_CTRL,
	SGTL5000_CHIP_LINREG_CTRL,
	SGTL5000_CHIP_REF_CTRL,
	SGTL5000_CHIP_MIC_CTRL,
	SGTL5000_CHIP_LINE_OUT_CTRL,
	SGTL5000_CHIP_LINE_OUT_VOL,
	SGTL5000_CHIP_ANA_POWER,
	SGTL5000_CHIP_PLL_CTRL,
	SGTL5000_CHIP_CLK_TOP_CTRL,
	SGTL5000_CHIP_ANA_STATUS,
	SGTL5000_CHIP_SHORT_CTRL,
};

#ifdef DEBUG
static void dump_reg(struct snd_soc_codec *codec)
{
	int i, reg;
	printk(KERN_DEBUG "dump begin\n");
	for (i = 0; i < 21; i++) {
		reg = sgtl5000_read(codec, all_reg[i]);
		printk(KERN_DEBUG "d r %04x, v %04x\n", all_reg[i], reg);
	}
	printk(KERN_DEBUG "dump end\n");
}
#else
static void dump_reg(struct snd_soc_codec *codec)
{
}
#endif

static int dac_mux_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_widget *widget = snd_kcontrol_chip(kcontrol);
	struct snd_soc_codec *codec = widget->codec;
	unsigned int reg;

	if (ucontrol->value.enumerated.item[0]) {
		reg = sgtl5000_read(codec, SGTL5000_CHIP_CLK_TOP_CTRL);
		reg |= SGTL5000_INT_OSC_EN;
		sgtl5000_write(codec, SGTL5000_CHIP_CLK_TOP_CTRL, reg);

		if (codec->bias_level != SND_SOC_BIAS_ON) {
			sgtl5000_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
			snd_soc_dapm_put_enum_double(kcontrol, ucontrol);
			sgtl5000_set_bias_level(codec, SND_SOC_BIAS_ON);
		} else
			snd_soc_dapm_put_enum_double(kcontrol, ucontrol);

		reg = sgtl5000_read(codec, SGTL5000_CHIP_ANA_CTRL);
		reg &= ~(SGTL5000_LINE_OUT_MUTE | SGTL5000_HP_MUTE);
		sgtl5000_write(codec, SGTL5000_CHIP_ANA_CTRL, reg);
	} else {
		reg = sgtl5000_read(codec, SGTL5000_CHIP_CLK_TOP_CTRL);
		reg &= ~SGTL5000_INT_OSC_EN;
		sgtl5000_write(codec, SGTL5000_CHIP_CLK_TOP_CTRL, reg);

		snd_soc_dapm_put_enum_double(kcontrol, ucontrol);
		sgtl5000_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	}
	return 0;
}

static const char *adc_mux_text[] = {
	"MIC_IN", "LINE_IN"
};

static const char *dac_mux_text[] = {
	"DAC", "LINE_IN"
};

static const struct soc_enum adc_enum =
SOC_ENUM_SINGLE(SGTL5000_CHIP_ANA_CTRL, 2, 2, adc_mux_text);

static const struct soc_enum dac_enum =
SOC_ENUM_SINGLE(SGTL5000_CHIP_ANA_CTRL, 6, 2, dac_mux_text);

static const struct snd_kcontrol_new adc_mux =
SOC_DAPM_ENUM("ADC Mux", adc_enum);

static const struct snd_kcontrol_new dac_mux = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DAC Mux",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE
	    | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info = snd_soc_info_enum_double,
	.get = snd_soc_dapm_get_enum_double,
	.put = dac_mux_put,
	.private_value = (unsigned long)&dac_enum,
};

static const struct snd_soc_dapm_widget sgtl5000_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("LINE_IN"),
	SND_SOC_DAPM_INPUT("MIC_IN"),

	SND_SOC_DAPM_OUTPUT("HP_OUT"),
	SND_SOC_DAPM_OUTPUT("LINE_OUT"),

	SND_SOC_DAPM_PGA("HP", SGTL5000_CHIP_ANA_CTRL, 4, 1, NULL, 0),
	SND_SOC_DAPM_PGA("LO", SGTL5000_CHIP_ANA_CTRL, 8, 1, NULL, 0),

	SND_SOC_DAPM_MUX("ADC Mux", SND_SOC_NOPM, 0, 0, &adc_mux),
	SND_SOC_DAPM_MUX("DAC Mux", SND_SOC_NOPM, 0, 0, &dac_mux),

	SND_SOC_DAPM_ADC("ADC", "Capture", SGTL5000_CHIP_DIG_POWER, 6, 0),
	SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"ADC Mux", "LINE_IN", "LINE_IN"},
	{"ADC Mux", "MIC_IN", "MIC_IN"},
	{"ADC", NULL, "ADC Mux"},
	{"DAC Mux", "DAC", "DAC"},
	{"DAC Mux", "LINE_IN", "LINE_IN"},
	{"LO", NULL, "DAC"},
	{"HP", NULL, "DAC Mux"},
	{"LINE_OUT", NULL, "LO"},
	{"HP_OUT", NULL, "HP"},
};

static int sgtl5000_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, sgtl5000_dapm_widgets,
				  ARRAY_SIZE(sgtl5000_dapm_widgets));

	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_new_widgets(codec);
	return 0;
}

static int dac_info_volsw(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xfc - 0x3c;
	return 0;
}

static int dac_get_volsw(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int reg, l, r;

	reg = sgtl5000_read(codec, SGTL5000_CHIP_DAC_VOL);
	l = (reg & SGTL5000_DAC_VOL_LEFT_MASK) << SGTL5000_DAC_VOL_LEFT_SHIFT;
	r = (reg & SGTL5000_DAC_VOL_RIGHT_MASK) << SGTL5000_DAC_VOL_RIGHT_SHIFT;
	l = l < 0x3c ? 0x3c : l;
	l = l > 0xfc ? 0xfc : l;
	r = r < 0x3c ? 0x3c : r;
	r = r > 0xfc ? 0xfc : r;
	l = 0xfc - l;
	r = 0xfc - r;

	ucontrol->value.integer.value[0] = l;
	ucontrol->value.integer.value[1] = l;

	return 0;
}

static int dac_put_volsw(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec = snd_kcontrol_chip(kcontrol);
	int reg, l, r;

	l = ucontrol->value.integer.value[0];
	r = ucontrol->value.integer.value[1];

	l = l < 0 ? 0 : l;
	l = l > 0xfc - 0x3c ? 0xfc - 0x3c : l;
	r = r < 0 ? 0 : r;
	r = r > 0xfc - 0x3c ? 0xfc - 0x3c : r;
	l = 0xfc - l;
	r = 0xfc - r;

	reg = l << SGTL5000_DAC_VOL_LEFT_SHIFT |
	    r << SGTL5000_DAC_VOL_RIGHT_SHIFT;

	sgtl5000_write(codec, SGTL5000_CHIP_DAC_VOL, reg);

	return 0;
}

static const char *mic_gain_text[] = {
	"0dB", "20dB", "30dB", "40dB"
};

static const char *adc_m6db_text[] = {
	"No Change", "Reduced by 6dB"
};

static const struct soc_enum mic_gain =
SOC_ENUM_SINGLE(SGTL5000_CHIP_MIC_CTRL, 0, 4, mic_gain_text);

static const struct soc_enum adc_m6db =
SOC_ENUM_SINGLE(SGTL5000_CHIP_ANA_ADC_CTRL, 8, 2, adc_m6db_text);

static const struct snd_kcontrol_new sgtl5000_snd_controls[] = {
	SOC_ENUM("MIC GAIN", mic_gain),
	SOC_DOUBLE("Capture Volume", SGTL5000_CHIP_ANA_ADC_CTRL, 0, 4, 0xf, 0),
	SOC_ENUM("Capture Vol Reduction", adc_m6db),
	{.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	 .name = "Playback Volume",
	 .access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
	 SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	 .info = dac_info_volsw,
	 .get = dac_get_volsw,
	 .put = dac_put_volsw,
	 },
	SOC_DOUBLE("Headphone Volume", SGTL5000_CHIP_ANA_HP_CTRL, 0, 8, 0x7f,
		   1),
};

static int sgtl5000_add_controls(struct snd_soc_codec *codec)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(sgtl5000_snd_controls); i++) {
		err = snd_ctl_add(codec->card,
				  snd_soc_cnew(&sgtl5000_snd_controls[i],
					       codec, NULL));
		if (err < 0)
			return err;
	}

	return 0;
}

static int sgtl5000_digital_mute(struct snd_soc_dai *codec_dai, int mute)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 adcdac_ctrl;

	adcdac_ctrl = sgtl5000_read(codec, SGTL5000_CHIP_ADCDAC_CTRL);

	if (mute) {
		adcdac_ctrl |= SGTL5000_DAC_MUTE_LEFT;
		adcdac_ctrl |= SGTL5000_DAC_MUTE_RIGHT;
	} else {
		adcdac_ctrl &= ~SGTL5000_DAC_MUTE_LEFT;
		adcdac_ctrl &= ~SGTL5000_DAC_MUTE_RIGHT;
	}

	sgtl5000_write(codec, SGTL5000_CHIP_ADCDAC_CTRL, adcdac_ctrl);
	if (!mute)
		dump_reg(codec);
	return 0;
}

static int sgtl5000_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct sgtl5000_priv *sgtl5000 = codec->private_data;
	u16 i2sctl = 0;

	//printk("sgtl5000_set_dai_fmt() 0x%08lX\n", fmt);

	pr_debug("%s:fmt=%08x\n", __func__, fmt);
	sgtl5000->master = 0;
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		i2sctl |= SGTL5000_I2S_MASTER;
		sgtl5000->master = 1;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
	case SND_SOC_DAIFMT_CBS_CFM:
		return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_DSP_A:
		i2sctl |= SGTL5000_I2S_MODE_PCM;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		i2sctl |= SGTL5000_I2S_MODE_PCM;
		i2sctl |= SGTL5000_I2S_LRALIGN;
		break;
	case SND_SOC_DAIFMT_I2S:
		i2sctl |= SGTL5000_I2S_MODE_I2S_LJ;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		i2sctl |= SGTL5000_I2S_MODE_RJ;
		i2sctl |= SGTL5000_I2S_LRPOL;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		i2sctl |= SGTL5000_I2S_MODE_I2S_LJ;
		i2sctl |= SGTL5000_I2S_LRALIGN;
		break;
	default:
		return -EINVAL;
	}
	sgtl5000->fmt = fmt & SND_SOC_DAIFMT_FORMAT_MASK;

	/* Clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
	case SND_SOC_DAIFMT_NB_IF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
	case SND_SOC_DAIFMT_IB_NF:
		i2sctl |= SGTL5000_I2S_SCLK_INV;
		break;
	default:
		return -EINVAL;
	}
	sgtl5000_write(codec, SGTL5000_CHIP_I2S_CTRL, i2sctl);

	return 0;
}

static int sgtl5000_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				   int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	struct sgtl5000_priv *sgtl5000 = codec->private_data;

	switch (clk_id) {
	case SGTL5000_SYSCLK:
//	   printk("set SGTL5000_SYSCLK %d\n", freq);
		sgtl5000->sysclk = freq;
		break;
	case SGTL5000_LRCLK:
//	   printk("set SGTL5000_LRCLK %d\n", freq);
		sgtl5000->lrclk = freq;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int sgtl5000_pcm_prepare(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct sgtl5000_priv *sgtl5000 = codec->private_data;
	int reg;

	reg = sgtl5000_read(codec, SGTL5000_CHIP_DIG_POWER);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		reg |= SGTL5000_I2S_IN_POWERUP;
	else
		reg |= SGTL5000_I2S_OUT_POWERUP;
	sgtl5000_write(codec, SGTL5000_CHIP_DIG_POWER, reg);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		reg = sgtl5000_read(codec, SGTL5000_CHIP_ANA_POWER);
		reg |= SGTL5000_ADC_POWERUP;
		if (sgtl5000->capture_channels == 1)
			reg &= ~SGTL5000_ADC_STEREO;
		else
			reg |= SGTL5000_ADC_STEREO;
		sgtl5000_write(codec, SGTL5000_CHIP_ANA_POWER, reg);
	}

	return 0;
}

static int sgtl5000_pcm_startup(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct sgtl5000_priv *sgtl5000 = codec->private_data;
	struct snd_pcm_runtime *master_runtime;

	//printk("sgtl5000_pcm_startup()\n");

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		sgtl5000->playback_active++;
	else
		sgtl5000->capture_active++;

	/* The DAI has shared clocks so if we already have a playback or
	 * capture going then constrain this substream to match it.
	 */
	if (sgtl5000->master_substream) {
		master_runtime = sgtl5000->master_substream->runtime;

		pr_debug("Constraining to %d bits at %dHz\n",
			 master_runtime->sample_bits, master_runtime->rate);

		snd_pcm_hw_constraint_minmax(substream->runtime,
					     SNDRV_PCM_HW_PARAM_RATE,
					     master_runtime->rate,
					     master_runtime->rate);

		snd_pcm_hw_constraint_minmax(substream->runtime,
					     SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
					     master_runtime->sample_bits,
					     master_runtime->sample_bits);

		sgtl5000->slave_substream = substream;
	} else
		sgtl5000->master_substream = substream;

	return 0;
}

static void sgtl5000_pcm_shutdown(struct snd_pcm_substream *substream, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct sgtl5000_priv *sgtl5000 = codec->private_data;
	int reg, dig_pwr, ana_pwr;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		sgtl5000->playback_active--;
	else
		sgtl5000->capture_active--;

	if (sgtl5000->master_substream == substream)
		sgtl5000->master_substream = sgtl5000->slave_substream;

	sgtl5000->slave_substream = NULL;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		ana_pwr = sgtl5000_read(codec, SGTL5000_CHIP_ANA_POWER);
		ana_pwr &= ~(SGTL5000_ADC_POWERUP | SGTL5000_ADC_STEREO);
		sgtl5000_write(codec, SGTL5000_CHIP_ANA_POWER, ana_pwr);
	}

	dig_pwr = sgtl5000_read(codec, SGTL5000_CHIP_DIG_POWER);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dig_pwr &= ~SGTL5000_I2S_IN_POWERUP;
	else
		dig_pwr &= ~SGTL5000_I2S_OUT_POWERUP;
	sgtl5000_write(codec, SGTL5000_CHIP_DIG_POWER, dig_pwr);

	if (!sgtl5000->playback_active && !sgtl5000->capture_active) {
		reg = sgtl5000_read(codec, SGTL5000_CHIP_I2S_CTRL);
		reg &= ~SGTL5000_I2S_MASTER;
		sgtl5000_write(codec, SGTL5000_CHIP_I2S_CTRL, reg);
	}
}

/*
 * Set PCM DAI bit size and sample rate.
 * input: params_rate, params_fmt
 */
static int sgtl5000_pcm_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct sgtl5000_priv *sgtl5000 = codec->private_data;
	int channels = params_channels(params);
	int clk_ctl = 0;
	int pll_ctl = 0;
	int i2s_ctl;
	int div2 = 0;
	int reg;

	//printk("sgtl5000_pcm_hw_params(), channels=%d\n", channels);

	pr_debug("%s channels=%d\n", __func__, channels);

	if (!sgtl5000->sysclk) {
		pr_err("%s: set sysclk first!\n", __func__);
		return -EFAULT;
	}

	if (substream == sgtl5000->slave_substream) {
		pr_debug("Ignoring hw_params for slave substream\n");
		return 0;
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		sgtl5000->capture_channels = channels;

	switch (sgtl5000->lrclk) {
	case 32000:
		clk_ctl |= SGTL5000_SYS_FS_32k << SGTL5000_SYS_FS_SHIFT;
		break;
	case 44100:
		clk_ctl |= SGTL5000_SYS_FS_44_1k << SGTL5000_SYS_FS_SHIFT;
		break;
	case 48000:
		clk_ctl |= SGTL5000_SYS_FS_48k << SGTL5000_SYS_FS_SHIFT;
		break;
	case 96000:
		clk_ctl |= SGTL5000_SYS_FS_96k << SGTL5000_SYS_FS_SHIFT;
		break;
	default:
		pr_err("%s: sample rate %d not supported\n", __func__,
		       sgtl5000->lrclk);
		return -EFAULT;
	}

#if 0	/* SGTL5000 rev1 has a IC bug to prevent switching to MCLK from PLL. */
	if (fs * 256 == sgtl5000->sysclk)
		clk_ctl |= SGTL5000_MCLK_FREQ_256FS << SGTL5000_MCLK_FREQ_SHIFT;
	else if (fs * 384 == sgtl5000->sysclk && fs != 96000)
		clk_ctl |= SGTL5000_MCLK_FREQ_384FS << SGTL5000_MCLK_FREQ_SHIFT;
	else if (fs * 512 == sgtl5000->sysclk && fs != 96000)
		clk_ctl |= SGTL5000_MCLK_FREQ_512FS << SGTL5000_MCLK_FREQ_SHIFT;
	else
#endif
	{
		if (!sgtl5000->master) {
			pr_err("%s: PLL not supported in slave mode\n",
			       __func__);
			return -EINVAL;
		}
		clk_ctl |= SGTL5000_MCLK_FREQ_PLL << SGTL5000_MCLK_FREQ_SHIFT;
	}

	if ((clk_ctl & SGTL5000_MCLK_FREQ_MASK) == SGTL5000_MCLK_FREQ_PLL) {
		u64 out, t;
		unsigned int in, int_div, frac_div;
		if (sgtl5000->sysclk > 17000000) {
			div2 = 1;
			in = sgtl5000->sysclk / 2;
		} else {
			div2 = 0;
			in = sgtl5000->sysclk;
		}
		if (sgtl5000->lrclk == 44100)
			out = 180633600;
		else
			out = 196608000;
		t = do_div(out, in);
		int_div = out;
		t *= 2048;
		do_div(t, in);
		frac_div = t;

		//printk("int_div=%d, frac_div=%d\n", int_div, frac_div);

		pll_ctl = int_div << SGTL5000_PLL_INT_DIV_SHIFT |
		    frac_div << SGTL5000_PLL_FRAC_DIV_SHIFT;
	}

	i2s_ctl = sgtl5000_read(codec, SGTL5000_CHIP_I2S_CTRL);
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		if (sgtl5000->fmt == SND_SOC_DAIFMT_RIGHT_J)
			return -EINVAL;
		i2s_ctl |= SGTL5000_I2S_DLEN_16 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_32FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		i2s_ctl |= SGTL5000_I2S_DLEN_20 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_64FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		i2s_ctl |= SGTL5000_I2S_DLEN_24 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_64FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		if (sgtl5000->fmt == SND_SOC_DAIFMT_RIGHT_J)
			return -EINVAL;
		i2s_ctl |= SGTL5000_I2S_DLEN_32 << SGTL5000_I2S_DLEN_SHIFT;
		i2s_ctl |= SGTL5000_I2S_SCLKFREQ_64FS <<
		    SGTL5000_I2S_SCLKFREQ_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	//printk("sysclk=%d, fs=%d,clk_ctl=%d,pll_ctl=0x%04X,i2s_ctl=0x%04X,div2=%d\n",
		// sgtl5000->sysclk, sgtl5000->lrclk, clk_ctl, pll_ctl, i2s_ctl, div2);

	if ((clk_ctl & SGTL5000_MCLK_FREQ_MASK) == SGTL5000_MCLK_FREQ_PLL) {
		sgtl5000_write(codec, SGTL5000_CHIP_PLL_CTRL, pll_ctl);
		reg = sgtl5000_read(codec, SGTL5000_CHIP_CLK_TOP_CTRL);
		if (div2)
			reg |= SGTL5000_INPUT_FREQ_DIV2;
		else
			reg &= ~SGTL5000_INPUT_FREQ_DIV2;
		sgtl5000_write(codec, SGTL5000_CHIP_CLK_TOP_CTRL, reg);
		reg = sgtl5000_read(codec, SGTL5000_CHIP_ANA_POWER);
		reg |= SGTL5000_PLL_POWERUP | SGTL5000_VCOAMP_POWERUP;
		sgtl5000_write(codec, SGTL5000_CHIP_ANA_POWER, reg);
	}
	sgtl5000_write(codec, SGTL5000_CHIP_CLK_CTRL, clk_ctl);
	sgtl5000_write(codec, SGTL5000_CHIP_I2S_CTRL, i2s_ctl);

	return 0;
}

static void sgtl5000_mic_bias(struct snd_soc_codec *codec, int enable)
{
	int reg, bias_r = 0;
	if (enable)
		bias_r = SGTL5000_BIAS_R_4k << SGTL5000_BIAS_R_SHIFT;
	reg = sgtl5000_read(codec, SGTL5000_CHIP_MIC_CTRL);
	if ((reg & SGTL5000_BIAS_R_MASK) != bias_r) {
		reg &= ~SGTL5000_BIAS_R_MASK;
		reg |= bias_r;
		sgtl5000_write(codec, SGTL5000_CHIP_MIC_CTRL, reg);
	}
}

static int sgtl5000_set_bias_level(struct snd_soc_codec *codec,
				   enum snd_soc_bias_level level)
{
	u16 reg, ana_pwr;
	int delay = 0;
	pr_debug("dapm level %d\n", level);
	switch (level) {
	case SND_SOC_BIAS_ON:		/* full On */
		if (codec->bias_level == SND_SOC_BIAS_ON)
			break;

		sgtl5000_mic_bias(codec, 1);

		reg = sgtl5000_read(codec, SGTL5000_CHIP_ANA_POWER);
		reg |= SGTL5000_VAG_POWERUP;
		sgtl5000_write(codec, SGTL5000_CHIP_ANA_POWER, reg);
		msleep(400);

		break;

	case SND_SOC_BIAS_PREPARE:	/* partial On */
		if (codec->bias_level == SND_SOC_BIAS_PREPARE)
			break;

		sgtl5000_mic_bias(codec, 0);

		/* must power up hp/line out before vag & dac to
		   avoid pops. */
		reg = sgtl5000_read(codec, SGTL5000_CHIP_ANA_POWER);
		if (reg & SGTL5000_VAG_POWERUP)
			delay = 400;
		reg &= ~SGTL5000_VAG_POWERUP;
		reg |= SGTL5000_DAC_POWERUP;
		reg |= SGTL5000_HP_POWERUP;
		reg |= SGTL5000_LINE_OUT_POWERUP;
		sgtl5000_write(codec, SGTL5000_CHIP_ANA_POWER, reg);
		if (delay)
			msleep(delay);

		reg = sgtl5000_read(codec, SGTL5000_CHIP_DIG_POWER);
		reg |= SGTL5000_DAC_EN;
		sgtl5000_write(codec, SGTL5000_CHIP_DIG_POWER, reg);

		break;

	case SND_SOC_BIAS_STANDBY:	/* Off, with power */
		/* soc doesn't do PREPARE state after record so make sure
		   that anything that needs to be turned OFF gets turned off. */
		if (codec->bias_level == SND_SOC_BIAS_STANDBY)
			break;

		/* soc calls digital_mute to unmute before record but doesn't
		   call digital_mute to mute after record. */
		sgtl5000_digital_mute(&sgtl5000_dai, 1);

		sgtl5000_mic_bias(codec, 0);

		reg = sgtl5000_read(codec, SGTL5000_CHIP_ANA_POWER);
		if (reg & SGTL5000_VAG_POWERUP) {
			reg &= ~SGTL5000_VAG_POWERUP;
			sgtl5000_write(codec, SGTL5000_CHIP_ANA_POWER, reg);
			msleep(400);
		}
		reg &= ~SGTL5000_DAC_POWERUP;
		reg &= ~SGTL5000_HP_POWERUP;
		reg &= ~SGTL5000_LINE_OUT_POWERUP;
		sgtl5000_write(codec, SGTL5000_CHIP_ANA_POWER, reg);

		reg = sgtl5000_read(codec, SGTL5000_CHIP_DIG_POWER);
		reg &= ~SGTL5000_DAC_EN;
		sgtl5000_write(codec, SGTL5000_CHIP_DIG_POWER, reg);

		break;

	case SND_SOC_BIAS_OFF:	/* Off, without power */
		/* must power down hp/line out after vag & dac to
		   avoid pops. */
		reg = sgtl5000_read(codec, SGTL5000_CHIP_ANA_POWER);
		ana_pwr = reg;
		reg &= ~SGTL5000_VAG_POWERUP;

		/* Workaround for sgtl5000 rev 0x11 chip audio suspend failure
		   issue on mx25 */
		/* reg &= ~SGTL5000_REFTOP_POWERUP; */

		sgtl5000_write(codec, SGTL5000_CHIP_ANA_POWER, reg);
		msleep(600);

		reg &= ~SGTL5000_HP_POWERUP;
		reg &= ~SGTL5000_LINE_OUT_POWERUP;
		reg &= ~SGTL5000_DAC_POWERUP;
		reg &= ~SGTL5000_ADC_POWERUP;
		sgtl5000_write(codec, SGTL5000_CHIP_ANA_POWER, reg);

		/* save ANA POWER register value for resume */
		sgtl5000_write_reg_cache(codec, SGTL5000_CHIP_ANA_POWER,
					 ana_pwr);
		break;
	}
	codec->bias_level = level;
	return 0;
}

#define SGTL5000_RATES (SNDRV_PCM_RATE_32000 |\
		      SNDRV_PCM_RATE_44100 |\
		      SNDRV_PCM_RATE_48000 |\
		      SNDRV_PCM_RATE_96000)

#define SGTL5000_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			SNDRV_PCM_FMTBIT_S20_3LE |\
			SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops sgtl5000_dai_ops = {
   .hw_params  = sgtl5000_pcm_hw_params,
   .digital_mute  = sgtl5000_digital_mute,
   .set_fmt = sgtl5000_set_dai_fmt,
   .set_sysclk = sgtl5000_set_dai_sysclk,

   .prepare = sgtl5000_pcm_prepare,
   .startup = sgtl5000_pcm_startup,
   .shutdown = sgtl5000_pcm_shutdown,
};

struct snd_soc_dai sgtl5000_dai = {
	.name = "SGTL5000",
	.playback = {
		     .stream_name = "Playback",
		     .channels_min = 2,
		     .channels_max = 2,
		     .rates = SGTL5000_RATES,
		     .formats = SGTL5000_FORMATS,
		     },
	.capture = {
		    .stream_name = "Capture",
		    .channels_min = 1,
		    .channels_max = 2,
		    .rates = SGTL5000_RATES,
		    .formats = SGTL5000_FORMATS,
		    },
	.ops = &sgtl5000_dai_ops,
	/* {
		.prepare = sgtl5000_pcm_prepare,
		.startup = sgtl5000_pcm_startup,
		.shutdown = sgtl5000_pcm_shutdown,
		.hw_params = sgtl5000_pcm_hw_params,
		},
	.dai_ops = {
		    .digital_mute = sgtl5000_digital_mute,
		    .set_fmt = sgtl5000_set_dai_fmt,
		    .set_sysclk = sgtl5000_set_dai_sysclk}
		    */
};
EXPORT_SYMBOL_GPL(sgtl5000_dai);

static int sgtl5000_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	sgtl5000_set_bias_level(codec, SND_SOC_BIAS_OFF);

	return 0;
}

static int sgtl5000_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	unsigned int i;

	/* Restore refs first in same order as in sgtl5000_init */
	sgtl5000_restore_reg(codec, SGTL5000_CHIP_LINREG_CTRL);
	sgtl5000_restore_reg(codec, SGTL5000_CHIP_ANA_POWER);
	msleep(10);
	sgtl5000_restore_reg(codec, SGTL5000_CHIP_REF_CTRL);
	sgtl5000_restore_reg(codec, SGTL5000_CHIP_LINE_OUT_CTRL);

	/* Restore everythine else */
	for (i = 1; i < sizeof(all_reg) / sizeof(int); i++)
		sgtl5000_restore_reg(codec, all_reg[i]);

	sgtl5000_write(codec, SGTL5000_DAP_CTRL, 0);

	/* Bring the codec back up to standby first to minimise pop/clicks */
	sgtl5000_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	if (codec->suspend_bias_level == SND_SOC_BIAS_ON)
		sgtl5000_set_bias_level(codec, SND_SOC_BIAS_PREPARE);
	sgtl5000_set_bias_level(codec, codec->suspend_bias_level);

	return 0;
}

/*
 * initialise the SGTL5000 driver
 * register the mixer and dsp interfaces with the kernel
 */
static int sgtl5000_init(struct snd_soc_device *socdev)
{
	struct sgtl5000_platform_data *plat = socdev->codec_data;
	struct snd_soc_codec *codec = socdev->card->codec;
	struct i2c_client *client = codec->control_data;
	struct sgtl5000_priv *sgtl5000 = codec->private_data;
	u16 reg, ana_pwr, lreg_ctrl, ref_ctrl, lo_ctrl, short_ctrl, sss;
	int vag;
	unsigned int val;
	int ret = 0;

	//printk("sgtl5000_init()\n");

	val = sgtl5000_read(codec, SGTL5000_CHIP_ID);
	if (((val & SGTL5000_PARTID_MASK) >> SGTL5000_PARTID_SHIFT) !=
	    SGTL5000_PARTID_PART_ID) {
		pr_err("Device with ID register %x is not a SGTL5000\n", val);
		return -ENODEV;
	}

	sgtl5000->rev = (val & SGTL5000_REVID_MASK) >> SGTL5000_REVID_SHIFT;
	dev_info(&client->dev, "SGTL5000 revision %d\n", sgtl5000->rev);

	codec->name = "SGTL5000";
	codec->owner = THIS_MODULE;
	codec->read = sgtl5000_read_reg_cache;
	codec->write = sgtl5000_write;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = sgtl5000_set_bias_level;
	codec->dai = &sgtl5000_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = sizeof(sgtl5000_regs);
	codec->reg_cache_step = 2;
	codec->reg_cache = (void *)&sgtl5000_regs;
	if (codec->reg_cache == NULL) {
		dev_err(&client->dev, "Failed to allocate register cache\n");
		return -ENOMEM;
	}

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(&client->dev, "failed to create pcms\n");
		return ret;
	}

	sgtl5000_sync_reg_cache(codec);

	/* reset value */
	ana_pwr = SGTL5000_DAC_STEREO |
	    SGTL5000_LINREG_SIMPLE_POWERUP |
	    SGTL5000_STARTUP_POWERUP |
	    SGTL5000_ADC_STEREO | SGTL5000_REFTOP_POWERUP;
	lreg_ctrl = 0;
	ref_ctrl = 0;
	lo_ctrl = 0;
	short_ctrl = 0;
	sss = SGTL5000_DAC_SEL_I2S_IN << SGTL5000_DAC_SEL_SHIFT;

	/* workaround for rev 0x11: use vddd linear regulator */
	if (!plat->vddd || (sgtl5000->rev >= 0x11)) {
		/* set VDDD to 1.2v */
		lreg_ctrl |= 0x8 << SGTL5000_LINREG_VDDD_SHIFT;
		/* power internal linear regulator */
		ana_pwr |= SGTL5000_LINEREG_D_POWERUP;
	} else {
		/* turn of startup power */
		ana_pwr &= ~SGTL5000_STARTUP_POWERUP;
		ana_pwr &= ~SGTL5000_LINREG_SIMPLE_POWERUP;
	}
	if (plat->vddio < 3100 && plat->vdda < 3100) {
		/* Enable VDDC charge pump */
		ana_pwr |= SGTL5000_VDDC_CHRGPMP_POWERUP;
	}
	if (plat->vddio >= 3100 && plat->vdda >= 3100) {
		/* VDDC use VDDIO rail */
		lreg_ctrl |= SGTL5000_VDDC_ASSN_OVRD;
		if (plat->vddio >= 3100)
			lreg_ctrl |= SGTL5000_VDDC_MAN_ASSN_VDDIO <<
			    SGTL5000_VDDC_MAN_ASSN_SHIFT;
	}
	/* If PLL is powered up (such as on power cycle) leave it on. */
	reg = sgtl5000_read(codec, SGTL5000_CHIP_ANA_POWER);
	ana_pwr |= reg & (SGTL5000_PLL_POWERUP | SGTL5000_VCOAMP_POWERUP);

	/* set ADC/DAC ref voltage to vdda/2 */
	vag = plat->vdda / 2;
	if (vag <= SGTL5000_ANA_GND_BASE)
		vag = 0;
	else if (vag >= SGTL5000_ANA_GND_BASE + SGTL5000_ANA_GND_STP *
		 (SGTL5000_ANA_GND_MASK >> SGTL5000_ANA_GND_SHIFT))
		vag = SGTL5000_ANA_GND_MASK >> SGTL5000_ANA_GND_SHIFT;
	else
		vag = (vag - SGTL5000_ANA_GND_BASE) / SGTL5000_ANA_GND_STP;
	ref_ctrl |= vag << SGTL5000_ANA_GND_SHIFT;

	/* set line out ref voltage to vddio/2 */
	vag = plat->vddio / 2;
	if (vag <= SGTL5000_LINE_OUT_GND_BASE)
		vag = 0;
	else if (vag >= SGTL5000_LINE_OUT_GND_BASE + SGTL5000_LINE_OUT_GND_STP *
		 SGTL5000_LINE_OUT_GND_MAX)
		vag = SGTL5000_LINE_OUT_GND_MAX;
	else
		vag = (vag - SGTL5000_LINE_OUT_GND_BASE) /
		    SGTL5000_LINE_OUT_GND_STP;
	lo_ctrl |= vag << SGTL5000_LINE_OUT_GND_SHIFT;

	/* enable small pop */
	ref_ctrl |= SGTL5000_SMALL_POP;

	/* Controls the output bias current for the lineout */
	lo_ctrl |= (SGTL5000_LINE_OUT_CURRENT_360u << SGTL5000_LINE_OUT_CURRENT_SHIFT);

	/* set short detect */
	/* keep default */

	/* set routing */
	/* keep default, bypass DAP */

	sgtl5000_write(codec, SGTL5000_CHIP_LINREG_CTRL, lreg_ctrl);
	sgtl5000_write(codec, SGTL5000_CHIP_ANA_POWER, ana_pwr);
	msleep(10);

	/* For rev 0x11, if vddd linear reg has been enabled, we have
	   to disable simple reg to get proper VDDD voltage.  */
	if ((ana_pwr & SGTL5000_LINEREG_D_POWERUP) && (sgtl5000->rev >= 0x11)) {
		ana_pwr &= ~SGTL5000_LINREG_SIMPLE_POWERUP;
		sgtl5000_write(codec, SGTL5000_CHIP_ANA_POWER, ana_pwr);
		msleep(10);
	}

	sgtl5000_write(codec, SGTL5000_CHIP_REF_CTRL, ref_ctrl);
	sgtl5000_write(codec, SGTL5000_CHIP_LINE_OUT_CTRL, lo_ctrl);
	sgtl5000_write(codec, SGTL5000_CHIP_SHORT_CTRL, short_ctrl);
	sgtl5000_write(codec, SGTL5000_CHIP_SSS_CTRL, sss);
	sgtl5000_write(codec, SGTL5000_CHIP_DIG_POWER, 0);

	reg = SGTL5000_DAC_VOL_RAMP_EN |
	    SGTL5000_DAC_MUTE_RIGHT | SGTL5000_DAC_MUTE_LEFT;
	sgtl5000_write(codec, SGTL5000_CHIP_ADCDAC_CTRL, reg);

//	if (cpu_is_mx25())
	//	sgtl5000_write(codec, SGTL5000_CHIP_PAD_STRENGTH, 0x01df);
	//else
		sgtl5000_write(codec, SGTL5000_CHIP_PAD_STRENGTH, 0x015f);

	reg = sgtl5000_read(codec, SGTL5000_CHIP_ANA_ADC_CTRL);
	reg &= ~SGTL5000_ADC_VOL_M6DB;
	reg &= ~(SGTL5000_ADC_VOL_LEFT_MASK | SGTL5000_ADC_VOL_RIGHT_MASK);
	reg |= (0xf << SGTL5000_ADC_VOL_LEFT_SHIFT)
	    | (0xf << SGTL5000_ADC_VOL_RIGHT_SHIFT);
	sgtl5000_write(codec, SGTL5000_CHIP_ANA_ADC_CTRL, reg);

	reg = SGTL5000_LINE_OUT_MUTE | SGTL5000_HP_MUTE |
	    SGTL5000_HP_ZCD_EN | SGTL5000_ADC_ZCD_EN;
	sgtl5000_write(codec, SGTL5000_CHIP_ANA_CTRL, reg);

	sgtl5000_write(codec, SGTL5000_CHIP_MIC_CTRL, 0);
	sgtl5000_write(codec, SGTL5000_CHIP_CLK_TOP_CTRL, 0);
	/* disable DAP */
	sgtl5000_write(codec, SGTL5000_DAP_CTRL, 0);
	/* TODO: initialize DAP */

	sgtl5000_add_controls(codec);
	sgtl5000_add_widgets(codec);

	sgtl5000_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		printk(KERN_ERR "sgtl5000: failed to register card\n");
		snd_soc_free_pcms(socdev);
		snd_soc_dapm_free(socdev);
		return ret;
	}

	return 0;
}

static struct snd_soc_device *sgtl5000_socdev;

static int sgtl5000_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct snd_soc_device *socdev = sgtl5000_socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	int ret;

	//printk("sgtl5000_i2c_probe\n");

	i2c_set_clientdata(i2c, codec);
	codec->control_data = i2c;

	ret = sgtl5000_init(socdev);
	if (ret < 0)
		dev_err(&i2c->dev, "Device initialisation failed\n");

	return ret;
}

static const struct i2c_device_id sgtl5000_id[] = {
	{"sgtl5000-i2c", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, sgtl5000_id);

static struct i2c_driver sgtl5000_i2c_driver = {
	.driver = {
		   .name = "sgtl5000-i2c",
		   .owner = THIS_MODULE,
		   },
	.probe = sgtl5000_i2c_probe,
	.id_table = sgtl5000_id,
};

static int sgtl5000_add_i2c_device(struct platform_device *pdev,
             const struct sgtl5000_platform_data *setup)
{
   struct i2c_board_info info;
   struct i2c_adapter *adapter;
   struct i2c_client *client;
   int ret;

   ret = i2c_add_driver(&sgtl5000_i2c_driver);
   if (ret != 0) {
      dev_err(&pdev->dev, "can't add i2c driver\n");
      return ret;
   }

   memset(&info, 0, sizeof(struct i2c_board_info));
   info.addr = setup->i2c_address;
   strlcpy(info.type, "sgtl5000-i2c", I2C_NAME_SIZE);

   adapter = i2c_get_adapter(setup->i2c_bus);
   if (!adapter) {
      dev_err(&pdev->dev, "can't get i2c adapter %d\n",
         setup->i2c_bus);
      goto err_driver;
   }

   client = i2c_new_device(adapter, &info);
   i2c_put_adapter(adapter);
   if (!client) {
      dev_err(&pdev->dev, "can't add i2c device at 0x%x\n",
         (unsigned int)info.addr);
      goto err_driver;
   }

   return 0;

err_driver:
   i2c_del_driver(&sgtl5000_i2c_driver);
   return -ENODEV;
}



static int sgtl5000_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	struct sgtl5000_platform_data *setup = socdev->codec_data;
	struct sgtl5000_priv *sgtl5000;
	int ret = 0;

	//printk("sgtl5000_probe()\n");

	codec = kzalloc(sizeof(struct snd_soc_codec), GFP_KERNEL);
	if (codec == NULL)
		return -ENOMEM;

	sgtl5000 = kzalloc(sizeof(struct sgtl5000_priv), GFP_KERNEL);
	if (sgtl5000 == NULL) {
		kfree(codec);
		return -ENOMEM;
	}

	codec->private_data = sgtl5000;
	socdev->card->codec = codec;
	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);
	sgtl5000_socdev = socdev;

	ret = -ENODEV;

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
   if (setup->i2c_address) {

      //printk("sgtl5000_probe: i2c address = 0x%04X\n", setup->i2c_address);
      codec->hw_write = (hw_write_t)i2c_master_send;
      ret = sgtl5000_add_i2c_device(pdev, setup);
   }
#endif

	if (ret != 0) {
		dev_err(&pdev->dev, "can't add i2c device\n");
		kfree(codec->private_data);
		kfree(codec);
	}

	return ret;
}

/* power down chip */
static int sgtl5000_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	if (codec->control_data)
		sgtl5000_set_bias_level(codec, SND_SOC_BIAS_OFF);
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
	i2c_del_driver(&sgtl5000_i2c_driver);
	kfree(codec->private_data);
	kfree(codec);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_sgtl5000 = {
	.probe = sgtl5000_probe,
	.remove = sgtl5000_remove,
	.suspend = sgtl5000_suspend,
	.resume = sgtl5000_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_sgtl5000);


static int __init sgtl5000_modinit(void)
{
   return snd_soc_register_dai(&sgtl5000_dai);
}
module_init(sgtl5000_modinit);

static void __exit sgtl5000_exit(void)
{
   snd_soc_unregister_dai(&sgtl5000_dai);
}
module_exit(sgtl5000_exit);


MODULE_DESCRIPTION("ASoC SGTL5000 driver");
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_LICENSE("GPL");
