/****************************************************************************
 *
 *   Copyright (C) 2020 PX4 Development Team. All rights reserved.
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

#include <px4_arch/i2c_hw_description.h>
#include <drivers/drv_hrt.h>

#include <stdio.h>

#include "hpm_soc.h"
#include "hpm_gpio_drv.h"
#include "hpm_i2c_drv.h"

#if defined(CONFIG_I2C)

static void hpm_i2c3_log_gpio_state(const char *tag)
{
	HPM_IOC->PAD[IOC_PAD_PF10].FUNC_CTL = IOC_PF10_FUNC_CTL_GPIO_F_10;
	HPM_IOC->PAD[IOC_PAD_PY05].FUNC_CTL = IOC_PY05_FUNC_CTL_GPIO_Y_05;
	HPM_PIOC->PAD[IOC_PAD_PY05].FUNC_CTL = PIOC_PY05_FUNC_CTL_SOC_PY_05;

	gpio_set_pin_input(HPM_GPIO0, GPIO_DI_GPIOF, 10);
	gpio_set_pin_input(HPM_GPIO0, GPIO_DI_GPIOY, 5);

	printf("[board_i2c] I2C3 %s: gpio SCL=%u SDA=%u\n", tag,
	       gpio_read_pin(HPM_GPIO0, GPIO_DI_GPIOF, 10),
	       gpio_read_pin(HPM_GPIO0, GPIO_DI_GPIOY, 5));
}

/****************************************************************************
 * Name: board_i2cbus_clear
 *
 * Description:
 *   Clear an I2C bus by performing a recovery sequence. Toggles the clock
 *   line up to 9 times while monitoring the SDA line to recover from a stuck
 *   I2C slave device.
 *
 * Input Parameters:
 *   gpio_sda  - GPIO pin for SDA (data line)
 *   gpio_clk  - GPIO pin for SCL (clock line)
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/
void board_i2cbus_clear(uint32_t gpio_sda, uint32_t gpio_clk)
{
    px4_arch_configgpio(gpio_sda);
    px4_arch_configgpio(gpio_clk);

    /* Release both lines high before and after the recovery pulses.
     * On HPM open-drain GPIO, leaving the output latch at 0 can keep the
     * bus pinned low after switching back to the I2C alternate function.
     */
    px4_arch_gpiowrite(gpio_sda, 1);
    px4_arch_gpiowrite(gpio_clk, 1);

    for (uint32_t j = 0; j < 5000; j++) {
        __asm__ volatile("nop");
    }

    for (uint32_t i = 0; i < 9; i++) {
        px4_arch_gpiowrite(gpio_clk, 1);
        for (uint32_t j = 0; j < 5000; j++) {
            __asm__ volatile("nop");
        }
        px4_arch_gpiowrite(gpio_clk, 0);
        for (uint32_t j = 0; j < 5000; j++) {
            __asm__ volatile("nop");
        }
    }

    px4_arch_gpiowrite(gpio_sda, 1);
    px4_arch_gpiowrite(gpio_clk, 1);

    for (uint32_t j = 0; j < 5000; j++) {
        __asm__ volatile("nop");
    }

    px4_arch_unconfiggpio(gpio_sda);
    px4_arch_unconfiggpio(gpio_clk);
}

/****************************************************************************
 * Name: hpm_i2cbus_pins_init
 *
 * Description:
 *   Initialize the selected I2C port pins
 *
 * Input Parameters:
 *   Port number (for hardware that has multiple CAN interfaces)
 *
 * Returned Value:
 *   Zero on success; a negated errno on failure
 *
 ****************************************************************************/
int hpm_i2cbus_pins_init(int port)
{
	switch(port){
		case 0:
#if defined(CONFIG_HPM_I2C0)
			board_i2cbus_clear(GPIO_I2C0_SDA_GPIO, GPIO_I2C0_SCL_GPIO);
			px4_arch_configgpio(GPIO_I2C0_SCL);
			px4_arch_configgpio(GPIO_I2C0_SDA);
#endif
			break;
		case 1:
#if defined(CONFIG_HPM_I2C1)
			board_i2cbus_clear(GPIO_I2C1_SDA_GPIO, GPIO_I2C1_SCL_GPIO);
			px4_arch_configgpio(GPIO_I2C1_SCL);
			px4_arch_configgpio(GPIO_I2C1_SDA);
#endif
			break;
		case 2:
#if defined(CONFIG_HPM_I2C2)
			board_i2cbus_clear(GPIO_I2C2_SDA_GPIO, GPIO_I2C2_SCL_GPIO);
			px4_arch_configgpio(GPIO_I2C2_SCL);
			px4_arch_configgpio(GPIO_I2C2_SDA);
#endif
			break;
		case 3:
#if defined(CONFIG_HPM_I2C3)
			hpm_i2c3_log_gpio_state("before-pinmux");

			/* Match the standalone HPM6750 I2C3 sensor probe exactly: PF10 is
			 * SCL, PY05 is SDA, and PY05 must be routed from the PY power
			 * domain to the SoC domain through PIOC.
			 */
			HPM_IOC->PAD[IOC_PAD_PF10].FUNC_CTL = IOC_PF10_FUNC_CTL_I2C3_SCL
							      | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
			HPM_IOC->PAD[IOC_PAD_PF10].PAD_CTL = IOC_PAD_PAD_CTL_OD_SET(1);

			HPM_IOC->PAD[IOC_PAD_PY05].FUNC_CTL = IOC_PY05_FUNC_CTL_I2C3_SDA
							      | IOC_PAD_FUNC_CTL_LOOP_BACK_MASK;
			HPM_PIOC->PAD[IOC_PAD_PY05].FUNC_CTL = PIOC_PY05_FUNC_CTL_SOC_PY_05;
			HPM_IOC->PAD[IOC_PAD_PY05].PAD_CTL = IOC_PAD_PAD_CTL_OD_SET(1);

			printf("[board_i2c] I2C3 after-pinmux: line SCL=%u SDA=%u\n",
			       i2c_get_line_scl_status(HPM_I2C3),
			       i2c_get_line_sda_status(HPM_I2C3));
#endif
			break;
	}

 return 0;
}

constexpr px4_i2c_bus_t px4_i2c_buses[I2C_BUS_MAX_BUS_ITEMS] = {
	initI2CBusExternal(PX4_BUS_NUMBER_TO_PX4(3)),
	initI2CBusInternal(PX4_BUS_NUMBER_TO_PX4(0)),
};


#endif
