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

#include <px4_platform_common/px4_config.h>
#include <px4_defines.h>

#include "hw_config.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <nuttx/board.h>

#include <lib/flash_cache.h>

#include "bl.h"
#include "riscv_internal.h"

#include "hpm_bgpr_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_l1c_drv.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_romapi.h"
#include "hpm_soc_ip.h"
#include "hpm_usb_drv.h"
#include "hpm_usbdev.h"

#define BOOTLOADER_RESERVATION_SIZE (256 * 1024U)
#define APP_SIZE_MAX (BOARD_FLASH_SIZE - BOOTLOADER_RESERVATION_SIZE)

#define HPM_BOOTLOADER_BGPR_INDEX 0U
#define HPM_BOOTLOADER_SIGNATURE  0xb007b007U

#if INTERFACE_USB
# define BOARD_INTERFACE_CONFIG_USB INTERFACE_USB_CONFIG
#endif

struct boardinfo board_info = {
	.board_type = BOARD_TYPE,
	.board_rev = 0,
	.fw_size = 0,
	.systick_mhz = 24,
};

static bool usb_connected;
static bool flash_initialized;
static uint32_t flash_sector_size_bytes = 4096U;
static xpi_nor_config_t g_nor_config;

static const xpi_nor_config_option_t g_nor_option = {
	.header.U = BOARD_APP_XPI_NOR_CFG_OPT_HDR,
	.option0.U = BOARD_APP_XPI_NOR_CFG_OPT_OPT0,
	.option1.U = BOARD_APP_XPI_NOR_CFG_OPT_OPT1,
};

extern void board_init_clock(void);
extern void board_ungate_mchtmr_at_lp_mode(void);

static uint32_t board_get_reset_signature(void)
{
	uint32_t sig = 0;
	bgpr_read32(HPM_BGPR, HPM_BOOTLOADER_BGPR_INDEX, &sig);
	return sig;
}

static void board_set_reset_signature(uint32_t sig)
{
	bgpr_write32(HPM_BGPR, HPM_BOOTLOADER_BGPR_INDEX, sig);
}

static bool board_test_force_pin(void)
{
#if defined(BOARD_FORCE_BL_PIN_IN) && defined(BOARD_FORCE_BL_PIN_OUT)
	volatile unsigned samples = 0;
	volatile unsigned vote = 0;

	for (volatile unsigned cycles = 0; cycles < 10; cycles++) {
		px4_arch_gpiowrite(BOARD_FORCE_BL_PIN_OUT, 1);

		for (unsigned count = 0; count < 20; count++) {
			if (px4_arch_gpioread(BOARD_FORCE_BL_PIN_IN) != 0) {
				vote++;
			}

			samples++;
		}

		px4_arch_gpiowrite(BOARD_FORCE_BL_PIN_OUT, 0);

		for (unsigned count = 0; count < 20; count++) {
			if (px4_arch_gpioread(BOARD_FORCE_BL_PIN_IN) == 0) {
				vote++;
			}

			samples++;
		}
	}

	return ((vote * 100U) > (samples * 90U));
#elif defined(BOARD_FORCE_BL_PIN)
	volatile unsigned samples = 0;
	volatile unsigned vote = 0;

	for (samples = 0; samples < 200; samples++) {
		if ((px4_arch_gpioread(BOARD_FORCE_BL_PIN) ? 1 : 0) == BOARD_FORCE_BL_STATE) {
			vote++;
		}
	}

	return ((vote * 100U) > (samples * 90U));
#else
	return false;
#endif
}

uint32_t board_get_devices(void)
{
	uint32_t devices = BOOT_DEVICES_SELECTION;

	if (usb_connected) {
		devices &= BOOT_DEVICES_FILTER_ONUSB;
	}

	return devices;
}

