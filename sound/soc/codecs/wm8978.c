/*
 * wm8978.c  --  WM8978 ALSA Soc Audio driver
 *
 * Copyright 2007 Wolfson Microelectronics PLC.
 *
 * Authors: Liam Girdwood <lg@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "wm8978.h"

#ifdef JEFFR_DEBUG
#define pr_jeffr pr_info
#else
#define pr_jeffr if(0) pr_info
#endif

#define trace(FMT, ARGS...) printk("%s at line %d: "FMT"\n", __FILE__, __LINE__, ## ARGS)

#define possibly_unused __attribute__ ((unused))
#define caseAsString(X) case X: return #X
#define unknownCase(X) default: { \
		static char buf[32]; \
		sprintf(buf, "<unknown: %d>", X); \
		return( buf); \
		};
struct snd_soc_codec_device soc_codec_dev_wm8978;

/*
 * wm8978 register cache
 * We can't read the WM8978 register space when we are
 * using 2 wire for device control, so we cache them instead.
 */

static const u16 wm8978_reg[WM8978_CACHEREGNUM] = {
    0x0000, 0x001d, 0x01bf, 0x01ef,  /* 0 - 3 */
    0x0010, 0x0000, 0x006c, 0x0001,  /* 4 - 7 */
    0x0000, 0x0050, 0x0000, 0x00ff,  /* 8 - 11 */
    0x00ff, 0x0012, 0x0100, 0x00ff,  /* 12 - 15 */
    0x00ff, 0x0000, 0x0126, 0x002c,  /* 16 - 19 */
    0x002e, 0x002c, 0x0026, 0x0000,  /* 20 - 23 */
    0x0032, 0x0000, 0x0000, 0x0000,  /* 24 - 27 */
    0x0000, 0x0000, 0x0000, 0x0000,  /* 28 - 31 */
    0x01b8, 0x000b, 0x0032, 0x0000,  /* 32 - 35 */
    0x0017, 0x0023, 0x01ea, 0x0125,  /* 36 - 39 */
    0x0000, 0x0002, 0x0000, 0x0000,  /* 40 - 43 */
    0x0033, 0x013c, 0x013c, 0x0100,  /* 44 - 47 */
    0x0100, 0x0006, 0x0001, 0x0001,  /* 48 - 51 */
    0x01b9, 0x01b9, 0x01b9, 0x01b9,  /* 52 - 55 */
    0x0001, 0x0001,					 /* 56 - 57 */
};

struct wm8978_priv {
	struct snd_soc_codec codec;
	u16 reg_cache[WM8978_CACHEREGNUM];
};

static struct snd_soc_codec *wm8978_codec;

/*
 * read wm8978 register cache
 */
