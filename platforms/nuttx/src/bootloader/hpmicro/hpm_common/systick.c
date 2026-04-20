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

#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include "riscv_internal.h"
#include "hpm_clock_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_soc_ip.h"

extern void sys_tick_handler(void);

static uint64_t g_tick_interval_cycles;

static int hpm_bootloader_tick_isr(int irq, void *context, void *arg)
{
	(void)irq;
	(void)context;
	(void)arg;

	mchtmr_set_compare_value(HPM_MCHTMR, mchtmr_get_count(HPM_MCHTMR) + g_tick_interval_cycles);
	sys_tick_handler();
	return 0;
}

void arch_systic_init(void)
{
	uint32_t timer_clock_hz = clock_get_frequency(clock_mchtmr0);

	g_tick_interval_cycles = (timer_clock_hz > 0) ? (timer_clock_hz / 1000ULL) : 24000ULL;

	if (g_tick_interval_cycles == 0) {
		g_tick_interval_cycles = 1;
	}

	irq_attach(RISCV_IRQ_MTIMER, hpm_bootloader_tick_isr, NULL);
	up_enable_irq(RISCV_IRQ_MTIMER);
	mchtmr_set_compare_value(HPM_MCHTMR, mchtmr_get_count(HPM_MCHTMR) + g_tick_interval_cycles);
}

void arch_systic_deinit(void)
{
	up_disable_irq(RISCV_IRQ_MTIMER);
}
