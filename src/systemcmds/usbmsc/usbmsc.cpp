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
 * @file usbmsc.cpp
 *
 * Simple USB Mass Storage control command. This switches the USB device
 * function from CDC ACM to MSC and exports the TF/microSD card to the host.
 */

#include <px4_platform_common/module.h>
#include <px4_platform_common/log.h>
#include <px4_platform_common/px4_config.h>
#include <px4_platform_common/shutdown.h>

#include <board_config.h>
#include <nuttx/usb/usbmsc.h>
#include <sys/boardctl.h>

#include <parameters/param.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>

extern "C" __EXPORT int logger_main(int argc, char *argv[]);
extern "C" __EXPORT int dataman_main(int argc, char *argv[]);
extern "C" __EXPORT int cdcacm_autostart_main(int argc, char *argv[]);
extern "C" __EXPORT int mavlink_main(int argc, char *argv[]);
extern "C" int serdis_main(int argc, char *argv[]);

#ifndef CONFIG_SYSTEM_USBMSC_DEVPATH1
# define CONFIG_SYSTEM_USBMSC_DEVPATH1 "/dev/mmcsd0"
#endif

#ifndef CONFIG_SYSTEM_USBMSC_NLUNS
# define CONFIG_SYSTEM_USBMSC_NLUNS 1
#endif

namespace
{

constexpr const char *USB_DEVICE_PATH{"/dev/ttyACM0"};
constexpr const char *MICROSD_MOUNT_PATH{"/fs/microsd"};
static void *g_msc_handle{nullptr};
static bool g_connected{false};

void print_usage()
{
	PRINT_MODULE_DESCRIPTION("Switch USB device mode to Mass Storage and export the TF/microSD card.");
	PRINT_MODULE_USAGE_NAME("usbmsc", "command");
	PRINT_MODULE_USAGE_COMMAND_DESCR("start", "Stop USB CDC and SD users, then expose the TF/microSD card as a USB drive");
	PRINT_MODULE_USAGE_COMMAND_DESCR("stop", "Stop USB mass storage mode and reboot to restore normal USB serial mode");
	PRINT_MODULE_USAGE_COMMAND_DESCR("status", "Show current USB mass storage status");
}

void stop_usb_services()
{
	PX4_INFO("stopping USB CDC services");

	char *cdcacm_argv[] = {const_cast<char *>("cdcacm_autostart"), const_cast<char *>("stop"), nullptr};
	(void)cdcacm_autostart_main(2, cdcacm_argv);
	px4_usleep(50 * 1000);

	char *mavlink_argv[] = {const_cast<char *>("mavlink"), const_cast<char *>("stop"),
				const_cast<char *>("-d"), const_cast<char *>(USB_DEVICE_PATH), nullptr};
	(void)mavlink_main(4, mavlink_argv);
	px4_usleep(50 * 1000);

	char *serdis_argv[] = {const_cast<char *>("serdis"), nullptr};
	(void)serdis_main(1, serdis_argv);
	px4_usleep(300 * 1000);

#ifdef BOARD_HAS_USB_CONNECT_TOGGLE
	board_usb_set_connected(false);
	px4_usleep(1000 * 1000);
#endif
}

void stop_sd_services()
{
	PX4_INFO("stopping SD card users");

	char *logger_argv[] = {const_cast<char *>("logger"), const_cast<char *>("stop"), nullptr};
	(void)logger_main(2, logger_argv);
	px4_usleep(300 * 1000);

	char *dataman_argv[] = {const_cast<char *>("dataman"), const_cast<char *>("stop"), nullptr};
	(void)dataman_main(2, dataman_argv);
	px4_usleep(200 * 1000);
}

int unmount_sd()
{
	int ret = umount(MICROSD_MOUNT_PATH);

	if (ret == 0) {
		PX4_INFO("unmounted %s", MICROSD_MOUNT_PATH);
		return 0;
	}

	if (errno == EINVAL || errno == ENOENT) {
		PX4_INFO("%s not mounted", MICROSD_MOUNT_PATH);
		return 0;
	}

	if (errno == EBUSY) {
		PX4_WARN("umount %s busy, forcing unmount", MICROSD_MOUNT_PATH);
		ret = umount2(MICROSD_MOUNT_PATH, MNT_FORCE);

		if (ret == 0) {
			PX4_INFO("forced unmount of %s succeeded", MICROSD_MOUNT_PATH);
			return 0;
		}

		PX4_ERR("forced umount %s failed (%d)", MICROSD_MOUNT_PATH, errno);
		return -1;
	}

	PX4_ERR("umount %s failed (%d)", MICROSD_MOUNT_PATH, errno);
	return -1;
}

int start_usbmsc()
{
	if (g_connected) {
		PX4_WARN("USB MSC already running");
		return 0;
	}

	stop_usb_services();
	stop_sd_services();
	param_control_autosave(false);

	if (unmount_sd() != 0) {
		return -1;
	}

	int ret = usbmsc_configure(CONFIG_SYSTEM_USBMSC_NLUNS, &g_msc_handle);

	if (ret < 0) {
		PX4_ERR("usbmsc_configure failed: %d", ret);
		g_msc_handle = nullptr;
		return -1;
	}

	ret = usbmsc_bindlun(g_msc_handle, CONFIG_SYSTEM_USBMSC_DEVPATH1, 0, 0, 0, false);

	if (ret < 0) {
		PX4_ERR("usbmsc_bindlun failed: %d", ret);
		usbmsc_uninitialize(g_msc_handle);
		g_msc_handle = nullptr;
		return -1;
	}

	ret = usbmsc_exportluns(g_msc_handle);

	if (ret < 0) {
		PX4_ERR("usbmsc_exportluns failed: %d", ret);
		usbmsc_uninitialize(g_msc_handle);
		g_msc_handle = nullptr;
		return -1;
	}

	g_connected = true;

#ifdef BOARD_HAS_USB_CONNECT_TOGGLE
	board_usb_set_connected(false);
	px4_usleep(300 * 1000);
	board_usb_set_connected(true);
	px4_usleep(500 * 1000);
#endif

	PX4_INFO("USB mass storage started");
	PX4_INFO("replug USB if the host does not enumerate immediately");
	return 0;
}

int stop_usbmsc()
{
	if (!g_connected || g_msc_handle == nullptr) {
		PX4_WARN("USB MSC not running");
		return 0;
	}

	PX4_INFO("stopping USB mass storage and rebooting");
	g_connected = false;
	px4_usleep(200 * 1000);

	int ret = px4_reboot_request(REBOOT_REQUEST);

	if (ret < 0) {
		PX4_ERR("reboot failed (%d)", ret);
		return -1;
	}

	while (true) {
		px4_usleep(1000);
	}
}

void status_usbmsc()
{
	PX4_INFO("USB MSC: %s", g_connected ? "running" : "stopped");
	PX4_INFO("device: %s", CONFIG_SYSTEM_USBMSC_DEVPATH1);
}

} // namespace

extern "C" __EXPORT int usbmsc_main(int argc, char *argv[])
{
	if (argc < 2) {
		print_usage();
		return 1;
	}

	if (!strcmp(argv[1], "start")) {
		return start_usbmsc();

	} else if (!strcmp(argv[1], "stop")) {
		return stop_usbmsc();

	} else if (!strcmp(argv[1], "status")) {
		status_usbmsc();
		return 0;
	}

	print_usage();
	return 1;
}