static inline unsigned int wm8978_read_reg_cache(struct snd_soc_codec  *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg == WM8978_RESET)
		return 0;
	if (reg >= WM8978_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write wm8978 register cache
 */
static int suspending = 0;

static inline void wm8978_write_reg_cache(struct snd_soc_codec  *codec,
	u16 reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;

	if(suspending)
			return;

	if (reg >= WM8978_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the WM8978 register space
 */
static int wm8978_write(struct snd_soc_codec  *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D9 WM8978 register offset
	 *   D8...D0 register data
	 */

	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;
	if(0) printk("%s: addr: 0x%02x (%d) val: %03x\n", __FUNCTION__, reg, reg, value);

	wm8978_write_reg_cache (codec, reg, value);
	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -1;
}

#define wm8978_reset(c)	wm8978_write(c, WM8978_RESET, 0)

static const char *wm8978_companding[] = {"Off", "NC", "u-law", "A-law" };
static const char *wm8978_deemp[] = {"None", "32kHz", "44.1kHz", "48kHz" };
static const char *wm8978_eqmode[] = {"Capture", "Playback" };
static const char *wm8978_bw[] = {"Narrow", "Wide" };
static const char *wm8978_eq1[] = {"80Hz", "105Hz", "135Hz", "175Hz" };
static const char *wm8978_eq2[] = {"230Hz", "300Hz", "385Hz", "500Hz" };
static const char *wm8978_eq3[] = {"650Hz", "850Hz", "1.1kHz", "1.4kHz" };
static const char *wm8978_eq4[] = {"1.8kHz", "2.4kHz", "3.2kHz", "4.1kHz" };
static const char *wm8978_eq5[] = {"5.3kHz", "6.9kHz", "9kHz", "11.7kHz" };
static const char *wm8978_alc[] =
    {"ALC both on", "ALC left only", "ALC right only", "Limiter" };

static const struct soc_enum wm8978_enum[] = {
	SOC_ENUM_SINGLE(WM8978_COMP, 1, 4, wm8978_companding), /* adc */
	SOC_ENUM_SINGLE(WM8978_COMP, 3, 4, wm8978_companding), /* dac */
	SOC_ENUM_SINGLE(WM8978_DAC,  4, 4, wm8978_deemp),
	SOC_ENUM_SINGLE(WM8978_EQ1,  8, 2, wm8978_eqmode),

	SOC_ENUM_SINGLE(WM8978_EQ1,  5, 4, wm8978_eq1),
	SOC_ENUM_SINGLE(WM8978_EQ2,  8, 2, wm8978_bw),
	SOC_ENUM_SINGLE(WM8978_EQ2,  5, 4, wm8978_eq2),
	SOC_ENUM_SINGLE(WM8978_EQ3,  8, 2, wm8978_bw),

	SOC_ENUM_SINGLE(WM8978_EQ3,  5, 4, wm8978_eq3),
	SOC_ENUM_SINGLE(WM8978_EQ4,  8, 2, wm8978_bw),
	SOC_ENUM_SINGLE(WM8978_EQ4,  5, 4, wm8978_eq4),
	SOC_ENUM_SINGLE(WM8978_EQ5,  8, 2, wm8978_bw),

	SOC_ENUM_SINGLE(WM8978_EQ5,  5, 4, wm8978_eq5),
	SOC_ENUM_SINGLE(WM8978_ALC3,  8, 2, wm8978_alc),
};

static const struct snd_kcontrol_new wm8978_snd_controls[] = {
SOC_SINGLE("Digital Loopback Switch", WM8978_COMP, 0, 1, 0),

SOC_ENUM("ADC Companding", wm8978_enum[0]),
SOC_ENUM("DAC Companding", wm8978_enum[1]),

SOC_SINGLE("Jack Detection Enable", WM8978_JACK1, 6, 1, 0),

SOC_SINGLE("DAC Right Inversion Switch", WM8978_DAC, 1, 1, 0),
SOC_SINGLE("DAC Left Inversion Switch", WM8978_DAC, 0, 1, 0),

SOC_SINGLE("ADC LR Swap Switch", WM8978_IFACE, 1, 1, 0),
SOC_SINGLE("DAC LR Swap Switch", WM8978_IFACE, 2, 1, 0),

SOC_SINGLE("Left Playback Volume", WM8978_DACVOLL, 0, 127, 0),
SOC_SINGLE("Right Playback Volume", WM8978_DACVOLR, 0, 127, 0),

SOC_SINGLE("High Pass Filter Switch", WM8978_ADC, 8, 1, 0),
SOC_SINGLE("High Pass Cut Off", WM8978_ADC, 4, 7, 0),
SOC_SINGLE("ADC Right Inversion Switch", WM8978_ADC, 1, 1, 0),
SOC_SINGLE("ADC Left Inversion Switch", WM8978_ADC, 0, 1, 0),

SOC_SINGLE("Left Capture Volume", WM8978_ADCVOLL,  0, 127, 0),
SOC_SINGLE("Right Capture Volume", WM8978_ADCVOLR,  0, 127, 0),

SOC_DOUBLE_R("Bypass Volume", WM8978_MIXL, WM8978_MIXR, 2,7,0),
SOC_DOUBLE_R("Bypass Switch", WM8978_MIXL, WM8978_MIXR, 1,1,0),
SOC_ENUM("EQ Function", wm8978_enum[3]),
SOC_ENUM("EQ1 Cutoff Frequency", wm8978_enum[4]),
SOC_SINGLE("EQ1 Volume", WM8978_EQ1,  0, 24, 1),

SOC_ENUM("EQ2 Bandwidth", wm8978_enum[5]),
SOC_ENUM("EQ2 Center Frequency", wm8978_enum[6]),
SOC_SINGLE("EQ2 Volume", WM8978_EQ2,  0, 24, 1),

SOC_ENUM("EQ3 Bandwidth", wm8978_enum[7]),
SOC_ENUM("EQ3 Center Frequency", wm8978_enum[8]),
SOC_SINGLE("EQ3 Volume", WM8978_EQ3,  0, 24, 1),

SOC_ENUM("EQ4 Bandwidth", wm8978_enum[9]),
SOC_ENUM("EQ4 Center Frequency", wm8978_enum[10]),
SOC_SINGLE("EQ4 Volume", WM8978_EQ4,  0, 24, 1),

SOC_ENUM("EQ5 Bandwidth", wm8978_enum[11]),
SOC_ENUM("EQ5 Cutoff Frequency", wm8978_enum[12]),
SOC_SINGLE("EQ5 Volume", WM8978_EQ5,  0, 24, 1),

SOC_SINGLE("3D Depth", WM8978_3D,  0, 15, 0),

SOC_SINGLE("DAC Playback Limiter Switch", WM8978_DACLIM1,  8, 1, 0),
SOC_SINGLE("DAC Playback Limiter Decay", WM8978_DACLIM1,  4, 15, 0),
SOC_SINGLE("DAC Playback Limiter Attack", WM8978_DACLIM1,  0, 15, 0),

SOC_SINGLE("DAC Playback Limiter Threshold", WM8978_DACLIM2,  4, 7, 0),
SOC_SINGLE("DAC Playback Limiter Boost", WM8978_DACLIM2,  0, 15, 0),

SOC_SINGLE("ALC Enable Switch", WM8978_ALC1,  8, 1, 0),
SOC_SINGLE("ALC Capture Max Gain", WM8978_ALC1,  3, 7, 0),
SOC_SINGLE("ALC Capture Min Gain", WM8978_ALC1,  0, 7, 0),

SOC_SINGLE("ALC Capture ZC Switch", WM8978_ALC2,  8, 1, 0),
SOC_SINGLE("ALC Capture Hold", WM8978_ALC2,  4, 7, 0),
SOC_SINGLE("ALC Capture Target", WM8978_ALC2,  0, 15, 0),

SOC_ENUM("ALC Capture Mode", wm8978_enum[13]),
SOC_SINGLE("ALC Capture Decay", WM8978_ALC3,  4, 15, 0),
SOC_SINGLE("ALC Capture Attack", WM8978_ALC3,  0, 15, 0),

SOC_SINGLE("ALC Capture Noise Gate Switch", WM8978_NGATE,  3, 1, 0),
SOC_SINGLE("ALC Capture Noise Gate Threshold", WM8978_NGATE,  0, 7, 0),

SOC_SINGLE("Left Capture PGA ZC Switch", WM8978_INPPGAL,  7, 1, 0),
SOC_SINGLE("Left Capture PGA Volume", WM8978_INPPGAL,  0, 63, 0),

SOC_SINGLE("Right Capture PGA ZC Switch", WM8978_INPPGAR,  7, 1, 0),
SOC_SINGLE("Right Capture PGA Volume", WM8978_INPPGAR,  0, 63, 0),

SOC_SINGLE("Left Headphone Playback ZC Switch", WM8978_HPVOLL,  7, 1, 0),
SOC_SINGLE("Left Headphone Playback Switch", WM8978_HPVOLL,  6, 1, 1),
SOC_SINGLE("Left Headphone Playback Volume", WM8978_HPVOLL,  0, 63, 0),

SOC_SINGLE("Right Headphone Playback ZC Switch", WM8978_HPVOLR,  7, 1, 0),
SOC_SINGLE("Right Headphone Playback Switch", WM8978_HPVOLR,  6, 1, 1),
SOC_SINGLE("Right Headphone Playback Volume", WM8978_HPVOLR,  0, 63, 0),

SOC_SINGLE("Left Speaker Playback ZC Switch", WM8978_SPKVOLL,  7, 1, 0),
SOC_SINGLE("Left Speaker Playback Switch", WM8978_SPKVOLL,  6, 1, 1),
SOC_SINGLE("Left Speaker Playback Volume", WM8978_SPKVOLL,  0, 63, 0),

SOC_SINGLE("Right Speaker Playback ZC Switch", WM8978_SPKVOLR,  7, 1, 0),
SOC_SINGLE("Right Speaker Playback Switch", WM8978_SPKVOLR,  6, 1, 1),
SOC_SINGLE("Right Speaker Playback Volume", WM8978_SPKVOLR,  0, 63, 0),

SOC_DOUBLE_R("Capture Boost Switch", WM8978_ADCBOOSTL, WM8978_ADCBOOSTR,
	8, 1, 0),
SOC_SINGLE("Left to Right Mix Switch", WM8978_OUTPUT, 6, 1, 0),
SOC_SINGLE("Right to Left Mix Switch", WM8978_OUTPUT, 5, 1, 0),
};

/* Left Output Mixer */
static const struct snd_kcontrol_new wm8978_left_mixer_controls[] = {
SOC_DAPM_SINGLE("Left to Right Mix Switch", WM8978_OUTPUT, 6, 1, 1),
SOC_DAPM_SINGLE("Left PCM Playback Switch", WM8978_MIXL, 0, 1, 1),
SOC_DAPM_SINGLE("Line Bypass Switch", WM8978_MIXL, 1, 1, 0),
SOC_DAPM_SINGLE("Aux Playback Switch", WM8978_MIXL, 5, 1, 0),
};

/* Right Output Mixer */
static const struct snd_kcontrol_new wm8978_right_mixer_controls[] = {
SOC_DAPM_SINGLE("Right to Left Mix Switch", WM8978_OUTPUT, 5, 1, 1),
SOC_DAPM_SINGLE("Right PCM Playback Switch", WM8978_MIXR, 0, 1, 1),
SOC_DAPM_SINGLE("Line Bypass Switch", WM8978_MIXR, 1, 1, 0),
SOC_DAPM_SINGLE("Aux Playback Switch", WM8978_MIXR, 5, 1, 0),
};

/* Left AUX Input boost vol */
static const struct snd_kcontrol_new wm8978_laux_boost_controls =
SOC_DAPM_SINGLE("Left Aux Volume", WM8978_ADCBOOSTL, 0, 3, 0);

/* Right AUX Input boost vol */
static const struct snd_kcontrol_new wm8978_raux_boost_controls =
SOC_DAPM_SINGLE("Right Aux Volume", WM8978_ADCBOOSTR, 0, 3, 0);

/* Left Input boost vol */
static const struct snd_kcontrol_new wm8978_lmic_boost_controls =
SOC_DAPM_SINGLE("Left Input Volume", WM8978_ADCBOOSTL, 4, 3, 0);

/* Right Input boost vol */
static const struct snd_kcontrol_new wm8978_rmic_boost_controls =
SOC_DAPM_SINGLE("Right Input Volume", WM8978_ADCBOOSTR, 4, 3, 0);

/* Left Aux In to PGA */
static const struct snd_kcontrol_new wm8978_laux_capture_boost_controls =
SOC_DAPM_SINGLE("Left Capture Switch", WM8978_ADCBOOSTL,  8, 1, 0);

/* Right  Aux In to PGA */
static const struct snd_kcontrol_new wm8978_raux_capture_boost_controls =
SOC_DAPM_SINGLE("Right Capture Switch", WM8978_ADCBOOSTR,  8, 1, 0);

/* Left Input P In to PGA */
static const struct snd_kcontrol_new wm8978_lmicp_capture_boost_controls =
SOC_DAPM_SINGLE("Left Input P Capture Boost Switch", WM8978_INPUT,  0, 1, 0);

/* Right Input P In to PGA */
static const struct snd_kcontrol_new wm8978_rmicp_capture_boost_controls =
SOC_DAPM_SINGLE("Right Input P Capture Boost Switch", WM8978_INPUT,  4, 1, 0);

/* Left Input N In to PGA */
static const struct snd_kcontrol_new wm8978_lmicn_capture_boost_controls =
SOC_DAPM_SINGLE("Left Input N Capture Boost Switch", WM8978_INPUT,  1, 1, 0);

/* Right Input N In to PGA */
static const struct snd_kcontrol_new wm8978_rmicn_capture_boost_controls =
SOC_DAPM_SINGLE("Right Input N Capture Boost Switch", WM8978_INPUT,  5, 1, 0);

// TODO Widgets
static const struct snd_soc_dapm_widget wm8978_dapm_widgets[] = {
#if 0
//SND_SOC_DAPM_MUTE("Mono Mute", WM8978_MONOMIX, 6, 0),
//SND_SOC_DAPM_MUTE("Speaker Mute", WM8978_SPKMIX, 6, 0),

SND_SOC_DAPM_MIXER("Speaker Mixer", WM8978_POWER3, 2, 0,
	&wm8978_speaker_mixer_controls[0],
	ARRAY_SIZE(wm8978_speaker_mixer_controls)),
SND_SOC_DAPM_MIXER("Mono Mixer", WM8978_POWER3, 3, 0,
	&wm8978_mono_mixer_controls[0],
	ARRAY_SIZE(wm8978_mono_mixer_controls)),
SND_SOC_DAPM_DAC("DAC", "HiFi Playback", WM8978_POWER3, 0, 0),
SND_SOC_DAPM_ADC("ADC", "HiFi Capture", WM8978_POWER3, 0, 0),
SND_SOC_DAPM_PGA("Aux Input", WM8978_POWER1, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("SpkN Out", WM8978_POWER3, 5, 0, NULL, 0),
SND_SOC_DAPM_PGA("SpkP Out", WM8978_POWER3, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mono Out", WM8978_POWER3, 7, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mic PGA", WM8978_POWER2, 2, 0, NULL, 0),

SND_SOC_DAPM_PGA("Aux Boost", SND_SOC_NOPM, 0, 0,
	&wm8978_aux_boost_controls, 1),
SND_SOC_DAPM_PGA("Mic Boost", SND_SOC_NOPM, 0, 0,
	&wm8978_mic_boost_controls, 1),
SND_SOC_DAPM_SWITCH("Capture Boost", SND_SOC_NOPM, 0, 0,
	&wm8978_capture_boost_controls),

SND_SOC_DAPM_MIXER("Boost Mixer", WM8978_POWER2, 4, 0, NULL, 0),

SND_SOC_DAPM_MICBIAS("Mic Bias", WM8978_POWER1, 4, 0),

SND_SOC_DAPM_INPUT("MICN"),
SND_SOC_DAPM_INPUT("MICP"),
SND_SOC_DAPM_INPUT("AUX"),
SND_SOC_DAPM_OUTPUT("MONOOUT"),
SND_SOC_DAPM_OUTPUT("SPKOUTP"),
SND_SOC_DAPM_OUTPUT("SPKOUTN"),
#endif
};

static const struct snd_soc_dapm_route audio_map[] = {
#if 0
	/* Mono output mixer */
	{"Mono Mixer", "PCM Playback Switch", "DAC"},
	{"Mono Mixer", "Aux Playback Switch", "Aux Input"},
	{"Mono Mixer", "Line Bypass Switch", "Boost Mixer"},

	/* Speaker output mixer */
	{"Speaker Mixer", "PCM Playback Switch", "DAC"},
	{"Speaker Mixer", "Aux Playback Switch", "Aux Input"},
	{"Speaker Mixer", "Line Bypass Switch", "Boost Mixer"},

	/* Outputs */
	{"Mono Out", NULL, "Mono Mixer"},
	{"MONOOUT", NULL, "Mono Out"},
	{"SpkN Out", NULL, "Speaker Mixer"},
	{"SpkP Out", NULL, "Speaker Mixer"},
	{"SPKOUTN", NULL, "SpkN Out"},
	{"SPKOUTP", NULL, "SpkP Out"},

	/* Boost Mixer */
	{"Boost Mixer", NULL, "ADC"},
	{"Capture Boost Switch", "Aux Capture Boost Switch", "AUX"},
	{"Aux Boost", "Aux Volume", "Boost Mixer"},
	{"Capture Boost", "Capture Switch", "Boost Mixer"},
	{"Mic Boost", "Mic Volume", "Boost Mixer"},

	/* Inputs */
	{"MICP", NULL, "Mic Boost"},
	{"MICN", NULL, "Mic PGA"},
	{"Mic PGA", NULL, "Capture Boost"},
	{"AUX", NULL, "Aux Input"},
#endif
};

static int wm8978_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, wm8978_dapm_widgets,
				  ARRAY_SIZE(wm8978_dapm_widgets));

	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_new_widgets(codec);
	return 0;
}

struct pll_ {
	unsigned int in_hz, out_hz;
	unsigned int post;  /* mclkdiv */
	unsigned int bclkdiv; /* bclk divider */
	unsigned int n:4;
	unsigned int k;
};


static struct pll_ pll[] = {
#if 0
	{12000000, 11289600, WM8978_MCLK_DIV_2, WM8978_BCLK_DIV_1, 0x7, 0x86c220},
	{12000000, 12288000, WM8978_MCLK_DIV_2, WM8978_BCLK_DIV_1, 0x8, 0x3126e8},

//	{13000000, 24576000, WM8978_MCLK_DIV_1, WM8978_BCLK_DIV_1, 0x7, 0x8fd526},
	{13000000, 24576000, WM8978_MCLK_DIV_1_5, WM8978_BCLK_DIV_1, 0xb, 0x57bfb9},

//	{13000000, 11289600, WM8978_MCLK_DIV_1_5, WM8978_BCLK_DIV_8, 0x5, 0x35e8e0},
//	{13000000, 11289600, WM8978_MCLK_DIV_2, WM8978_BCLK_DIV_8, 0x6, 0xf28bd5},
	{13000000, 11289600, WM8978_MCLK_DIV_3, WM8978_BCLK_DIV_8, 0xa, 0x6bd1bf},

//	{13000000, 12288000, WM8978_MCLK_DIV_1, WM8978_BCLK_DIV_1, 0x5, 0xabdfdd},
//	{13000000, 12288000, WM8978_MCLK_DIV_2, WM8978_BCLK_DIV_1, 0x7, 0x8fd526},
	{13000000, 12288000, WM8978_MCLK_DIV_3, WM8978_BCLK_DIV_1, 0xb, 0x57bfb9},
//	{13000000, 12288000, WM8978_MCLK_DIV_4, WM8978_BCLK_DIV_1, 0xd, 0xe517aa},

//	{13000000,  8192000, WM8978_MCLK_DIV_2, WM8978_BCLK_DIV_8, 0x5, 0xa8e1a},
//	{13000000,  8192000, WM8978_MCLK_DIV_3, WM8978_BCLK_DIV_8, 0x7, 0x8fd526},
	{13000000,  8192000, WM8978_MCLK_DIV_4, WM8978_BCLK_DIV_8, 0xa, 0x151c33},

//	{13000000,  4096000, WM8978_MCLK_DIV_4, WM8978_BCLK_DIV_8, 0x5, 0xa8e1a},
//	{13000000,  4096000, WM8978_MCLK_DIV_6, WM8978_BCLK_DIV_8, 0x7, 0x8fd526},
	{13000000,  4096000, WM8978_MCLK_DIV_8, WM8978_BCLK_DIV_8, 0xa, 0x151c33},

//	{13000000,  2048000, WM8978_MCLK_DIV_8, WM8978_BCLK_DIV_1, 0x5, 0xa8e1a},
	{13000000,  2048000, WM8978_MCLK_DIV_12, WM8978_BCLK_DIV_1, 0x7, 0x8fd526},

	{12288000, 11289600, WM8978_MCLK_DIV_2, WM8978_BCLK_DIV_1, 0x7, 0x59999a},
	{11289600, 12288000, WM8978_MCLK_DIV_2, WM8978_BCLK_DIV_1, 0x8, 0x80dee9},
#endif
	{12288000,  2048000, WM8978_MCLK_DIV_12, WM8978_BCLK_DIV_1, 0x8, 0x000000},	/*  8000 */
	{12288000,  4096000, WM8978_MCLK_DIV_6, WM8978_BCLK_DIV_1, 0x8, 0x000000},		/* 16000 */
	{12288000,  8192000, WM8978_MCLK_DIV_3, WM8978_BCLK_DIV_1, 0x8, 0x000000},		/* 32000 */
	{12288000, 12288000, WM8978_MCLK_DIV_2, WM8978_BCLK_DIV_1, 0x8, 0x000000},	/* 48000 */

	{11289600,  2822400, WM8978_MCLK_DIV_8, WM8978_BCLK_DIV_1, 0x8, 0x000000},		/* 11025 */
	{11289600,  5644800, WM8978_MCLK_DIV_4, WM8978_BCLK_DIV_1, 0x8, 0x000000},		/* 22050 */
	{11289600, 11289600, WM8978_MCLK_DIV_2, WM8978_BCLK_DIV_1, 0x8, 0x000000},	/* 44100 */

	/* TODO: liam - add more entries */
};

static int wm8978_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div);

static int wm8978_set_dai_pll(struct snd_soc_dai *codec_dai,
		int pll_id, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int i, prescale;
	u16 reg;

	if(freq_in == 0 || freq_out == 0) {
		reg = wm8978_read_reg_cache(codec, WM8978_POWER1);
		wm8978_write(codec, WM8978_POWER1, reg & 0x1df);
		return 0;
	}

	for(prescale = 0; prescale <= 1; prescale++, freq_in /= 2) {
	for(i = 0; i < ARRAY_SIZE(pll); i++) {
		if (freq_in != pll[i].in_hz || freq_out != pll[i].out_hz)
			continue;

		wm8978_write(codec, WM8978_PLLN, (prescale << 4) | pll[i].n);
		wm8978_write(codec, WM8978_PLLK1, pll[i].k >> 18);
		wm8978_write(codec, WM8978_PLLK2, (pll[i].k >> 9) & 0x1ff);
		wm8978_write(codec, WM8978_PLLK3, pll[i].k & 0x1ff);

		wm8978_set_dai_clkdiv(codec_dai, WM8978_MCLKDIV, pll[i].post);
		wm8978_set_dai_clkdiv(codec_dai, WM8978_BCLKDIV, pll[i].bclkdiv);

		reg = wm8978_read_reg_cache(codec, WM8978_POWER1);
		wm8978_write(codec, WM8978_POWER1, reg | 0x020);
		return 0;
		}
	}

	pr_err("%s: no pll config for freq_in = %d freq_out = %d\n",
					__FUNCTION__, freq_in, freq_out);

	return -EINVAL;
}

static int wm8978_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = wm8978_read_reg_cache(codec, WM8978_IFACE) & 0x3;
	u16 clk = wm8978_read_reg_cache(codec, WM8978_CLOCK) & 0xfffe;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		clk |= 0x0001;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0010;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0008;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x00018;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0180;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0100;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0080;
		break;
	default:
		return -EINVAL;
	}

	wm8978_write(codec, WM8978_CLOCK, clk);
	wm8978_write(codec, WM8978_IFACE, iface);
	return 0;
}

