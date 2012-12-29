/*
 * arch/arm/mach-tegra/board-endeavoru-camera.c
 *
 * Copyright (c) 2011, NVIDIA CORPORATION, All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the name of NVIDIA CORPORATION nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/bma250.h>
#include <linux/tps61310_flashlight.h>

#include <media/s5k3h2y.h>
#include <media/ov8838.h>
#include <media/s5k6a1gx03.h>
#include <media/ad5823.h>

#include <mach/gpio.h>
#include <mach/board_htc.h>

#include "cpu-tegra.h"
#include "gpio-names.h"
#include "board-endeavoru.h"
#include "board.h"

#include <media/rawchip/Yushan_API.h>

static struct regulator *cam_vcm_2v85_en = NULL;
static struct regulator *cam_a2v85_en = NULL;
static struct regulator *cam_vddio_1v8_en = NULL;
static struct regulator *cam_d1v2_en = NULL;
static struct regulator *cam2_d1v2_en = NULL;

static inline void ENR_msleep(u32 t)
{
	/*
	 If timer value is between ( 10us - 20ms),
	 ENR_usleep_range() is recommended.
	 Please read Documentation/timers/timers-howto.txt.
	 */
	usleep_range(t * 1000, t * 1000 + 500);
}

static inline void ENR_usleep(u32 t)
{
	usleep_range(t, t + 500);
}

static int endeavor_s5k3h2y_power_state = 0;
static int endeavor_s5k3h2y_get_power(void)
{
	return endeavor_s5k3h2y_power_state;
}

static int endeavor_s5k3h2y_power_on(void)
{
    int ret;

    if (endeavor_s5k3h2y_power_state) {
	pr_info("[CAM] %s already power on, return 0", __func__);
	return 0;
    }

	tegra_gpio_disable(MCAM_SPI_CLK);
	tegra_gpio_disable(MCAM_SPI_CS0);
	tegra_gpio_disable(MCAM_SPI_DI);
	tegra_gpio_disable(MCAM_SPI_DO);
	tegra_gpio_disable(CAM_I2C_SCL);
	tegra_gpio_disable(CAM_I2C_SDA);
	tegra_gpio_disable(CAM_MCLK);

    gpio_direction_output(CAM1_PWDN, 0);
    gpio_direction_output(CAM1_VCM_PD, 0);
    gpio_direction_output(RAW_RSTN, 0);

	/* RAW_1V8_EN */
	gpio_direction_output(RAW_1V8_EN, 1);
	ENR_usleep(200);

	/* VCM */
	ret = regulator_enable(cam_vcm_2v85_en);
	if (ret < 0) {
		pr_err("[CAM] couldn't enable regulator v_cam1_vcm_2v85\n");
		regulator_put(cam_vcm_2v85_en);
		cam_vcm_2v85_en = NULL;
		return ret;
	}

    	/* main/front cam analog */
	ret = regulator_enable(cam_a2v85_en);
	if (ret < 0) {
		pr_err("[CAM] couldn't enable regulator cam_a2v85_en\n");
		regulator_put(cam_a2v85_en);
		cam_a2v85_en = NULL;
		return ret;
	}
    	ENR_usleep(200);
 
    	/* main cam core 1v2 & rawchip external 1v2 */
	ret = regulator_enable(cam_d1v2_en);
	if (ret < 0) {
		pr_err("[CAM] couldn't enable regulator cam_d1v2_en\n");
		regulator_put(cam_d1v2_en);
		cam_d1v2_en = NULL;
		return ret;
	}
	ENR_usleep(200);
 
	/* RAW_1V2_EN */
    	if (htc_get_pcbid_info() < PROJECT_PHASE_XE) {
        	gpio_direction_output(RAW_1V2_EN, 1);
		ENR_usleep(200);
    	}

    	/* IO */
	ret = regulator_enable(cam_vddio_1v8_en);
	if (ret < 0) {
		pr_err("[CAM] couldn't enable regulator cam_vddio_1v8_en\n");
		regulator_put(cam_vddio_1v8_en);
		cam_vddio_1v8_en = NULL;
		return ret;
	}
    	ENR_usleep(100);

	/* CAM SEL */
	gpio_direction_output(CAM_SEL, 0);
	ENR_usleep(100);

	/* RAW_RSTN */
	gpio_direction_output(RAW_RSTN, 1);
	ENR_msleep(3);

	/* SPI send command to configure RAWCHIP here! */
	rawchip_spi_clock_control(1);
	yushan_spi_write(0x0008, 0x7f);
	ENR_msleep(1);

	/* XSHUTDOWM */
	gpio_direction_output(CAM1_PWDN, 1);
	ENR_usleep(100);

	/* VCM PD */
	gpio_direction_output(CAM1_VCM_PD, 1);
	ENR_usleep(100);

	endeavor_s5k3h2y_power_state = 1;

    return 0;
}

