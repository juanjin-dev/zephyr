/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 RAKwireless Technology Limited
 */

#include <zephyr/init.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/devicetree/io-channels.h>
#include <zephyr/dt-bindings/pinctrl/rpi-pico-pinctrl-common.h>
#include <zephyr/dt-bindings/pinctrl/rpi-pico-rp2040-pinctrl.h>

#define RP2_PINMUX_CFG(pinmux)								\
	{										\
		.pin_num = RP2_GET_PIN_NUM(pinmux),					\
		.alt_func = RP2_GET_PIN_ALT_FUNC(pinmux),				\
	}

#define WISBLOCK_STATES(name)								\
	PINCTRL_DT_DEV_CONFIG_DECLARE(DT_NODELABEL(name));				\
	static const struct pinctrl_state name##_states[] = {				\
		{.pins = name##_pins, .pin_cnt = ARRAY_SIZE(name##_pins),		\
		 .id = PINCTRL_STATE_DEFAULT},						\
		{.pins = name##_sleep, .pin_cnt = ARRAY_SIZE(name##_sleep),		\
		 .id = PINCTRL_STATE_SLEEP},						\
	}

#define WISBLOCK_UPDATE(name)								\
	(void)pinctrl_update_states(PINCTRL_DT_DEV_CONFIG_GET(DT_NODELABEL(name)),	\
				    name##_states, ARRAY_SIZE(name##_states))

#if defined(CONFIG_PWM)

#define WISBLOCK_PWM_MARK(node, ch)							\
	COND_CODE_1(DT_NODE_HAS_PROP(node, pwms),					\
		(COND_CODE_1(DT_SAME_NODE(DT_PWMS_CTLR_BY_IDX(node, 0),			\
					  DT_NODELABEL(pwm)),				\
			(COND_CODE_1(IS_EQ(DT_PWMS_CHANNEL_BY_IDX(node, 0), ch),	\
				(x), ())),						\
			())),								\
		())

#define WISBLOCK_PWM_ACTIVE(ch)								\
	UTIL_NOT(IS_EMPTY(DT_FOREACH_STATUS_OKAY_NODE_VARGS(WISBLOCK_PWM_MARK, ch)))

#define WISBLOCK_PWM_PIN(ch, pinmux)							\
	COND_CODE_1(WISBLOCK_PWM_ACTIVE(ch), (RP2_PINMUX_CFG(pinmux),), ())

static const pinctrl_soc_pin_t pwm_pins[] = {
	WISBLOCK_PWM_PIN(6, PWM_3A_P6)
	WISBLOCK_PWM_PIN(7, PWM_3B_P7)
	WISBLOCK_PWM_PIN(8, PWM_4A_P8)
	WISBLOCK_PWM_PIN(9, PWM_4B_P9)
	WISBLOCK_PWM_PIN(12, PWM_6A_P28)
};

static const pinctrl_soc_pin_t pwm_sleep[] = {
	WISBLOCK_PWM_PIN(6, GPIO_P6)
	WISBLOCK_PWM_PIN(7, GPIO_P7)
	WISBLOCK_PWM_PIN(8, GPIO_P8)
	WISBLOCK_PWM_PIN(9, GPIO_P9)
	WISBLOCK_PWM_PIN(12, GPIO_P28)
};

WISBLOCK_STATES(pwm);
#endif /* CONFIG_PWM */

#if defined(CONFIG_ADC)

#define WISBLOCK_ADC_MATCH(node, prop, idx, input)					\
	COND_CODE_1(DT_SAME_NODE(DT_IO_CHANNELS_CTLR_BY_IDX(node, idx),			\
				 DT_NODELABEL(adc)),					\
		(COND_CODE_1(IS_EQ(DT_IO_CHANNELS_INPUT_BY_IDX(node, idx), input),	\
			(x), ())),							\
		())

#define WISBLOCK_ADC_MARK(node, input)							\
	COND_CODE_1(DT_NODE_HAS_PROP(node, io_channels),				\
		(DT_FOREACH_PROP_ELEM_VARGS(node, io_channels,				\
					    WISBLOCK_ADC_MATCH, input)),		\
		())

#define WISBLOCK_ADC_ACTIVE(input)							\
	UTIL_NOT(IS_EMPTY(DT_FOREACH_STATUS_OKAY_NODE_VARGS(WISBLOCK_ADC_MARK, input)))

#define WISBLOCK_ADC_PIN(input, pinmux)							\
	COND_CODE_1(WISBLOCK_ADC_ACTIVE(input), (RP2_PINMUX_CFG(pinmux),), ())

static const pinctrl_soc_pin_t adc_pins[] = {
	WISBLOCK_ADC_PIN(0, ADC_CH0_P26)
	WISBLOCK_ADC_PIN(1, ADC_CH1_P27)
	WISBLOCK_ADC_PIN(2, ADC_CH2_P28)
};

static const pinctrl_soc_pin_t adc_sleep[] = {
	WISBLOCK_ADC_PIN(0, GPIO_P26)
	WISBLOCK_ADC_PIN(1, GPIO_P27)
	WISBLOCK_ADC_PIN(2, GPIO_P28)
};

WISBLOCK_STATES(adc);
#endif /* CONFIG_ADC */

void board_early_init_hook(void)
{
#if defined(CONFIG_PWM)
	WISBLOCK_UPDATE(pwm);
#endif
#if defined(CONFIG_ADC)
	WISBLOCK_UPDATE(adc);
#endif
}
