// SPDX-License-Identifier: GPL-2.0-only
/*
 * Oplus/realme sensor inventory for the SLPI (SMEM item 130).
 *
 * Oplus's SLPI firmware reads which proximity/light/magnetometer chips exist (chip id, bus number, direction,
 * interrupt) from SMEM item 130, which the downstream kernel fills before the SLPI boots. Without it the SSC only
 * enumerates the default IMU. The structure layout and the DT parsing are adapted from Oplus's GPL
 * oplus_sensor_devinfo driver (Copyright (C) 2018-2020 Oplus) in the realme sm8250 downstream kernel, trimmed to
 * the SMEM table (no procfs, no calibration files). Device tree node: compatible "oplus,sensor-devinfo".
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/soc/qcom/smem.h>

#define SMEM_SENSOR 130
#define CLOSE_PD 1
#define CLOSE_PD_CONDITION 2
#define SAR_MAX_CH_NUM 5
#define SENSOR_DEVINFO_DEBUG(a, arg...) pr_debug(a, ##arg)

#define REG_NUM 10
#define PARAMETER_NUM 25
#define FEATURE_NUM 10
#define SOURCE_NUM 3
#define ALGO_PARAMETER_NUM 15
#define ALGO_FEATURE_NUM 5
#define DEFAULT_CONFIG 0xff

enum sensor_id {
	OPLUS_ACCEL,
	OPLUS_GYRO,
	OPLUS_MAG,
	OPLUS_LIGHT,
	OPLUS_PROXIMITY,
	OPLUS_SAR,
	OPLUS_CCT,
	OPLUS_CCT_REAR,
	OPLUS_BAROMETER,
	OPLUS_SARS,
	OPLUS_LIGHT_REAR,
	SENSORS_NUM
};

enum sensor_algo_id {
	OPLUS_PICKUP_DETECT,
	OPLUS_LUX_AOD,
	OPLUS_TP_GESTURE,
	OPLUS_FP_DISPLAY,
	OPLUS_FREE_FALL,
	OPLUS_CAMERA_PROTECT,
	OPLUS_MAG_FUSION,
	OPLUS_MEASUREMENT,
	SENSOR_ALGO_NUM
};

enum {
	STK3A5X = 0x01,
	TCS3701 = 0x02,
	TCS3408 = 0x04,
	STK3A6X = 0x08,
	STK3329 = 0x10,
};

enum {
	LSM6DSM = 0x01,
	BMI160 = 0x02,
	LSM6DS3_C = 0x04,
	BMI260 = 0x08,
	LSM6DSO = 0x10,
	ICM4X6XX = 0x20,
	ICM4X607 = 0x40,
	BMI3X0 = 0x80,
	LIS2HH12 = 0x81,
	BMA420 = 0x82,
	SC7A20 = 0x84,
};

enum {
	AKM09918 = 0x01,
	MMC5603 = 0x02,
	MXG4300 = 0X04,
};

enum {
	LPS22HH = 0x01,
	BMP380 = 0x02,
};

enum { SX9324 = 0x01, SX9331 = 0x02 };

enum { CCT_TCS3408 = 0x01, CCT_STK37600 = 0x02 };

enum {
	TSL2540 = 0x01,
};

struct sensor_feature {
	int reg[REG_NUM];
	int parameter[PARAMETER_NUM];
	int feature[FEATURE_NUM];
};

struct sensor_hw {
	uint8_t sensor_name;
	uint8_t bus_number;
	uint8_t direction;
	uint8_t irq_number;
	struct sensor_feature feature;
};

struct sensor_vector {
	int sensor_id;
	struct sensor_hw hw[SOURCE_NUM];
};

struct sensor_vector_old {
	int sensor_id;
	struct sensor_hw hw[SOURCE_NUM - 1];
};

struct sensor_algorithm {
	int sensor_id;
	int parameter[ALGO_PARAMETER_NUM];
	int feature[ALGO_FEATURE_NUM];
};

struct sensor_info {
	int magic_num;
	struct sensor_vector s_vector[SENSORS_NUM];
	struct sensor_algorithm a_vector[SENSOR_ALGO_NUM];
};

struct sensor_info_old {
	int magic_num;
	struct sensor_vector_old s_vector[SENSORS_NUM];
	struct sensor_algorithm a_vector[SENSOR_ALGO_NUM];
};

/* project number only selects among per-project magnetometer tables; bitra gives its parameters directly */
static int get_project(void)
{
	return 0;
}