static int wm8978_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_device *socdev = rtd->socdev;
	struct snd_soc_codec *codec = socdev->card->codec;
	u16 iface = wm8978_read_reg_cache(codec, WM8978_IFACE) & 0xff9f;
	u16 adn = wm8978_read_reg_cache(codec, WM8978_ADD) & 0x1f1;
	u16 output = wm8978_read_reg_cache(codec, WM8978_OUTPUT) & ~0x60;

	/* channels */
	switch(params_channels(params)) {
	case 1:
		output |= 0x60;
		break;
	default:
	case 2:
		break;
	};

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0020;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0040;
		break;
	}

	/* filter coefficient */
	switch (params_rate(params)) {

	case 8000:
		adn |= 0x5 << 1;
		break;
	case 11025:
		adn |= 0x4 << 1;
		break;
	case 16000:
		adn |= 0x3 << 1;
		break;
	case 22050:
		adn |= 0x2 << 1;
		break;
	case 32000:
		adn |= 0x1 << 1;
		break;
	}

	/* set iface */
	wm8978_write(codec, WM8978_IFACE, iface);
	wm8978_write(codec, WM8978_ADD, adn);
	wm8978_write(codec, WM8978_OUTPUT, output);
	return 0;
}

static
char *div_id_as_string(int div_id) possibly_unused;