static int endeavor_s5k3h2y_power_off(void)
{

    if (!endeavor_s5k3h2y_power_state) {
		pr_info("[CAM] %s already power off, return 0", __func__);
		return 0;
    }
    endeavor_s5k3h2y_power_state = 0;

    /* VCM PD*/
    gpio_direction_output(CAM1_VCM_PD, 0);
    ENR_msleep(1);

    /* XSHUTDOWN */
    gpio_direction_output(CAM1_PWDN, 0);
    ENR_msleep(1);

    /* RAW RSTN*/
    gpio_direction_output(RAW_RSTN, 0);
    ENR_msleep(3);

   /* VCM */
   regulator_disable(cam_vcm_2v85_en);
   ENR_msleep(1);

    /*RAW_1V2_EN */
    if (htc_get_pcbid_info() < PROJECT_PHASE_XE) {
	gpio_direction_output(RAW_1V2_EN, 0);
	ENR_msleep(5);
    }

   /* digital */
   regulator_disable(cam_d1v2_en);
   ENR_msleep(1);

   /* RAW_1V8_EN */
   gpio_direction_output(RAW_1V8_EN, 0);
   rawchip_spi_clock_control(0);
   ENR_msleep(1);

   /* analog */
   regulator_disable(cam_a2v85_en);
   ENR_msleep(5);

   /* IO */
   regulator_disable(cam_vddio_1v8_en);
   ENR_msleep(10);

   	/* set gpio output low : O(L) */
	tegra_gpio_enable(MCAM_SPI_CLK);
	tegra_gpio_enable(MCAM_SPI_CS0);
	tegra_gpio_enable(MCAM_SPI_DI);
	tegra_gpio_enable(MCAM_SPI_DO);
	tegra_gpio_enable(CAM_I2C_SCL);
	tegra_gpio_enable(CAM_I2C_SDA);
	tegra_gpio_enable(CAM_MCLK);

    return 0;
}

struct s5k3h2yx_platform_data endeavor_s5k3h2yx_data = {
	.sensor_name = "s5k3h2y",
	.data_lane = 2,
	.get_power_state = endeavor_s5k3h2y_get_power,
	.power_on = endeavor_s5k3h2y_power_on,
	.power_off = endeavor_s5k3h2y_power_off,
	.rawchip_need_powercycle = 1,
	.mirror_flip = 0,
	.use_rawchip = RAWCHIP_ENABLE,
};

struct ad5823_platform_data endeavor_ad5823_data = {
	.focal_length = 3.03f,
	.fnumber = 2.0f,
	.pos_low = 96,
	.pos_high = 496,
	.settle_time = 55,
	.get_power_state = endeavor_s5k3h2y_get_power,
	.set_gsensor_mode = GSensor_set_mode,
	.get_gsensor_data = GSensorReadData,
};