static struct sensor_info *g_chip;
static struct sensor_info_old *g_chip_old;

static char *als_rear_feature[] = {
	"als-factor",
};

static unsigned int get_serialID(void)
{
	return 0;
}
static void is_need_close_pd(struct sensor_hw *hw, struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	int di = 0;
	int sn_size = 0;
	uint32_t *specific_sn = NULL;
	hw->feature.feature[2] = 0;
	rc = of_property_read_u32(ch_node, "is_need_close_pd", &value);

	if (!rc) {
		if (CLOSE_PD == value) {
			hw->feature.feature[2] = CLOSE_PD;
		} else if (CLOSE_PD_CONDITION == value) {
			sn_size = of_property_count_elems_of_size(
				ch_node, "sn_number", sizeof(uint32_t));
			pr_info("sn size %d\n", sn_size);
			specific_sn = (uint32_t *)kzalloc(
				sizeof(uint32_t) * sn_size, GFP_KERNEL);

			if (!specific_sn) {
				pr_err("%s kzalloc failed!\n", __func__);
				return;
			}

			of_property_read_u32_array(ch_node, "sn_number",
						   specific_sn, sn_size);

			for (di = 0; di < sn_size; di++) {
				if (specific_sn[di] == get_serialID()) {
					hw->feature.feature[2] = CLOSE_PD;
					break;
				}
			}

			kfree(specific_sn);
		}
	}
}

static void parse_physical_sensor_common_dts(struct sensor_hw *hw,
					     struct device_node *ch_node)
{
	int rc = 0;
	uint32_t chip_value = 0;
	rc = of_property_read_u32(ch_node, "sensor-name", &chip_value);

	if (rc) {
		hw->sensor_name = 0;
	} else {
		hw->sensor_name = chip_value;
	}

	rc = of_property_read_u32(ch_node, "bus-number", &chip_value);

	if (rc) {
		hw->bus_number = DEFAULT_CONFIG; /*read from registry */
	} else {
		hw->bus_number = chip_value;
	}

	rc = of_property_read_u32(ch_node, "sensor-direction", &chip_value);

	if (rc) {
		hw->direction = DEFAULT_CONFIG; /*read from registry */
	} else {
		hw->direction = chip_value;
	}

	rc = of_property_read_u32(ch_node, "irq-number", &chip_value);

	if (rc) {
		hw->irq_number = DEFAULT_CONFIG; /*read from registry */
	} else {
		hw->irq_number = chip_value;
	}
}