static
char *div_id_as_string(int div_id)
	{
	switch(div_id)
		{
		caseAsString(WM8978_MCLKDIV);
		caseAsString(WM8978_BCLKDIV);
		caseAsString(WM8978_OPCLKDIV);
		caseAsString(WM8978_DACOSR);
		caseAsString(WM8978_ADCOSR);
		caseAsString(WM8978_MCLKSEL);
		unknownCase(div_id);
		}
}

static int wm8978_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	switch (div_id) {
	case WM8978_MCLKDIV:
		reg = wm8978_read_reg_cache(codec, WM8978_CLOCK) & 0x11f;
		wm8978_write(codec, WM8978_CLOCK, reg | div);
		break;
	case WM8978_BCLKDIV:
		reg = wm8978_read_reg_cache(codec, WM8978_CLOCK) & 0x1e3;
		wm8978_write(codec, WM8978_CLOCK, reg | div);
		break;
	case WM8978_OPCLKDIV:
		reg = wm8978_read_reg_cache(codec, WM8978_GPIO) & 0x1cf;
		wm8978_write(codec, WM8978_GPIO, reg | div);
		break;
	case WM8978_DACOSR:
		reg = wm8978_read_reg_cache(codec, WM8978_DAC) & 0x1f7;
		wm8978_write(codec, WM8978_DAC, reg | div);
		break;
	case WM8978_ADCOSR:
		reg = wm8978_read_reg_cache(codec, WM8978_ADC) & 0x1f7;
		wm8978_write(codec, WM8978_ADC, reg | div);
		break;
	case WM8978_MCLKSEL:
		reg = wm8978_read_reg_cache(codec, WM8978_CLOCK) & 0x0ff;
		wm8978_write(codec, WM8978_CLOCK, reg | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8978_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 mute_reg = wm8978_read_reg_cache(codec, WM8978_DAC) & 0xffbf;

	if(mute)
		wm8978_write(codec, WM8978_DAC, mute_reg | 0x40);
	else
		wm8978_write(codec, WM8978_DAC, mute_reg);

	return 0;
}
static int wm8978_codec_resume(struct snd_soc_codec *codec);

static
char *bias_lvl_as_string(int lvl) possibly_unused;

static
char *bias_lvl_as_string(int lvl)
	{
	switch(lvl)
		{
		caseAsString(SND_SOC_BIAS_ON);
		caseAsString(SND_SOC_BIAS_PREPARE);
		caseAsString(SND_SOC_BIAS_STANDBY);
		caseAsString(SND_SOC_BIAS_OFF);
		unknownCase(lvl);
		}
	}
/* TODO: liam need to make this lower power with dapm */
static int wm8978_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
//	trace("level = %s", bias_lvl_as_string(level));

	switch (level) {
	case SND_SOC_BIAS_ON:
	case SND_SOC_BIAS_PREPARE:
	case SND_SOC_BIAS_STANDBY:
			// FIXME: Someday we need to power down/up reliably with no clicks and pops
		break;
	case SND_SOC_BIAS_OFF:
		wm8978_write(codec, WM8978_POWER1, 0x0);
		wm8978_write(codec, WM8978_POWER2, 0x0);
		wm8978_write(codec, WM8978_POWER3, 0x0);
		break;
	}
	codec->bias_level = level;
	return 0;
}

