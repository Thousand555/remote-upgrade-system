#ifndef FIRMWARE_DOWNLOADER_H
#define FIRMWARE_DOWNLOADER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "gateway_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    GW_FW_DL_IDLE = 0,
    GW_FW_DL_WAIT_WIFI,
    GW_FW_DL_WAIT_TIME,
    GW_FW_DL_FETCH_MANIFEST,
    GW_FW_DL_VALIDATE_MANIFEST,
    GW_FW_DL_PREPARE,
    GW_FW_DL_DOWNLOAD,
    GW_FW_DL_VERIFY_PACKAGE,
    GW_FW_DL_COMMIT_PACKAGE,
    GW_FW_DL_READY,
    GW_FW_DL_FAILED,
    GW_FW_DL_CANCELED
} gateway_firmware_download_state_t;

typedef struct
{
    gateway_firmware_download_state_t state;
    esp_err_t last_error;
    uint32_t package_size;
    uint32_t received_size;
    bool can_resume;
    char firmware_id[GATEWAY_FIRMWARE_DOWNLOAD_ID_MAX_LENGTH + 1U];
} gateway_firmware_download_progress_t;

esp_err_t firmware_downloader_init(void);
esp_err_t firmware_downloader_start(const char *firmware_id);
esp_err_t firmware_downloader_cancel(void);
bool firmware_downloader_is_active(void);
esp_err_t firmware_downloader_get_progress(
    gateway_firmware_download_progress_t *progress);
const char *firmware_downloader_state_name(
    gateway_firmware_download_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_DOWNLOADER_H */