static void parse_magnetic_sensor_dts(struct sensor_hw *hw,
				      struct device_node *ch_node)
{
	int value = 0;
	int rc = 0;
	int di = 0;
	int soft_default_para[18] = { 10000, 0, 0, 0, 0, 0, 0, 0,     10000,
				      0,     0, 0, 0, 0, 0, 0, 10000, 0 };
	/*set default defaut mag */
	memcpy((void *)&hw->feature.parameter[0], (void *)&soft_default_para[0],
	       sizeof(soft_default_para));
	rc = of_property_read_u32(ch_node, "parameter-number", &value);
	if (!rc && value > 0 && value < PARAMETER_NUM) {
		rc = of_property_read_u32_array(ch_node, "soft-mag-parameter",
						&hw->feature.parameter[0],
						value);
		for (di = 0; di < value; di++) {
			SENSOR_DEVINFO_DEBUG(
				"soft magnetic parameter[%d] : %d\n", di,
				hw->feature.parameter[di]);
		}
		return;
	} else if (rc) {
		int prj_id = 0;
		int prj_dir[5];
		struct device_node *node = ch_node;
		struct device_node *ch_node_mag = NULL;
		prj_id = get_project();
		for_each_child_of_node(node, ch_node_mag) {
			if (ch_node_mag == NULL) {
				SENSOR_DEVINFO_DEBUG(
					" the mag_para use default parametyers");
				return;
			}
			rc = of_property_read_u32(ch_node_mag, "projects-num",
						  &value);
			SENSOR_DEVINFO_DEBUG("get that project is %d", prj_id);
			rc = of_property_read_u32_array(ch_node_mag,
							"match-projects",
							&prj_dir[0], value);
			for (di = 0; di < value; di++) {
				SENSOR_DEVINFO_DEBUG(
					" which get there are %d projects",
					prj_dir[di]);
				if (prj_dir[di] == prj_id) {
					rc = of_property_read_u32(
						ch_node_mag, "parameter-number",
						&value);
					if (!rc && value > 0 &&
					    value < PARAMETER_NUM) {
						rc = of_property_read_u32_array(
							ch_node_mag,
							"soft-mag-parameter",
							&hw->feature
								 .parameter[0],
							value);
						for (di = 0; di < value; di++) {
							SENSOR_DEVINFO_DEBUG(
								"soft magnetic parameter[%d] : %d\n",
								di,
								hw->feature.parameter
									[di]);
						}
						return;
					} else {
						pr_info("parse soft magnetic parameter failed!\n");
					}
				} else
					continue;
			}
		}
	} else {
		pr_info("parse soft magnetic parameter failed!\n");
	}
}

static void parse_proximity_sensor_dts(struct sensor_hw *hw,
				       struct device_node *ch_node)
{
	int value = 0;
	int rc = 0;
	int di = 0;
	char *param[] = {
		"low_step",	     "high_step",	  "low_limit",
		"high_limit",	     "dirty_low_step",	  "dirty_high_step",
		"ps_dirty_limit",    "ps_ir_limit",	  "ps_adjust_min",
		"ps_adjust_max",     "sampling_count",	  "step_max",
		"step_min",	     "step_div",	  "anti_shake_delta",
		"dynamic_cali_max",  "raw2offset_radio",  "offset_max",
		"offset_range_min",  "offset_range_max",  "force_cali_limit",
		"cali_jitter_limit", "cal_offset_margin", "esd_gpio_use"
	};
	rc = of_property_read_u32(ch_node, "ps-type", &value);

	if (!rc) {
		hw->feature.feature[0] = value;
	}

	rc = of_property_read_u32(ch_node, "ps_saturation", &value);

	if (!rc) {
		hw->feature.feature[1] = value;
	}

	is_need_close_pd(hw, ch_node);
	rc = of_property_read_u32(ch_node, "ps_factory_cali_max", &value);

	if (!rc) {
		hw->feature.feature[3] = value;
	}

