#ifndef UPGRADE_MANAGER_H
#define UPGRADE_MANAGER_H

#include <stdint.h>

#include "esp_err.h"
#include "upgrade_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    GW_UPG_IDLE = 0,
    GW_UPG_VALIDATE_IMAGE,
    GW_UPG_DISCOVER,
    GW_UPG_ENTER_BOOT,
    GW_UPG_GET_INFO,
    GW_UPG_START,
    GW_UPG_ERASE,
    GW_UPG_TRANSFER,
    GW_UPG_VERIFY,
    GW_UPG_ACTIVATE,
    GW_UPG_WAIT_APP,
    GW_UPG_SUCCESS,
    GW_UPG_FAILED
} gateway_upgrade_state_t;

typedef struct
{
    gateway_upgrade_state_t state;
    esp_err_t last_error;
    upgrade_status_t last_device_status;
    uint32_t session_id;
    uint32_t firmware_version;
    uint32_t image_size;
    uint32_t transferred_bytes;
    uint16_t remote_boot_state;
} gateway_upgrade_progress_t;

typedef struct
{
    uint16_t capabilities;
    uint16_t max_payload_size;
    uint32_t service_version;
    uint16_t product_id;
    uint16_t hardware_id;
    uint32_t bootloader_version;
    uint32_t application_version;
    uint32_t application_base;
    uint32_t application_max_size;
    uint16_t boot_state;
} gateway_upgrade_probe_t;

esp_err_t upgrade_manager_init(void);
esp_err_t upgrade_manager_probe(gateway_upgrade_probe_t *probe);
esp_err_t upgrade_manager_start(void);
esp_err_t upgrade_manager_abort(void);
gateway_upgrade_state_t upgrade_manager_state(void);
esp_err_t upgrade_manager_get_progress(gateway_upgrade_progress_t *progress);
const char *upgrade_manager_state_name(gateway_upgrade_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* UPGRADE_MANAGER_H */