#if 0
#define WM8978_RATES SNDRV_PCM_RATE_8000_48000
#else
#define WM8978_RATES    SNDRV_PCM_RATE_48000
#endif

#define WM8978_FORMATS \
	(SNDRV_PCM_FORMAT_S16_LE | SNDRV_PCM_FORMAT_S20_3LE | \
	SNDRV_PCM_FORMAT_S24_3LE | SNDRV_PCM_FORMAT_S24_LE)

static struct snd_soc_dai_ops wm8978_ops = {
	.hw_params = wm8978_hw_params,
	.digital_mute = wm8978_mute,
	.set_fmt = wm8978_set_dai_fmt,
	.set_clkdiv = wm8978_set_dai_clkdiv,
	.set_pll = wm8978_set_dai_pll,
};

struct snd_soc_dai wm8978_dai = {
	.name = "WM8978 HiFi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8978_RATES,
		.formats = WM8978_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = WM8978_RATES,
		.formats = WM8978_FORMATS,},
	.ops = &wm8978_ops,
};
EXPORT_SYMBOL_GPL(wm8978_dai);

static int wm8978_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	suspending = 1;		// do not cache following command

	wm8978_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8978_codec_resume(struct snd_soc_codec *codec)
{
	int i;
	u8 data[2];
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(wm8978_reg); i++) {
		data[0] = (i << 1) | ((cache[i] >> 8) & 0x0001);
		data[1] = cache[i] & 0x00ff;
		codec->hw_write(codec->control_data, data, 2);
	}
	suspending = 0;