	for (di = 0; di < ARRAY_SIZE(param); di++) {
		rc = of_property_read_u32(ch_node, param[di], &value);

		if (!rc) {
			hw->feature.parameter[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("parameter[%d] : %d\n", di,
				     hw->feature.parameter[di]);
	}

	rc = of_property_read_u32(ch_node, "parameter-number", &value);

	if (!rc && value > 0 && value < REG_NUM - 1) {
		hw->feature.reg[0] = value;
		rc = of_property_read_u32_array(ch_node, "sensor-reg",
						&hw->feature.reg[1], value);

		for (di = 0; di < value / 2; di++) {
			SENSOR_DEVINFO_DEBUG("sensor reg 0x%x = 0x%x\n",
					     hw->feature.reg[di * 2 + 1],
					     hw->feature.reg[2 * di + 2]);
		}
	} else {
		pr_info("parse alsps sensor reg failed\n");
	}

	SENSOR_DEVINFO_DEBUG(
		"ps-type:%d ps_saturation:%d is_need_close_pd:%d\n",
		hw->feature.feature[0], hw->feature.feature[1],
		hw->feature.feature[2]);
}

static void parse_light_sensor_dts(struct sensor_hw *hw,
				   struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	int di = 0;

	char *als_feature[] = { "als-type",
				"is-unit-device",
				"is-als-dri",
				"als-factor",
				"is_als_initialed",
				"als_buffer_length",
				"normalization_value",
				"use_lb_algo",
				"para-matrix",
				"als_ratio_type" };

	char *light_para[] = {
		"coef_a", "coef_b", "coef_c", "coef_d", "coef_e", "coef_ratio",
		"gold-reset-scale", /*gold scale value after sale*/
		/*lb para begin*/
		"lcd_type", "linear_comp_scope", "linear_comp_intercept",
		"temp_lux", "coef", "bal_coef", "ir_coef_stage_0",
		"ir_coef_stage_1", "ir_coef_stage_2", "ir_coef_stage_3",
		"ir_coef_val_0", "ir_coef_val_1", "ir_coef_val_2",
		"ir_coef_val_3", "ir_coef_val_4", "lb-dbv-coefs-id",
		"light-cut",
		/*lb para end*/
	};

	for (di = 0; di < ARRAY_SIZE(als_feature); di++) {
		rc = of_property_read_u32(ch_node, als_feature[di], &value);

		if (!rc) {
			hw->feature.feature[di] = value;
		} else if (0 == strncmp(als_feature[di], "norm", 4)) {
			hw->feature.feature[di] = 1057;
		} else if (0 == strncmp(als_feature[di], "als_ratio_type",
					strlen("als_ratio_type"))) {
			hw->feature.feature[di] = 0; /*set defaut zero*/
		} else {
			pr_info("parse %s failed!", als_feature[di]);
		}

		SENSOR_DEVINFO_DEBUG("light feature[%s] : %d\n",
				     als_feature[di], hw->feature.feature[di]);
	}

	for (di = 0; di < ARRAY_SIZE(light_para); di++) {
		rc = of_property_read_u32(ch_node, light_para[di], &value);

		if (!rc) {
			hw->feature.parameter[di] = value;
		} else if (0 == strncmp(light_para[di], "gold-reset-scale",
					strlen("gold-reset-scale"))) {
			hw->feature.parameter[di] =
				1001; /*set defaut value 1001*/
		} else {
			hw->feature.parameter[di] = 0; /*set defaut param*/
			pr_info("parse %s failed!", light_para[di]);
		}
		SENSOR_DEVINFO_DEBUG("light_para[%s] : %d\n", light_para[di],
				     hw->feature.parameter[di]);
	}
}
static void parse_light_rear_sensor_dts(struct sensor_hw *hw,
					struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	int di = 0;

	for (di = 0; di < ARRAY_SIZE(als_rear_feature); di++) {
		rc = of_property_read_u32(ch_node, als_rear_feature[di],
					  &value);

		if (!rc) {
			hw->feature.feature[di] = value;
		} else {
			pr_info("parse %s failed!", als_rear_feature[di]);
		}

		SENSOR_DEVINFO_DEBUG(
			"parse_light_rear_sensor_dts-feature[%d] : %d\n", di,
			hw->feature.feature[di]);
	}
}

static void parse_sar_sensor_dts(struct sensor_hw *hw,
				 struct device_node *ch_node)
{
	int di = 0;
	int rc = 0;
	int value = 0;
	int dc_offset_default[SAR_MAX_CH_NUM * 2] = {
		0, 0, 0, 0, 0, 30000, 30000, 30000, 30000, 30000
	};
	rc = of_property_read_u32(ch_node, "parameter-number", &value);
	if (!rc && value > 0 && value < PARAMETER_NUM) {
		rc = of_property_read_u32_array(ch_node, "sensor-reg",
						&hw->feature.parameter[0],
						value);
		for (di = 0; di < value / 2; di++) {
			SENSOR_DEVINFO_DEBUG("sensor reg 0x%x = 0x%x\n",
					     hw->feature.parameter[di * 2],
					     hw->feature.parameter[2 * di + 1]);
		}
	} else {
		pr_info("parse sar sensor reg failed\n");
	}
	/*channel-num*/
	rc = of_property_read_u32(ch_node, "channel-num", &value);
	if (!rc && value < SAR_MAX_CH_NUM) {
		hw->feature.feature[di] = value;
		SENSOR_DEVINFO_DEBUG("sar channel-num: %d\n", value);
	} else {
		pr_info("parse sar sensor channel-num failed, rc %d, value %d",
			rc, value);
	}
	/*reg->dc_offset*/
	rc = of_property_read_u32(ch_node, "is-dc-offset", &value);
	if (!rc && value == 1) {
		memcpy((void *)&hw->feature.reg[0],
		       (void *)&dc_offset_default[0], SAR_MAX_CH_NUM * 2);
		for (di = 0; di < SAR_MAX_CH_NUM; di++) {
			SENSOR_DEVINFO_DEBUG(
				"sar dc_offset_l[%d] = %d, dc_offset_H[%d] = %d",
				di, hw->feature.reg[di], di + SAR_MAX_CH_NUM,
				hw->feature.reg[di + SAR_MAX_CH_NUM]);
		}
		rc = of_property_read_u32_array(ch_node, "dc-offset",
						&hw->feature.reg[0],
						SAR_MAX_CH_NUM * 2);
		for (di = 0; di < SAR_MAX_CH_NUM; di++) {
			SENSOR_DEVINFO_DEBUG(
				"sar dc_offset_l[%d] = %d, dc_offset_H[%d] = %d",
				di, hw->feature.reg[di], di + SAR_MAX_CH_NUM,
				hw->feature.reg[di + SAR_MAX_CH_NUM]);
		}
	} else {
		pr_info("parse sar sensor dc_offset failed, rc %d, value %d",
			rc, value);
	}
}

static void parse_down_sar_sensor_dts(struct sensor_hw *hw,
				      struct device_node *ch_node)
{
	int di = 0;
	int rc = 0;
	int value = 0;
	rc = of_property_read_u32(ch_node, "parameter-number", &value);

	if (!rc && value > 0 && value < PARAMETER_NUM) {
		rc = of_property_read_u32_array(ch_node, "sensor-reg",
						&hw->feature.parameter[0],
						value);

		for (di = 0; di < value / 2; di++) {
			SENSOR_DEVINFO_DEBUG("sensor reg 0x%x = 0x%x\n",
					     hw->feature.parameter[di * 2],
					     hw->feature.parameter[2 * di + 1]);
		}

	} else {
		pr_info("parse down-sar sensor reg failed\n");
	}
}

static void parse_cct_sensor_dts(struct sensor_hw *hw,
				 struct device_node *ch_node)
{
	int value = 0;
	int rc = 0;
	int di = 0;
	char *feature[] = { "decoupled-driver", "publish-sensors", "is-ch-dri",
			    "timer-size", "fac-cali-sensor" };

	char *para[] = { "para-matrix",	  "atime",
			 "first-atime",	  "fac-cali-atime",
			 "first-again",	  "fac-cali-again",
			 "fd-time",	  "fac-cali-fd-time",
			 "first-fd-gain", "fac-cali-fd-gain",
			 "algo-flag" };

	hw->feature.feature[0] = 1; /*default use decoupled driver oplus_cct */

	for (di = 0; di < ARRAY_SIZE(feature); di++) {
		rc = of_property_read_u32(ch_node, feature[di], &value);

		if (!rc) {
			hw->feature.feature[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("cct_feature[%d] : %d\n", di,
				     hw->feature.feature[di]);
	}

	for (di = 0; di < ARRAY_SIZE(para); di++) {
		rc = of_property_read_u32(ch_node, para[di], &value);

		if (!rc) {
			hw->feature.parameter[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("cct_parameter[%d] : %d\n", di,
				     hw->feature.parameter[di]);
	}
}

static void parse_cct_rear_sensor_dts(struct sensor_hw *hw,
				      struct device_node *ch_node)
{
	int value = 0;
	int rc = 0;
	int di = 0;
	char *feature[] = { "decoupled-driver", "publish-sensors", "is-ch-dri",
			    "timer-size", "fac-cali-sensor" };

	char *para[] = { "para-matrix",	  "atime",
			 "first-atime",	  "fac-cali-atime",
			 "first-again",	  "fac-cali-again",
			 "fd-time",	  "fac-cali-fd-time",
			 "first-fd-gain", "fac-cali-fd-gain" };

	hw->feature.feature[0] = 1; /*default use decoupled driver oplus_cct */

	for (di = 0; di < ARRAY_SIZE(feature); di++) {
		rc = of_property_read_u32(ch_node, feature[di], &value);

		if (!rc) {
			hw->feature.feature[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("cct_feature[%d] : %d\n", di,
				     hw->feature.feature[di]);
	}

	for (di = 0; di < ARRAY_SIZE(para); di++) {
		rc = of_property_read_u32(ch_node, para[di], &value);

		if (!rc) {
			hw->feature.parameter[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("cct_parameter[%d] : %d\n", di,
				     hw->feature.parameter[di]);
	}
}

static void parse_accelerometer_sensor_dts(struct sensor_hw *hw,
					   struct device_node *ch_node)
{
	int value = 0;
	int rc = 0;
	int di = 0;
	char *feature[] = { "use-sois", "single-acc" };

	hw->feature.feature[0] = 0; /*default not use s-ois */

	for (di = 0; di < ARRAY_SIZE(feature); di++) {
		rc = of_property_read_u32(ch_node, feature[di], &value);

		if (!rc) {
			hw->feature.feature[di] = value;
		}

		SENSOR_DEVINFO_DEBUG("gsensor_feature[%d] : %d\n", di,
				     hw->feature.feature[di]);
	}
}

static void parse_each_physical_sensor_dts(struct sensor_hw *hw,
					   struct device_node *ch_node)
{
	if (0 == strncmp(ch_node->name, "msensor", 7)) {
		parse_magnetic_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "psensor", 7)) {
		parse_proximity_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "lsensor", 7)) {
		parse_light_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "ssensor", 7)) {
		parse_sar_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "cctsensor", 7)) {
		parse_cct_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "cctrsensor", 7)) {
		parse_cct_rear_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "sdsensor", 7)) {
		parse_down_sar_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "lrsensor", 7)) {
		parse_light_rear_sensor_dts(hw, ch_node);
	} else if (0 == strncmp(ch_node->name, "gsensor", 7)) {
		parse_accelerometer_sensor_dts(hw, ch_node);
	} else {
		/*do nothing */
	}
}

static void parse_pickup_sensor_dts(struct sensor_algorithm *algo,
				    struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	rc = of_property_read_u32(ch_node, "is-need-prox", &value);

	if (!rc) {
		algo->feature[0] = value;
	}

	rc = of_property_read_u32(ch_node, "prox-type", &value);

	if (!rc) {
		algo->parameter[0] = value;
	}

	SENSOR_DEVINFO_DEBUG("is-need-prox: %d, prox-type: %d\n",
			     algo->feature[0], algo->parameter[0]);
}

static void parse_lux_aod_sensor_dts(struct sensor_algorithm *algo,
				     struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	rc = of_property_read_u32(ch_node, "thrd-low", &value);

	if (!rc) {
		algo->parameter[0] = value;
	}

	rc = of_property_read_u32(ch_node, "thrd-high", &value);

	if (!rc) {
		algo->parameter[1] = value;
	}

	rc = of_property_read_u32(ch_node, "als-type", &value);

	if (!rc) {
		algo->parameter[2] = value;
	}

	rc = of_property_read_u32(ch_node, "fold-feature", &value);

	if (!rc) {
		algo->feature[0] = value;
	}

	SENSOR_DEVINFO_DEBUG(
		"thrd-low: %d, thrd-high: %d, als-type: %d, fold-feature: %d\n",
		algo->parameter[0], algo->parameter[1], algo->parameter[2],
		algo->feature[0]);
}

static void parse_fp_display_sensor_dts(struct sensor_algorithm *algo,
					struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;
	rc = of_property_read_u32(ch_node, "prox-type", &value);

	if (!rc) {
		algo->parameter[0] = value;
	}

	SENSOR_DEVINFO_DEBUG("prox-type :%d\n", algo->parameter[0]);
}

static void parse_mag_fusion_sensor_dts(struct sensor_algorithm *algo,
					struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;

	rc = of_property_read_u32(ch_node, "fusion-type", &value);

	if (!rc) {
		algo->feature[0] = value;
	}

	rc = of_property_read_u32(ch_node, "mag-type", &value);

	if (!rc) {
		algo->feature[1] = value;
	}

	SENSOR_DEVINFO_DEBUG("fusion-type :%d mag-type : %d\n",
			     algo->feature[0], algo->feature[1]);
}

static void parse_oplus_measurement_sensor_dts(struct sensor_algorithm *algo,
					       struct device_node *ch_node)
{
	int rc = 0;
	int value = 0;

	rc = of_property_read_u32(ch_node, "acc-range", &value);

	if (!rc) {
		algo->parameter[0] = value;
	}

	rc = of_property_read_u32(ch_node, "gyro-range", &value);

	if (!rc) {
		algo->parameter[1] = value;
	}

	SENSOR_DEVINFO_DEBUG("acc-range: %d, gyro-range: %d\n",
			     algo->parameter[0], algo->parameter[1]);
}

static void parse_each_virtual_sensor_dts(struct sensor_algorithm *algo,
					  struct device_node *ch_node)
{
	if (0 == strncmp(ch_node->name, "pickup", 6)) {
		parse_pickup_sensor_dts(algo, ch_node);
	} else if (0 == strncmp(ch_node->name, "lux_aod", 6)) {
		parse_lux_aod_sensor_dts(algo, ch_node);
	} else if (0 == strncmp(ch_node->name, "fp_display", 6)) {
		parse_fp_display_sensor_dts(algo, ch_node);
	} else if (0 == strncmp(ch_node->name, "mag_fusion", 10)) {
		parse_mag_fusion_sensor_dts(algo, ch_node);
	} else if (0 == strncmp(ch_node->name, "oplus_measurement", 17)) {
		parse_oplus_measurement_sensor_dts(algo, ch_node);
	} else {
		/* do nothing */
	}
}

static void oplus_sensor_parse_dts(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	int rc = 0;
	int value = 0;
	bool is_virtual_sensor = false;
	struct device_node *ch_node = NULL;
	int sensor_type = 0;
	int sensor_index = 0;
	struct sensor_hw *hw = NULL;
	struct sensor_algorithm *algo = NULL;
	pr_info("start \n");

	for_each_child_of_node(node, ch_node) {
		is_virtual_sensor = false;

		if (of_property_read_bool(ch_node, "is-virtual-sensor")) {
			is_virtual_sensor = true;
		}

		rc = of_property_read_u32(ch_node, "sensor-type", &value);

		if (rc || (is_virtual_sensor && value >= SENSOR_ALGO_NUM) ||
		    value >= SENSORS_NUM) {
			pr_info("parse sensor type failed!\n");
			continue;
		} else {
			sensor_type = value;
		}

		if (!is_virtual_sensor) {
			if (g_chip) {
				g_chip->s_vector[sensor_type].sensor_id =
					sensor_type;
			} else if (g_chip_old) {
				g_chip_old->s_vector[sensor_type].sensor_id =
					sensor_type;
			}
			rc = of_property_read_u32(ch_node, "sensor-index",
						  &value);

			if (rc || value >= SOURCE_NUM) {
				pr_info("parse sensor index failed!\n");
				continue;
			} else {
				sensor_index = value;
			}

			if (g_chip) {
				hw = &g_chip->s_vector[sensor_type]
					      .hw[sensor_index];
			} else if (g_chip_old) {
				hw = &g_chip_old->s_vector[sensor_type]
					      .hw[sensor_index];
			}
			parse_physical_sensor_common_dts(hw, ch_node);
			parse_each_physical_sensor_dts(hw, ch_node);
		} else {
			if (g_chip) {
				g_chip->a_vector[sensor_type].sensor_id =
					sensor_type;
				algo = &g_chip->a_vector[sensor_type];
			} else if (g_chip_old) {
				g_chip_old->a_vector[sensor_type].sensor_id =
					sensor_type;
				algo = &g_chip_old->a_vector[sensor_type];
			}
			parse_each_virtual_sensor_dts(algo, ch_node);
		}
	} /*for_each_child_of_node */
}

static int oplus_devinfo_probe(struct platform_device *pdev)
{
	size_t smem_size = 0;
	void *smem_addr;
	int rc;

	rc = qcom_smem_alloc(QCOM_SMEM_HOST_ANY, SMEM_SENSOR, (sizeof(struct sensor_info) + 3) & ~3);
	if (rc < 0 && rc != -EEXIST)
		return dev_err_probe(&pdev->dev, rc, "cannot allocate SMEM item %d\n", SMEM_SENSOR);

	smem_addr = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_SENSOR, &smem_size);
	if (IS_ERR(smem_addr))
		return dev_err_probe(&pdev->dev, PTR_ERR(smem_addr), "cannot get SMEM item %d\n", SMEM_SENSOR);

	if (smem_size == sizeof(struct sensor_info)) {
		g_chip = smem_addr;
		memset(g_chip, 0, sizeof(*g_chip));
	} else if (smem_size == sizeof(struct sensor_info_old)) {
		g_chip_old = smem_addr;
		memset(g_chip_old, 0, sizeof(*g_chip_old));
	} else {
		dev_err(&pdev->dev, "unsupported SMEM item size %zu (known: %zu, %zu)\n", smem_size,
			sizeof(struct sensor_info), sizeof(struct sensor_info_old));
		return -EINVAL;
	}

	oplus_sensor_parse_dts(pdev);
	dev_info(&pdev->dev, "sensor inventory written to SMEM item %d (%zu bytes, %s layout)\n", SMEM_SENSOR,
		 smem_size, g_chip ? "3 sources" : "2 sources");
	return 0;
}

static const struct of_device_id oplus_devinfo_of_match[] = {
	{ .compatible = "oplus,sensor-devinfo" },
	{ }
};
MODULE_DEVICE_TABLE(of, oplus_devinfo_of_match);

static struct platform_driver oplus_devinfo_driver = {
	.probe = oplus_devinfo_probe,
	.driver = {
		.name = "oplus-sensor-devinfo",
		.of_match_table = oplus_devinfo_of_match,
	},
};
module_platform_driver(oplus_devinfo_driver);

MODULE_DESCRIPTION("Oplus sensor inventory for the SLPI (SMEM item 130)");
MODULE_LICENSE("GPL");