static int endeavor_s5k6a1gx03_power_on(void)
{
    int ret;

    gpio_direction_output(FRONT_CAM_RST, 0);
    gpio_direction_output(CAM_SEL, 0);

    tegra_gpio_disable(CAM_I2C_SCL);
    tegra_gpio_disable(CAM_I2C_SDA);
    tegra_gpio_disable(CAM_MCLK);

    /* analog */
        ret = regulator_enable(cam_a2v85_en);
	if (ret < 0) {
		pr_err("[CAM] couldn't enable regulator cam_a2v85_en\n");
		regulator_put(cam_a2v85_en);
		cam_a2v85_en = NULL;
		return ret;
	}
    ENR_usleep(200);
    /* vcm */
	ret = regulator_enable(cam_vcm_2v85_en);
	if (ret < 0) {
		pr_err("[CAM] couldn't enable regulator cam_vcm_2v85_en\n");
		regulator_put(cam_vcm_2v85_en);
		cam_vcm_2v85_en = NULL;
		return ret;
	}
    ENR_usleep(200);
    /* IO */
	ret = regulator_enable(cam_vddio_1v8_en);
	if (ret < 0) {
		pr_err("[CAM] couldn't enable regulator cam_vddio_1v8_en\n");
		regulator_put(cam_vddio_1v8_en);
		cam_vddio_1v8_en = NULL;
		return ret;
	}
    ENR_usleep(200);
    /* RSTN */
    gpio_direction_output(FRONT_CAM_RST, 1);
    /* digital */
	ret = regulator_enable(cam2_d1v2_en);
	if (ret < 0) {
		pr_err("[CAM] couldn't enable regulator cam_d1v2_en\n");
		regulator_put(cam_d1v2_en);
		cam_d1v2_en = NULL;
		return ret;
	}
    /* CAM SEL */
    gpio_direction_output(CAM_SEL, 1);
    ENR_msleep(1);

	/* SPI send command to configure RAWCHIP here! */
	rawchip_spi_clock_control(1);
	yushan_spi_write(0x0008, 0x7f);

    return 0;
}

static int endeavor_s5k6a1gx03_power_off(void)
{
    pr_info("[CAM] s5k6a1g power off ++\n");

    /* CAM SEL */
    gpio_direction_output(CAM_SEL, 0);
    ENR_msleep(1);
    /* vcm */
    regulator_disable(cam_vcm_2v85_en);
    ENR_msleep(1);

    rawchip_spi_clock_control(0);

    /* analog */
    regulator_disable(cam_a2v85_en);
    ENR_msleep(5);
    /* RSTN */
    gpio_direction_output(FRONT_CAM_RST, 0);
    /* digital */
    regulator_disable(cam2_d1v2_en);
    ENR_msleep(1);
    /* IO */
    regulator_disable(cam_vddio_1v8_en);
    ENR_msleep(10);

	tegra_gpio_enable(CAM_I2C_SCL);
	tegra_gpio_enable(CAM_I2C_SDA);
	tegra_gpio_enable(CAM_MCLK);

    return 0;
}

struct s5k6a1gx03_platform_data endeavor_s5k6a1gx03_data = {
	.sensor_name = "s5k6a2gx",
	.data_lane = 1,
	.csi_if = 1,
	.power_on = endeavor_s5k6a1gx03_power_on,
	.power_off = endeavor_s5k6a1gx03_power_off,
	.mirror_flip = 0,
	.use_rawchip = RAWCHIP_DISABLE,
};

static struct i2c_board_info endeavor_i2c3_board_info[] = {
	{
		I2C_BOARD_INFO("s5k3h2y", 0x10),
		.platform_data = &endeavor_s5k3h2yx_data,
	},
	{
		I2C_BOARD_INFO("ad5823", 0x0C),
		.platform_data = &endeavor_ad5823_data,
	}
};

static struct i2c_board_info endeavor_i2c4_board_info[] = {
	{
		I2C_BOARD_INFO("s5k6a1gx03", 0x36),
		.platform_data = &endeavor_s5k6a1gx03_data,
	},
};

