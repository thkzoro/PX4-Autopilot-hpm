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
 * @file adc.cpp
 *
 * Minimal HPM6750 ADC support for PX4 board_adc.
 * This is used for PM02 analog voltage/current sensing on the Wildfire BTB
 * debug setup through PE21/J5-29 and PE25/J5-30.
 */

#include <drivers/drv_adc.h>
#include <drivers/drv_hrt.h>
#include <nuttx/irq.h>
#include <px4_arch/adc.h>

#include <cerrno>
#include <cstdint>

#include "board.h"
#include "hpm_adc12_drv.h"

namespace
{

constexpr uint32_t ADC_TIMEOUT_US = 1000;

struct HpmAdcState {
	bool initialized{false};
	uint32_t configured_channels{0};
};

HpmAdcState g_adc_state[SYSTEM_ADC_COUNT];

HpmAdcState &state_for_base(uint32_t base_address)
{
#if SYSTEM_ADC_COUNT > 1
	if (base_address == HW_REV_VER_ADC_BASE && HW_REV_VER_ADC_BASE != SYSTEM_ADC_BASE) {
		return g_adc_state[1];
	}
#else
	(void)base_address;
#endif

	return g_adc_state[0];
}

ADC12_Type *adc_from_base(uint32_t base_address)
{
	return reinterpret_cast<ADC12_Type *>(base_address);
}

int hpm_configure_channel(ADC12_Type *adc, HpmAdcState &state, unsigned channel)
{
	if (state.configured_channels & (1u << channel)) {
		return OK;
	}

	adc12_channel_config_t channel_cfg;
	adc12_get_channel_default_config(&channel_cfg);
	channel_cfg.ch = channel;
	channel_cfg.sample_cycle = 20;

	if (adc12_init_channel(adc, &channel_cfg) != status_success) {
		return -EIO;
	}

	state.configured_channels |= (1u << channel);
	return OK;
}

} // namespace

int px4_arch_adc_init(uint32_t base_address)
{
	ADC12_Type *adc = adc_from_base(base_address);

	if (adc != HPM_ADC0) {
		return -ENODEV;
	}

	HpmAdcState &state = state_for_base(base_address);

	if (state.initialized) {
		return OK;
	}

	board_init_adc12_pins();
	board_init_adc_clock(adc, false);

	adc12_config_t config;
	adc12_get_default_config(&config);
	config.res = adc12_res_12_bits;
	config.conv_mode = adc12_conv_mode_oneshot;
	config.adc_clk_div = adc12_clock_divider_4;
	config.wait_dis = true;
	config.sel_sync_ahb = true;
	config.adc_ahb_en = false;
	config.diff_sel = adc12_sample_signal_single_ended;

	if (adc12_init(adc, &config) != status_success) {
		return -EIO;
	}

	adc12_set_nonblocking_read(adc);

	for (unsigned channel = 0; channel < 32; ++channel) {
		if ((ADC_CHANNELS | px4_arch_adc_temp_sensor_mask()) & (1u << channel)) {
			int ret = hpm_configure_channel(adc, state, channel);

			if (ret != OK) {
				return ret;
			}
		}
	}

	state.initialized = true;
	return OK;
}

void px4_arch_adc_uninit(uint32_t base_address)
{
	ADC12_Type *adc = adc_from_base(base_address);

	if (adc == HPM_ADC0) {
		adc12_deinit(adc);
		state_for_base(base_address) = {};
	}
}

uint32_t px4_arch_adc_sample(uint32_t base_address, unsigned channel)
{
	ADC12_Type *adc = adc_from_base(base_address);

	if (adc != HPM_ADC0) {
		return UINT32_MAX;
	}

	HpmAdcState &state = state_for_base(base_address);

	if (!state.initialized && px4_arch_adc_init(base_address) != OK) {
		return UINT32_MAX;
	}

	if (hpm_configure_channel(adc, state, channel) != OK) {
		return UINT32_MAX;
	}

	uint16_t result = 0;
	irqstate_t flags = px4_enter_critical_section();
	const hrt_abstime start = hrt_absolute_time();

	while (adc12_get_oneshot_result(adc, channel, &result) != status_success) {
		if ((hrt_absolute_time() - start) > ADC_TIMEOUT_US) {
			px4_leave_critical_section(flags);
			return UINT32_MAX;
		}
	}

	px4_leave_critical_section(flags);
	return result;
}

float px4_arch_adc_reference_v()
{
	return 3.3f;
}

uint32_t px4_arch_adc_temp_sensor_mask()
{
	return 0;
}

uint32_t px4_arch_adc_dn_fullcount()
{
	return 1u << 12;
}