//	wm8978_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
//	wm8978_set_bias_level(codec, codec->suspend_bias_level);
	return 0;
}

static int wm8978_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	return wm8978_codec_resume(codec);
}

static int wm8978_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	if (wm8978_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = wm8978_codec;
	codec = wm8978_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	wm8978_codec_resume(codec);
	snd_soc_add_controls(codec, wm8978_snd_controls,
			     ARRAY_SIZE(wm8978_snd_controls));
	wm8978_add_widgets(codec);
	ret = snd_soc_init_card(socdev);
	if (ret < 0) {
		dev_err(codec->dev, "failed to register card: %d\n", ret);
		goto card_err;
	}

	return ret;

card_err:
	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);
pcm_err:
	return ret;
}

/* power down chip */
static int wm8978_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8978 = {
	.probe =	wm8978_probe,
	.remove =	wm8978_remove,
	.suspend =	wm8978_suspend,
	.resume =	wm8978_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8978);

static int wm8978_register(struct wm8978_priv *wm8978)
{
	int ret;
	struct snd_soc_codec *codec = &wm8978->codec;

	if (wm8978_codec) {
		dev_err(codec->dev, "Another WM8978 is registered\n");
		return -EINVAL;
	}
	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->private_data = wm8978;
	codec->name = "WM8978";
	codec->owner = THIS_MODULE;
	codec->read = wm8978_read_reg_cache;
	codec->write = wm8978_write;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = wm8978_set_bias_level;
	codec->dai = &wm8978_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = WM8978_CACHEREGNUM;
	codec->reg_cache = &wm8978->reg_cache;

	memcpy(codec->reg_cache, wm8978_reg, sizeof(wm8978_reg));

	ret = wm8978_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		return ret;
	}

	wm8978_dai.dev = codec->dev;

	wm8978_set_bias_level(codec, SND_SOC_BIAS_STANDBY);

	wm8978_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_dai(&wm8978_dai);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		snd_soc_unregister_codec(codec);
		return ret;
	}

	return 0;
}