static int hpm_boot_flash_init(void)
{
	if (flash_initialized) {
		return 0;
	}

	if (rom_xpi_nor_auto_config(BOARD_APP_XPI_NOR_XPI_BASE, &g_nor_config,
				    (xpi_nor_config_option_t *)&g_nor_option) != status_success) {
		return -1;
	}

	uint32_t sector_size = 0;

	if (rom_xpi_nor_get_property(BOARD_APP_XPI_NOR_XPI_BASE, &g_nor_config,
				     xpi_nor_property_sector_size, &sector_size) == status_success &&
	    sector_size > 0) {
		flash_sector_size_bytes = sector_size;
	}

	flash_initialized = true;
	return 0;
}

static void board_init(void)
{
	board_info.fw_size = APP_SIZE_MAX;
	board_info.systick_mhz = clock_get_frequency(clock_mchtmr0) / 1000000U;

#if defined(BOARD_PIN_LED_ACTIVITY)
	px4_arch_configgpio(BOARD_PIN_LED_ACTIVITY);
#endif
#if defined(BOARD_PIN_LED_BOOTLOADER)
	px4_arch_configgpio(BOARD_PIN_LED_BOOTLOADER);
#endif
#if defined(BOARD_FORCE_BL_PIN_IN) && defined(BOARD_FORCE_BL_PIN_OUT)
	px4_arch_configgpio(BOARD_FORCE_BL_PIN_IN);
	px4_arch_configgpio(BOARD_FORCE_BL_PIN_OUT);
#endif
#if defined(BOARD_FORCE_BL_PIN)
	px4_arch_configgpio(BOARD_FORCE_BL_PIN);
#endif
}

void board_deinit(void)
{
}

static inline void clock_init(void)
{
	board_init_clock();
	board_ungate_mchtmr_at_lp_mode();
}

void clock_deinit(void)
{
}

void arch_flash_lock(void)
{
}

void arch_flash_unlock(void)
{
	if (hpm_boot_flash_init() == 0) {
		fc_reset();
	}
}

static void invalidate_flash_region(uintptr_t address, size_t size)
{
	uint32_t aligned_start = HPM_L1C_CACHELINE_ALIGN_DOWN((uint32_t)address);
	uint32_t aligned_end = HPM_L1C_CACHELINE_ALIGN_UP((uint32_t)(address + size));

	l1c_dc_invalidate(aligned_start, aligned_end - aligned_start);
}

ssize_t arch_flash_write(uintptr_t address, const void *buffer, size_t buflen)
{
	if (hpm_boot_flash_init() != 0) {
		return 0;
	}

	uint32_t offset = (uint32_t)address - BOARD_FLASH_BASE_ADDRESS;

	irqstate_t flags = enter_critical_section();
	hpm_stat_t status = rom_xpi_nor_program(BOARD_APP_XPI_NOR_XPI_BASE, xpi_xfer_channel_auto, &g_nor_config,
						(const uint32_t *)buffer, offset, buflen);
	leave_critical_section(flags);

	if (status == status_success) {
		invalidate_flash_region(address, buflen);
		return buflen;
	}

	return 0;
}

void arch_setvtor(const uint32_t *address)
{
	(void)address;
}

uint32_t flash_func_sector_size(unsigned sector)
{
	if (hpm_boot_flash_init() != 0) {
		return 0;
	}

	return ((sector * flash_sector_size_bytes) < board_info.fw_size) ? flash_sector_size_bytes : 0;
}

void flash_func_erase_sector(unsigned sector, bool force)
{
	if (hpm_boot_flash_init() != 0) {
		return;
	}

	uint32_t sector_size = flash_func_sector_size(sector);

	if (sector_size == 0) {
		return;
	}

	uintptr_t address = APP_LOAD_ADDRESS + (sector * sector_size);

	if (!force) {
		bool blank = true;
		const uint32_t *p = (const uint32_t *)address;

		for (uint32_t i = 0; i < sector_size / sizeof(uint32_t); i++) {
			if (p[i] != 0xffffffffU) {
				blank = false;
				break;
			}
		}

		if (blank) {
			return;
		}
	}

	irqstate_t flags = enter_critical_section();
	(void)rom_xpi_nor_erase_sector(BOARD_APP_XPI_NOR_XPI_BASE, xpi_xfer_channel_auto, &g_nor_config,
				       (uint32_t)address - BOARD_FLASH_BASE_ADDRESS);
	leave_critical_section(flags);
	invalidate_flash_region(address, sector_size);
}

