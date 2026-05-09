#ifndef HW_CONFIG_H_
#define HW_CONFIG_H_

#include "board_config.h"

/* Boot device selection list */
#define USB0_DEV       0x01

/*
 * Temporary direct-boot layout for JTAG/OpenOCD bring-up.
 * App is linked from flash base so the chip can boot without a PX4 bootloader.
 */
#define APP_LOAD_ADDRESS               0x80000000
#define APP_VECTOR_OFFSET              0x1014
#define BOOT_DELAY_ADDRESS             0x1020
#define BOOTLOADER_DELAY               5000
#define INTERFACE_USB                  1
#define INTERFACE_USB_CONFIG           "/dev/ttyACM0"
#define INTERFACE_USART                0
#define BOARD_TYPE                     6700
#define BOARD_FLASH_SIZE               (4 * 1024 * 1024)
#define BOARD_FLASH_BASE_ADDRESS       0x80000000UL

#define BOARD_APP_XPI_NOR_XPI_BASE     HPM_XPI0
#define BOARD_APP_XPI_NOR_CFG_OPT_HDR  0xfcf90002U
#define BOARD_APP_XPI_NOR_CFG_OPT_OPT0 0x00000007U
#define BOARD_APP_XPI_NOR_CFG_OPT_OPT1 0x0000000EU

#define BOARD_PIN_LED_ACTIVITY         GPIO_LED_SAFETY
#define BOARD_PIN_LED_BOOTLOADER       GPIO_LED_SAFETY
#define BOARD_LED_ON                   0
#define BOARD_LED_OFF                  1

#if !defined(ARCH_SN_MAX_LENGTH)
# define ARCH_SN_MAX_LENGTH 12
#endif

#if !defined(APP_RESERVATION_SIZE)
# define APP_RESERVATION_SIZE 0
#endif

#if !defined(USB_DATA_ALIGN)
# define USB_DATA_ALIGN
#endif

#ifndef BOOT_DEVICES_SELECTION
# define BOOT_DEVICES_SELECTION USB0_DEV
#endif

#ifndef BOOT_DEVICES_FILTER_ONUSB
# define BOOT_DEVICES_FILTER_ONUSB USB0_DEV
#endif

#endif /* HW_CONFIG_H_ */
