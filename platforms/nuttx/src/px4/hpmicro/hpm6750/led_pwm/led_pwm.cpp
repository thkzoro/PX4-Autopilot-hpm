/****************************************************************************
 *
 *   Copyright (c) 2026 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file led_pwm.cpp
 *
 * Custom RGB LED PWM backend for the HPM6750 onboard RGB LED.
 */

#include <px4_platform_common/px4_config.h>

#include "board.h"
#include "hpm_pwm_drv.h"

namespace
{

constexpr uint32_t RGB_PWM_HZ = 1000;

struct RGBChannelConfig {
	PWM_Type *pwm;
	uint8_t output;
	uint8_t cmp;
	uint32_t clock_name;
};

const RGBChannelConfig g_channels[3] = {
	{BOARD_RED_PWM, BOARD_RED_PWM_OUT, BOARD_RED_PWM_CMP, BOARD_RED_PWM_CLOCK_NAME},
	{BOARD_GREEN_PWM, BOARD_GREEN_PWM_OUT, BOARD_GREEN_PWM_CMP, BOARD_GREEN_PWM_CLOCK_NAME},
	{BOARD_BLUE_PWM, BOARD_BLUE_PWM_OUT, BOARD_BLUE_PWM_CMP, BOARD_BLUE_PWM_CLOCK_NAME},
};

uint8_t g_values[3] {};
bool g_initialized{false};

uint32_t pwm_reload_for_clock(uint32_t clock_name)
{
	const uint32_t clock_hz = clock_get_frequency(static_cast<clock_name_t>(clock_name));
	const uint32_t reload = clock_hz / RGB_PWM_HZ;
	return (reload > 255u) ? reload : 255u;
}

void configure_rgb_channel(const RGBChannelConfig &config, uint32_t reload)
{
	pwm_cmp_config_t cmp_config{};
	pwm_output_channel_t channel_config{};
	pwm_config_t pwm_config{};

	pwm_stop_counter(config.pwm);
	pwm_set_reload(config.pwm, 0, reload);
	pwm_set_start_count(config.pwm, 0, 0);

	pwm_get_default_cmp_config(config.pwm, &cmp_config);
	pwm_get_default_output_channel_config(config.pwm, &channel_config);
	pwm_get_default_pwm_config(config.pwm, &pwm_config);

	cmp_config.mode = pwm_cmp_mode_output_compare;
	cmp_config.update_trigger = pwm_shadow_register_update_on_modify;
	cmp_config.cmp = reload;

	channel_config.cmp_start_index = config.cmp;
	channel_config.cmp_end_index = config.cmp;
	channel_config.invert_output = !board_get_led_pwm_off_level();

	pwm_config.invert_output = channel_config.invert_output;
	pwm_config.enable_output = false;

	pwm_config_cmp(config.pwm, config.cmp, &cmp_config);
	pwm_config_output_channel(config.pwm, config.output, &channel_config);
	pwm_config_pwm(config.pwm, config.output, &pwm_config, false);
	pwm_disable_output(config.pwm, config.output);
	pwm_start_counter(config.pwm);
}

void set_rgb_channel(unsigned channel, uint8_t value)
{
	if (channel >= 3) {
		return;
	}

	g_values[channel] = value;

	const RGBChannelConfig &config = g_channels[channel];
	const uint32_t reload = pwm_get_reload_val(config.pwm);

	if (value == 0) {
		pwm_disable_output(config.pwm, config.output);
		pwm_cmp_update_cmp_value(config.pwm, config.cmp, reload, 0);

	} else {
		pwm_enable_output(config.pwm, config.output);
		const float duty = (static_cast<float>(value) / 255.f) * 100.f;
		pwm_update_duty_edge_aligned(config.pwm, config.cmp, duty);
	}
}

} // namespace

int led_pwm_servo_set(unsigned channel, uint8_t value)
{
	set_rgb_channel(channel, value);
	return 0;
}

unsigned led_pwm_servo_get(unsigned channel)
{
	return (channel < 3) ? g_values[channel] : 0;
}

int led_pwm_servo_set_ex(uint8_t r, uint8_t g, uint8_t b, uint8_t led_count)
{
	(void)led_count;
	set_rgb_channel(0, r);
	set_rgb_channel(1, g);
	set_rgb_channel(2, b);
	return 0;
}

int led_pwm_servo_init(void)
{
	board_init_rgb_pwm_pins();

	configure_rgb_channel(g_channels[0], pwm_reload_for_clock(g_channels[0].clock_name));
	configure_rgb_channel(g_channels[1], pwm_reload_for_clock(g_channels[1].clock_name));
	configure_rgb_channel(g_channels[2], pwm_reload_for_clock(g_channels[2].clock_name));

	g_values[0] = 0;
	g_values[1] = 0;
	g_values[2] = 0;
	g_initialized = true;
	return 0;
}

void led_pwm_servo_deinit(void)
{
	if (!g_initialized) {
		return;
	}

	for (const auto &config : g_channels) {
		pwm_disable_output(config.pwm, config.output);
		pwm_stop_counter(config.pwm);
	}

	board_init_led_pins();
	g_values[0] = 0;
	g_values[1] = 0;
	g_values[2] = 0;
	g_initialized = false;
}