void flash_func_write_word(uintptr_t address, uint32_t word)
{
	fc_write(address + APP_LOAD_ADDRESS, word);
}

uint32_t flash_func_read_word(uintptr_t address)
{
	if ((address & 3U) != 0) {
		return 0;
	}

	return fc_read(address + APP_LOAD_ADDRESS);
}

uint32_t flash_func_read_otp(uintptr_t address)
{
	(void)address;
	return 0;
}

uint32_t flash_func_read_sn(uintptr_t address)
{
	(void)address;
	return 0;
}

uint32_t get_mcu_id(void)
{
	return 0x6750U;
}

int get_mcu_desc(int max, uint8_t *revstr)
{
	static const char chip[] = "HPM6750";
	int len = 0;

	while (len < (max - 1) && chip[len] != '\0') {
		revstr[len] = chip[len];
		len++;
	}

	return len;
}

int check_silicon(void)
{
	return 0;
}

void led_on(unsigned led)
{
	switch (led) {
	case LED_ACTIVITY:
#if defined(BOARD_PIN_LED_ACTIVITY)
		px4_arch_gpiowrite(BOARD_PIN_LED_ACTIVITY, BOARD_LED_ON);
#endif
		break;

	case LED_BOOTLOADER:
#if defined(BOARD_PIN_LED_BOOTLOADER)
		px4_arch_gpiowrite(BOARD_PIN_LED_BOOTLOADER, BOARD_LED_ON);
#endif
		break;
	}
}

void led_off(unsigned led)
{
	switch (led) {
	case LED_ACTIVITY:
#if defined(BOARD_PIN_LED_ACTIVITY)
		px4_arch_gpiowrite(BOARD_PIN_LED_ACTIVITY, BOARD_LED_OFF);
#endif
		break;

	case LED_BOOTLOADER:
#if defined(BOARD_PIN_LED_BOOTLOADER)
		px4_arch_gpiowrite(BOARD_PIN_LED_BOOTLOADER, BOARD_LED_OFF);
#endif
		break;
	}
}

void led_toggle(unsigned led)
{
	switch (led) {
	case LED_ACTIVITY:
#if defined(BOARD_PIN_LED_ACTIVITY)
		px4_arch_gpiowrite(BOARD_PIN_LED_ACTIVITY, px4_arch_gpioread(BOARD_PIN_LED_ACTIVITY) ^ 1);
#endif
		break;

	case LED_BOOTLOADER:
#if defined(BOARD_PIN_LED_BOOTLOADER)
		px4_arch_gpiowrite(BOARD_PIN_LED_BOOTLOADER, px4_arch_gpioread(BOARD_PIN_LED_BOOTLOADER) ^ 1);
#endif
		break;
	}
}

void arch_do_jump(const uint32_t *app_base)
{
	uintptr_t entrypoint = app_base[APP_VECTOR_OFFSET_WORDS + 1];

	__asm volatile("fence.i");
	__asm volatile("jr %0" : : "r"(entrypoint));

	for (;;) {
	}
}

int bootloader_main(void)
{
	bool try_boot = true;
	unsigned timeout = BOOTLOADER_DELAY;

	board_init();
	clock_init();

	if (board_get_reset_signature() == HPM_BOOTLOADER_SIGNATURE) {
		try_boot = false;
		timeout = 0;
		board_set_reset_signature(0);
	}

	if (board_test_force_pin()) {
		try_boot = false;
	}

#if INTERFACE_USB
	if (board_read_VBUS_state() == PX4_OK) {
		usb_connected = true;
		try_boot = false;
	}
#endif

	if (try_boot) {
		jump_to_app();
		timeout = 0;
	}

#if INTERFACE_USB
	hpm_usbdev_initialize(CONFIG_HPM_USBDEV_INSTANCE);
	cinit(BOARD_INTERFACE_CONFIG_USB, USB);
#endif

	while (1) {
		bootloader(timeout);

		if (board_test_force_pin()) {
			continue;
		}

		jump_to_app();
		timeout = 0;
	}
}