static void wm8978_unregister(struct wm8978_priv *wm8978)
{
	wm8978_set_bias_level(&wm8978->codec, SND_SOC_BIAS_OFF);
	snd_soc_unregister_dai(&wm8978_dai);
	snd_soc_unregister_codec(&wm8978->codec);
	kfree(wm8978);
	wm8978_codec = NULL;
}

#if defined(CONFIG_SPI_MASTER)
static int wm8978_spi_write(struct spi_device *spi, const char *data, int len)
{
	struct spi_transfer t;
	struct spi_message m;
	u8 msg[2];

	if (len <= 0)
		return 0;

	msg[0] = data[0];
	msg[1] = data[1];

	spi_message_init(&m);
	memset(&t, 0, (sizeof t));

	t.tx_buf = &msg[0];
	t.len = len;

	spi_message_add_tail(&t, &m);
	spi_sync(spi, &m);

	return len;
}

static int __devinit wm8978_spi_probe(struct spi_device *spi)
{
	struct snd_soc_codec *codec;
	struct wm8978_priv *wm8978;

	wm8978 = kzalloc(sizeof(struct wm8978_priv), GFP_KERNEL);
	if (wm8978 == NULL)
		return -ENOMEM;

	codec = &wm8978->codec;
	codec->control_data = spi;
	codec->hw_write = (hw_write_t)wm8978_spi_write;
	codec->dev = &spi->dev;

	spi->dev.driver_data = wm8978;

	return wm8978_register(wm8978);
}

