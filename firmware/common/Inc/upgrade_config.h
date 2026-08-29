#ifndef UPGRADE_CONFIG_H
#define UPGRADE_CONFIG_H

#include <stdint.h>

/*
 * M6 integration baseline. Product variants may override these macros from
 * the build target, but PC, APP and Bootloader must always use the same set.
 */
#ifndef UPGRADE_NODE_ADDRESS
#define UPGRADE_NODE_ADDRESS              1U
#endif

#ifndef UPGRADE_PRODUCT_ID
#define UPGRADE_PRODUCT_ID                0x0001U
#endif

#ifndef UPGRADE_HARDWARE_ID
#define UPGRADE_HARDWARE_ID               0x0001U
#endif

#ifndef UPGRADE_BOOTLOADER_VERSION
#define UPGRADE_BOOTLOADER_VERSION        0x00010000UL
#endif

#ifndef UPGRADE_APPLICATION_VERSION
#define UPGRADE_APPLICATION_VERSION       1UL
#endif

#define UPGRADE_BOOT_WAIT_MS              500UL
#define UPGRADE_UART_TX_TIMEOUT_MS        1000UL
#define UPGRADE_METADATA_CHECKPOINT_SIZE  4096UL

#define UPGRADE_CAP_BOOTLOADER             0x0001U
#define UPGRADE_CAP_ENTER_BOOT             0x0002U
#define UPGRADE_CAP_RESUME                 0x0004U
#define UPGRADE_CAP_CRC32                  0x0008U

#define UPGRADE_BOOT_CAPABILITIES          \
    (UPGRADE_CAP_BOOTLOADER | UPGRADE_CAP_RESUME | UPGRADE_CAP_CRC32)
#define UPGRADE_APP_CAPABILITIES           UPGRADE_CAP_ENTER_BOOT

#endif /* UPGRADE_CONFIG_H */
