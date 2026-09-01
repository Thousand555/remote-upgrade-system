#ifndef UPGRADE_CLIENT_H
#define UPGRADE_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "modbus_rtu.h"
#include "upgrade_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    upgrade_status_t status;
    uint32_t session_id;
    uint32_t sequence;
    uint32_t next_offset;
} upgrade_client_response_t;

typedef struct
{
    uint8_t address;
    uint32_t next_sequence;
    uint8_t request_adu[MODBUS_RTU_MAX_ADU_SIZE];
    uint8_t response_adu[MODBUS_RTU_MAX_ADU_SIZE];
    upgrade_message_t request;
    upgrade_message_t response;
} upgrade_client_t;

void upgrade_client_init(upgrade_client_t *client, uint8_t address);

esp_err_t upgrade_client_hello(upgrade_client_t *client,
                               uint32_t timeout_ms,
                               uint32_t attempts,
                               upgrade_hello_response_t *hello,
                               upgrade_client_response_t *result);
esp_err_t upgrade_client_get_info(upgrade_client_t *client,
                                  upgrade_device_info_t *info,
                                  upgrade_client_response_t *result);
esp_err_t upgrade_client_enter_boot(upgrade_client_t *client,
                                    uint32_t session_id,
                                    upgrade_client_response_t *result);
esp_err_t upgrade_client_start(upgrade_client_t *client,
                               uint32_t session_id,
                               const upgrade_start_manifest_t *manifest,
                               upgrade_client_response_t *result);
esp_err_t upgrade_client_erase(upgrade_client_t *client,
                               uint32_t session_id,
                               upgrade_client_response_t *result);
esp_err_t upgrade_client_query_progress(upgrade_client_t *client,
                                        uint32_t timeout_ms,
                                        uint32_t attempts,
                                        upgrade_progress_t *progress,
                                        upgrade_client_response_t *result);
esp_err_t upgrade_client_send_data(upgrade_client_t *client,
                                   uint32_t session_id,
                                   uint32_t image_offset,
                                   const uint8_t *data,
                                   uint16_t length,
                                   upgrade_data_ack_t *ack,
                                   upgrade_client_response_t *result);
esp_err_t upgrade_client_verify(upgrade_client_t *client,
                                uint32_t session_id,
                                upgrade_client_response_t *result);
esp_err_t upgrade_client_activate(upgrade_client_t *client,
                                  uint32_t session_id,
                                  upgrade_client_response_t *result);
esp_err_t upgrade_client_abort(upgrade_client_t *client,
                               uint32_t session_id,
                               upgrade_client_response_t *result);

#ifdef __cplusplus
}
#endif

#endif /* UPGRADE_CLIENT_H */
