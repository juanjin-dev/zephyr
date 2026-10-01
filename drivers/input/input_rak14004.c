/*
 * Copyright (c) 2026 RAKwireless Technology Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT rakwireless_rak14004

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rak14004, CONFIG_INPUT_LOG_LEVEL);

#define RAK14004_REG_KEYPAD 0x01

#define RAK14004_MAX_ROWS 8

struct rak14004_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec int_gpio;
	struct gpio_dt_spec reset_gpio;
	uint8_t row_size;
	uint8_t col_size;
};

struct rak14004_data {
	const struct device *dev;
	struct k_work work;
	struct gpio_callback int_gpio_cb;
	uint8_t state[RAK14004_MAX_ROWS];
};

static void rak14004_work_handler(struct k_work *work)
{
	struct rak14004_data *data = CONTAINER_OF(work, struct rak14004_data, work);
	const struct device *dev = data->dev;
	const struct rak14004_config *cfg = dev->config;
	uint8_t rows[RAK14004_MAX_ROWS];
	int ret;

	ret = i2c_burst_read_dt(&cfg->i2c, RAK14004_REG_KEYPAD, rows, cfg->row_size);
	if (ret < 0) {
		LOG_ERR("Could not read the key matrix: %d", ret);
		return;
	}

	for (uint8_t row = 0; row < cfg->row_size; row++) {
		uint8_t changed = rows[row] ^ data->state[row];

		data->state[row] = rows[row];

		for (uint8_t col = 0; col < cfg->col_size; col++) {
			if ((changed & BIT(col)) == 0U) {
				continue;
			}

			input_report_abs(dev, INPUT_ABS_X, col, false, K_NO_WAIT);
			input_report_abs(dev, INPUT_ABS_Y, row, false, K_NO_WAIT);
			input_report_key(dev, INPUT_BTN_TOUCH, (rows[row] & BIT(col)) != 0U, true,
					 K_NO_WAIT);
		}
	}
}

static void rak14004_int_callback(const struct device *port, struct gpio_callback *cb,
				  uint32_t pins)
{
	struct rak14004_data *data = CONTAINER_OF(cb, struct rak14004_data, int_gpio_cb);

	k_work_submit(&data->work);
}

static int rak14004_init(const struct device *dev)
{
	const struct rak14004_config *cfg = dev->config;
	struct rak14004_data *data = dev->data;
	int ret;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->i2c.bus);
		return -ENODEV;
	}

	if (cfg->reset_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->reset_gpio)) {
			LOG_ERR_DEVICE_NOT_READY(cfg->reset_gpio.port);
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Could not release the reset line: %d", ret);
			return ret;
		}
	}

	data->dev = dev;
	k_work_init(&data->work, rak14004_work_handler);

	if (!gpio_is_ready_dt(&cfg->int_gpio)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->int_gpio.port);
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->int_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Could not configure the interrupt pin: %d", ret);
		return ret;
	}

	gpio_init_callback(&data->int_gpio_cb, rak14004_int_callback, BIT(cfg->int_gpio.pin));

	ret = gpio_add_callback_dt(&cfg->int_gpio, &data->int_gpio_cb);
	if (ret < 0) {
		LOG_ERR("Could not add the interrupt callback: %d", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&cfg->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Could not enable the interrupt: %d", ret);
		return ret;
	}

	return 0;
}

#define RAK14004_INIT(inst)                                                                        \
	BUILD_ASSERT(DT_INST_PROP(inst, row_size) <= RAK14004_MAX_ROWS, "too many rows");          \
                                                                                                   \
	static const struct rak14004_config rak14004_config_##inst = {                             \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.int_gpio = GPIO_DT_SPEC_INST_GET(inst, int_gpios),                                \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),                    \
		.row_size = DT_INST_PROP(inst, row_size),                                          \
		.col_size = DT_INST_PROP(inst, col_size),                                          \
	};                                                                                         \
                                                                                                   \
	static struct rak14004_data rak14004_data_##inst;                                          \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, rak14004_init, NULL, &rak14004_data_##inst,                    \
			      &rak14004_config_##inst, POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY,    \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(RAK14004_INIT)