struct endeavor_cam_gpio {
	int gpio;
	const char *label;
	int value;
};

#define TEGRA_CAMERA_GPIO(_gpio, _label, _value)	\
	{						\
		.gpio = _gpio,				\
		.label = _label,			\
		.value = _value,			\
	}

static struct endeavor_cam_gpio endeavor_cam_gpio_output_data[] = {
	[0] = TEGRA_CAMERA_GPIO(CAM_SEL, "cam_sel_gpio", 0),
	[1] = TEGRA_CAMERA_GPIO(FRONT_CAM_RST, "front_cam_rst_gpio", 0),
	[2] = TEGRA_CAMERA_GPIO(CAM1_PWDN, "cam_pwdn", 0),
	[3] = TEGRA_CAMERA_GPIO(CAM1_VCM_PD, "cam1_vcm_pd", 0),
	[4] = TEGRA_CAMERA_GPIO(CAM_I2C_SCL, "CAM_I2C_SCL", 0),
	[5] = TEGRA_CAMERA_GPIO(CAM_I2C_SDA, "CAM_I2C_SDA", 0),
	[6] = TEGRA_CAMERA_GPIO(CAM_MCLK, "CAM_MCLK", 0),
	/*for rawchip */
	[7] = TEGRA_CAMERA_GPIO(RAW_1V8_EN, "RAW_1V8_EN", 0),
	[8] = TEGRA_CAMERA_GPIO(RAW_1V2_EN, "RAW_1V2_EN", 0),
	[9] = TEGRA_CAMERA_GPIO(RAW_RSTN, "RAW_RSTN", 0),
	[10] = TEGRA_CAMERA_GPIO(MCAM_SPI_CLK, "MCAM_SPI_CLK", 0),
	[11] = TEGRA_CAMERA_GPIO(MCAM_SPI_CS0, "MCAM_SPI_CS0", 0),
	[12] = TEGRA_CAMERA_GPIO(MCAM_SPI_DI, "MCAM_SPI_DI", 0),
	[13] = TEGRA_CAMERA_GPIO(MCAM_SPI_DO, "MCAM_SPI_DO", 0),
};

static struct endeavor_cam_gpio endeavor_cam_gpio_input_data[] = {
	[0] = TEGRA_CAMERA_GPIO(CAM1_ID, "cam1_id_gpio", 0),
	[1] = TEGRA_CAMERA_GPIO(FRONT_CAM_ID, "front_cam_id_gpio", 0),
	[2] = TEGRA_CAMERA_GPIO(RAW_INTR0, "RAW_INTR0", 0),
	[3] = TEGRA_CAMERA_GPIO(RAW_INTR1, "RAW_INTR1", 0),
};