static int __devexit wm8978_spi_remove(struct spi_device *spi)
{
	struct wm8978_priv *wm8978 = spi->dev.driver_data;

	wm8978_unregister(wm8978);

	return 0;
}

static struct spi_driver wm8978_spi_driver = {
	.driver = {
		.name	= "wm8978",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm8978_spi_probe,
	.remove		= __devexit_p(wm8978_spi_remove),
};
#endif /* CONFIG_SPI_MASTER */

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8978_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8978_priv *wm8978;
	struct snd_soc_codec *codec;

	wm8978 = kzalloc(sizeof(struct wm8978_priv), GFP_KERNEL);
	if (wm8978 == NULL)
		return -ENOMEM;

	codec = &wm8978->codec;
	codec->hw_write = (hw_write_t)i2c_master_send;

	i2c_set_clientdata(i2c, wm8978);
	codec->control_data = i2c;

	codec->dev = &i2c->dev;

	return wm8978_register(wm8978);
}

static __devexit int wm8978_i2c_remove(struct i2c_client *client)
{
	struct wm8978_priv *wm8978 = i2c_get_clientdata(client);
	wm8978_unregister(wm8978);
	return 0;
}

static const struct i2c_device_id wm8978_i2c_id[] = {
	{ "wm8978", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8978_i2c_id);

static struct i2c_driver wm8978_i2c_driver = {
	.driver = {
		.name = "WM8978 I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe =    wm8978_i2c_probe,
	.remove =   __devexit_p(wm8978_i2c_remove),
	.id_table = wm8978_i2c_id,
};
#endif

static int __init wm8978_modinit(void)
{
	int ret;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8978_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8978 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8978_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8978 SPI driver: %d\n",
		       ret);
	}
#endif
	return 0;
}
module_init(wm8978_modinit);

static void __exit wm8978_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8978_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8978_spi_driver);
#endif
}
module_exit(wm8978_exit);

MODULE_DESCRIPTION("ASoC WM8978 driver");
MODULE_AUTHOR("Liam Girdwood" \
	" and Jeffrey Rosenwald");
MODULE_LICENSE("GPL");
