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
 * @file ToneAlarmInterface.cpp
 *
 * Tone alarm backend for the HPM6750 onboard buzzer.
 */

#include <drivers/drv_tone_alarm.h>
#include <nuttx/irq.h>
#include <px4_platform_common/defines.h>

#include <board_config.h>
#include "board.h"
#include "hpm_pwm_drv.h"

namespace
{

bool g_initialized{false};

void configure_beeper_channel(uint32_t reload)
{
	pwm_cmp_config_t cmp_config{};
	pwm_output_channel_t channel_config{};
	pwm_config_t pwm_config{};

	pwm_stop_counter(BOARD_BEEP_PWM);
	pwm_set_reload(BOARD_BEEP_PWM, 0, reload);
	pwm_set_start_count(BOARD_BEEP_PWM, 0, 0);

	pwm_get_default_cmp_config(BOARD_BEEP_PWM, &cmp_config);
	pwm_get_default_output_channel_config(BOARD_BEEP_PWM, &channel_config);
	pwm_get_default_pwm_config(BOARD_BEEP_PWM, &pwm_config);

	cmp_config.mode = pwm_cmp_mode_output_compare;
	cmp_config.update_trigger = pwm_shadow_register_update_on_modify;
	cmp_config.cmp = reload / 2;

	channel_config.cmp_start_index = BOARD_BEEP_PWM_OUT;
	channel_config.cmp_end_index = BOARD_BEEP_PWM_OUT;

	pwm_config_cmp(BOARD_BEEP_PWM, BOARD_BEEP_PWM_OUT, &cmp_config);
	pwm_config_output_channel(BOARD_BEEP_PWM, BOARD_BEEP_PWM_OUT, &channel_config);
	pwm_config_pwm(BOARD_BEEP_PWM, BOARD_BEEP_PWM_OUT, &pwm_config, false);
	pwm_enable_output(BOARD_BEEP_PWM, BOARD_BEEP_PWM_OUT);
	pwm_start_counter(BOARD_BEEP_PWM);
}

} // namespace

namespace ToneAlarmInterface
{

void init()
{
	px4_arch_configgpio(GPIO_TONE_ALARM_IDLE);
	g_initialized = true;
}

hrt_abstime start_note(unsigned frequency)
{
	if (!g_initialized || frequency == 0) {
		return 0;
	}

	board_init_beep_pwm_pins();

	const uint32_t clock_hz = clock_get_frequency(static_cast<clock_name_t>(BOARD_BEEP_PWM_CLOCK_NAME));
	const uint32_t computed_reload = clock_hz / frequency;
	const uint32_t reload = (computed_reload > 2u) ? computed_reload : 2u;

	configure_beeper_channel(reload);

	irqstate_t flags = enter_critical_section();
	const hrt_abstime time_started = hrt_absolute_time();
	leave_critical_section(flags);

	return time_started;
}

void stop_note()
{
	if (!g_initialized) {
		return;
	}

	pwm_disable_output(BOARD_BEEP_PWM, BOARD_BEEP_PWM_OUT);
	pwm_stop_counter(BOARD_BEEP_PWM);
	px4_arch_configgpio(GPIO_TONE_ALARM_IDLE);
}

} // namespace ToneAlarmInterface