int endeavoru_cam_init(void)
{
    int ret;
    int i = 0, j = 0;

    for (i = 0; i < ARRAY_SIZE(endeavor_cam_gpio_output_data); i++) {
        ret = gpio_request(endeavor_cam_gpio_output_data[i].gpio,
                   endeavor_cam_gpio_output_data[i].label);
        if (ret < 0) {
            pr_err("[CAM] %s: gpio_request failed for gpio #%d\n",
                __func__, i);
            goto fail_free_gpio;
        }
        gpio_direction_output(endeavor_cam_gpio_output_data[i].gpio,
                      endeavor_cam_gpio_output_data[i].value);
        gpio_export(endeavor_cam_gpio_output_data[i].gpio, false);
        tegra_gpio_enable(endeavor_cam_gpio_output_data[i].gpio);
    }

    for (j = 0; j < ARRAY_SIZE(endeavor_cam_gpio_input_data); j++) {
        ret = gpio_request(endeavor_cam_gpio_input_data[j].gpio,
                   endeavor_cam_gpio_input_data[j].label);
        if (ret < 0) {
            pr_err("[CAM] %s: gpio_request failed for gpio #%d\n",
                __func__, j);
            goto fail_free_gpio;
        }
        gpio_direction_input(endeavor_cam_gpio_input_data[j].gpio);
        gpio_export(endeavor_cam_gpio_input_data[j].gpio, false);
        tegra_gpio_enable(endeavor_cam_gpio_input_data[j].gpio);
    }
/* set gpio input no pull: I(NP) */
    tegra_pinmux_set_pullupdown(TEGRA_PINGROUP_KB_ROW6, TEGRA_PUPD_NORMAL);/* CAM1_ID */
    tegra_pinmux_set_pullupdown(TEGRA_PINGROUP_KB_ROW0, TEGRA_PUPD_NORMAL);/* RAW_INTR0 */
    tegra_pinmux_set_pullupdown(TEGRA_PINGROUP_CLK3_REQ, TEGRA_PUPD_NORMAL);/* RAW_INTR1 */
/* set gpio input no pull: I(PU) */
    tegra_pinmux_set_pullupdown(TEGRA_PINGROUP_KB_ROW7, TEGRA_PUPD_PULL_UP);/* FRONT_CAM_ID */

    i2c_register_board_info(2, endeavor_i2c3_board_info,
        ARRAY_SIZE(endeavor_i2c3_board_info));

    i2c_register_board_info(2, endeavor_i2c4_board_info,
        ARRAY_SIZE(endeavor_i2c4_board_info));

    return 0;

fail_free_gpio:
    pr_err("[CAM] %s endeavor_cam_init failed!\n", __func__);
    while (i--)
        gpio_free(endeavor_cam_gpio_output_data[i].gpio);
    while (j--)
        gpio_free(endeavor_cam_gpio_input_data[j].gpio);
    return ret;
}

int __init endeavor_cam_late_init(void)
{
	int ret = 0;
	printk("%s: \n", __func__);
/* vcm */
      cam_vcm_2v85_en = regulator_get(NULL, "v_cam_vcm_2v85");
		if (IS_ERR_OR_NULL(cam_vcm_2v85_en)) {
			ret = PTR_ERR(cam_vcm_2v85_en);
			pr_err("[CAM] couldn't get regulator v_cam_vcm_2v85\n");
			cam_vcm_2v85_en = NULL;
			return ret;
		}
/* io */
      cam_vddio_1v8_en = regulator_get(NULL, "v_camio_1v8");
		if (IS_ERR_OR_NULL(cam_vddio_1v8_en)) {
			ret = PTR_ERR(cam_vddio_1v8_en);
			pr_err("[CAM] couldn't get regulator v_camio_1v8\n");
			cam_vddio_1v8_en = NULL;
			return ret;
		}
/* analog */
      cam_a2v85_en = regulator_get(NULL, "v_cam_a2v85");
		if (IS_ERR_OR_NULL(cam_a2v85_en)) {
			ret = PTR_ERR(cam_a2v85_en);
			pr_err("[CAM] couldn't get regulator v_cam_a2v85\n");
			cam_a2v85_en = NULL;
			return ret;
		}
/* cam_d1v2 */
      cam_d1v2_en = regulator_get(NULL, "v_cam_d1v2");
		if (IS_ERR_OR_NULL(cam_d1v2_en)) {
			ret = PTR_ERR(cam_d1v2_en);
			pr_err("[CAM] couldn't get regulator v_cam_d1v2\n");
			cam_d1v2_en = NULL;
			return ret;
		}
/* cam2_d1v2 */
      cam2_d1v2_en = regulator_get(NULL, "v_cam2_d1v2");
		if (IS_ERR_OR_NULL(cam2_d1v2_en)) {
			ret = PTR_ERR(cam2_d1v2_en);
			pr_err("[CAM] couldn't get regulator v_cam2_d1v2\n");
			cam2_d1v2_en = NULL;
			return ret;
		}
	return ret;
}
late_initcall(endeavor_cam_late_init);
