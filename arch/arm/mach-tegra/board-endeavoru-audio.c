#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>

#include <mach/board_htc.h>

#include "gpio-names.h"
#include "htc_audio_power.h"

#undef LOG_TAG
#define LOG_TAG "AUD"

#undef PWR_DEVICE_TAG
#define PWR_DEVICE_TAG LOG_TAG

#undef AUDIO_DEBUG
#define AUDIO_DEBUG 0

#define AUD_ERR(fmt, ...) pr_tag_err(LOG_TAG, fmt, ##__VA_ARGS__)
#define AUD_INFO(fmt, ...) pr_tag_info(LOG_TAG, fmt, ##__VA_ARGS__)

#if AUDIO_DEBUG
#define AUD_DBG(fmt, ...) pr_tag_info(LOG_TAG, fmt, ##__VA_ARGS__)
#else
#define AUD_DBG(fmt, ...) pr_tag_dbg(LOG_TAG, fmt, ##__VA_ARGS__)
#endif

struct aic3008_power aic3008_power;
static int pcbid;

static void aic3008_powerinit(void)
{
	power_config("AUD_MCLK_EN", TEGRA_GPIO_PX7, INIT_OUTPUT_HIGH);
	power_config("AUD_AIC3008_RST#", TEGRA_GPIO_PW5, INIT_OUTPUT_HIGH);

	power_config("AUD_MCLK", TEGRA_GPIO_PW4, INIT_OUTPUT_LOW);
	sfio_deconfig("AUD_MCLK", TEGRA_GPIO_PW4);

	if (pcbid >= PROJECT_PHASE_XB || board_get_sku_tag() == 0x34600) {
		power_config("AUD_HP_GAIN_CONTROL", TEGRA_GPIO_PD1, INIT_OUTPUT_LOW);
		power_config("AUD_SPK_RST#", TEGRA_GPIO_PP6, INIT_OUTPUT_HIGH);
		power_config("AUD_HEADPHONE_EN", TEGRA_GPIO_PP7, INIT_OUTPUT_LOW);
	} else {
		power_config("AUD_SPK_EN", TEGRA_GPIO_PP6, INIT_OUTPUT_LOW);
		power_config("AUD_LINEOUT_EN", TEGRA_GPIO_PP7, INIT_OUTPUT_LOW);
	}

	common_init();

	spin_lock_init(&aic3008_power.spin_lock);
	aic3008_power.isPowerOn = true;

	return;
}

static void aic3008_resume(void)
{
	spin_lock(&aic3008_power.spin_lock);
	power_config("AUD_MCLK_EN", TEGRA_GPIO_PX7, GPIO_OUTPUT);
	common_config();
	aic3008_power.isPowerOn = true;
	spin_unlock(&aic3008_power.spin_lock);
	return;
}

static void aic3008_suspend(void)
{
	spin_lock(&aic3008_power.spin_lock);
	power_deconfig("AUD_MCLK_EN", TEGRA_GPIO_PX7, GPIO_OUTPUT);
	common_deconfig();
	aic3008_power.isPowerOn = false;
	spin_unlock(&aic3008_power.spin_lock);
	return;
}

static void aic3008_mic_powerup(void)
{
	/* No Need to Mic PowerUp */
	return;
}

static void aic3008_mic_powerdown(void)
{
	/* No Need to Mic PowerDown */
	return;
}

static void aic3008_amp_powerup(int type)
{
	switch (type) {
	case HEADSET_AMP:
		if (pcbid >= PROJECT_PHASE_XB || board_get_sku_tag() == 0x34600) {
			mdelay(50);
			power_config("AUD_HEADPHONE_EN", TEGRA_GPIO_PP7, GPIO_OUTPUT);
		}
		break;
	case SPEAKER_AMP:
		mdelay(50);
		if (pcbid >= PROJECT_PHASE_XB || board_get_sku_tag() == 0x34600) {
#if (defined(CONFIG_SND_AMP_TFA9887))
			set_tfa9887_spkamp(1, 0);
#endif
		} else {
			power_config("AUD_SPK_EN", TEGRA_GPIO_PP6, GPIO_OUTPUT);
		}
		break;
	case DOCK_AMP:
		mdelay(50);
		if (pcbid >= PROJECT_PHASE_XB || board_get_sku_tag() == 0x34600) {
		} else {
			power_config("AUD_LINEOUT_EN", TEGRA_GPIO_PP7, GPIO_OUTPUT);
		}
		dock_config("TEGRA_GPIO_DESK_AUD", TEGRA_GPIO_PCC5, true, true);
		break;
	default:
		AUD_ERR("aic3008_amp_powerup unknown type %d\n", type);
		break;
	}
	return;
}

static void aic3008_amp_powerdown(int type)
{
	switch (type) {
	case HEADSET_AMP:
		if (pcbid >= PROJECT_PHASE_XB || board_get_sku_tag() == 0x34600) {
			power_deconfig("AUD_HEADPHONE_EN", TEGRA_GPIO_PP7, GPIO_OUTPUT);
		}
		break;
	case SPEAKER_AMP:
		if (pcbid >= PROJECT_PHASE_XB || board_get_sku_tag() == 0x34600) {
#if (defined(CONFIG_SND_AMP_TFA9887))
			set_tfa9887_spkamp(0, 0);
#endif
		} else {
			power_deconfig("AUD_SPK_EN", TEGRA_GPIO_PP6, GPIO_OUTPUT);
		}
		break;
	case DOCK_AMP:
		if (pcbid >= PROJECT_PHASE_XB || board_get_sku_tag() == 0x34600) {
		} else {
			power_deconfig("AUD_LINEOUT_EN", TEGRA_GPIO_PP7, GPIO_OUTPUT);
		}
		dock_config("TEGRA_GPIO_DESK_AUD", TEGRA_GPIO_PCC5, false, true);
		break;
	default:
		AUD_ERR("aic3008_amp_powerdown unknown type %d\n", type);
		break;
	}
	return;
}
/*
static void aic3008_i2s_control(int dsp_enum)
{
	switch (dsp_enum) {
	case Phone_Default:
	case Phone_BT:
	case VOIP_BT:
	case VOIP_BT_HW_AEC:
		if (pcbid >= PROJECT_PHASE_XB || board_get_sku_tag() == 0x34600) {
			power_config("AUD_BT_SEL", TEGRA_GPIO_PK5, GPIO_OUTPUT);
		}
		break;
	case FM_Headset:
	case FM_Speaker:
		power_config("AUD_FM_SEL", TEGRA_GPIO_PK6, GPIO_OUTPUT);
		break;
	default:
		if (pcbid >= PROJECT_PHASE_XB || board_get_sku_tag() == 0x34600) {
			power_deconfig("AUD_BT_SEL", TEGRA_GPIO_PK5, GPIO_OUTPUT);
		}
		power_deconfig("AUD_FM_SEL", TEGRA_GPIO_PK6, GPIO_OUTPUT);
		break;
	}
	return;
}
*/
struct aic3008_power aic3008_power = {
	.mic_switch = false,
	.amp_switch = true,
//	.i2s_switch = true,
	.powerinit = aic3008_powerinit,
	.resume = aic3008_resume,
	.suspend = aic3008_suspend,
	.mic_powerup = aic3008_mic_powerup,
	.mic_powerdown = aic3008_mic_powerdown,
	.amp_powerup = aic3008_amp_powerup,
	.amp_powerdown = aic3008_amp_powerdown,
//	.i2s_control = aic3008_i2s_control,
};
struct aic3008_power *aic3008_power_ctl = &aic3008_power;
EXPORT_SYMBOL_GPL(aic3008_power_ctl);
