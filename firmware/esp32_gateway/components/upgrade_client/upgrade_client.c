#include "upgrade_client.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gateway_config.h"
#include "gateway_log.h"
#include "transport_uart.h"

#if CONFIG_GATEWAY_RELIABILITY_TEST
#include "reliability_fault.h"
#endif

static const char *TAG = "upgrade_client";

static void upgrade_client_invalid_response_backoff(uint32_t attempt,
                                                    uint32_t attempts)
{
    if (attempt < attempts) {
        vTaskDelay(pdMS_TO_TICKS(GATEWAY_INVALID_RESPONSE_RETRY_DELAY_MS));
    }
}

static void upgrade_client_set_result(const upgrade_message_t *response,
                                      upgrade_client_response_t *result)
{
    if (result == NULL) {
        return;
    }
    result->status = (upgrade_status_t)response->flags_or_status;
    result->session_id = response->session_id;
    result->sequence = response->sequence;
    result->next_offset = response->offset_or_next_offset;
}

static esp_err_t upgrade_client_transact(upgrade_client_t *client,
                                         upgrade_subfunction_t subfunction,
                                         uint32_t session_id,
                                         uint32_t image_offset,
                                         const uint8_t *payload,
                                         uint16_t payload_length,
                                         uint32_t timeout_ms,
                                         uint32_t attempts,
                                         upgrade_client_response_t *result)
{
    protocol_status_t protocol_status;
    esp_err_t last_error;
    size_t request_length;
    size_t response_length;
    uint32_t attempt;
    uint8_t response_address;

    if ((client == NULL) || (attempts == 0U) || (timeout_ms == 0U) ||
        (payload_length > UPGRADE_MAX_PAYLOAD_SIZE) ||
        ((payload == NULL) && (payload_length != 0U))) {
        return ESP_ERR_INVALID_ARG;
    }

    upgrade_message_init(&client->request, subfunction);
    client->request.session_id = session_id;
    client->request.sequence = client->next_sequence;
    client->request.offset_or_next_offset = image_offset;
    client->request.payload_length = payload_length;
    if (payload_length > 0U) {
        memcpy(client->request.payload, payload, payload_length);
    }

    protocol_status = upgrade_encode_request(client->address,
                                             &client->request,
                                             client->request_adu,
                                             sizeof(client->request_adu),
                                             &request_length);
    if (protocol_status != PROTOCOL_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    last_error = ESP_ERR_TIMEOUT;
    for (attempt = 1U; attempt <= attempts; attempt++) {
#if CONFIG_GATEWAY_RELIABILITY_TEST
        if (reliability_fault_consume_timeout((uint8_t)subfunction)) {
            last_error = ESP_ERR_TIMEOUT;
            GW_LOGW(TAG,
                    "TEST: command 0x%02x attempt %lu/%lu timed out before transmit",
                    (unsigned int)subfunction,
                    (unsigned long)attempt,
                    (unsigned long)attempts);
            continue;
        }
#endif
        response_length = 0U;
        last_error = transport_uart_exchange(client->request_adu,
                                             request_length,
                                             client->response_adu,
                                             sizeof(client->response_adu),
                                             &response_length,
                                             timeout_ms);
        if (last_error != ESP_OK) {
            GW_LOGW(TAG,
                    "Command 0x%02x attempt %lu/%lu failed: %s",
                    (unsigned int)subfunction,
                    (unsigned long)attempt,
                    (unsigned long)attempts,
                    esp_err_to_name(last_error));
            continue;
        }

        protocol_status = upgrade_decode_response(client->response_adu,
                                                  response_length,
                                                  &response_address,
                                                  &client->response);
        if (protocol_status != PROTOCOL_OK) {
            last_error = ESP_ERR_INVALID_RESPONSE;
            GW_LOGW(TAG,
                    "Command 0x%02x attempt %lu/%lu decode failed: protocol=%u, length=%u",
                    (unsigned int)subfunction,
                    (unsigned long)attempt,
                    (unsigned long)attempts,
                    (unsigned int)protocol_status,
                    (unsigned int)response_length);
            upgrade_client_invalid_response_backoff(attempt, attempts);
            continue;
        }
        if ((response_address != client->address) ||
            (client->response.subfunction != subfunction) ||
            (client->response.sequence != client->request.sequence)) {
            last_error = ESP_ERR_INVALID_RESPONSE;
            GW_LOGW(TAG,
                    "Command 0x%02x attempt %lu/%lu response mismatch: "
                    "address=%u/%u, command=0x%02x, sequence=%lu/%lu",
                    (unsigned int)subfunction,
                    (unsigned long)attempt,
                    (unsigned long)attempts,
                    (unsigned int)response_address,
                    (unsigned int)client->address,
                    (unsigned int)client->response.subfunction,
                    (unsigned long)client->response.sequence,
                    (unsigned long)client->request.sequence);
            upgrade_client_invalid_response_backoff(attempt, attempts);
            continue;
        }

#if CONFIG_GATEWAY_RELIABILITY_TEST
        if ((subfunction == UPG_SUB_DATA) &&
            reliability_fault_consume(REL_FAULT_DROP_DATA_ACK)) {
            last_error = ESP_ERR_TIMEOUT;
            GW_LOGW(TAG,
                    "TEST: discarded one valid DATA response at offset %lu",
                    (unsigned long)image_offset);
            continue;
        }
        if ((subfunction == UPG_SUB_ACTIVATE) &&
            reliability_fault_consume(REL_FAULT_DROP_ACTIVATE_ACK)) {
            last_error = ESP_ERR_TIMEOUT;
            GW_LOGW(TAG, "TEST: discarded the valid ACTIVATE response");
            continue;
        }
#endif

        upgrade_client_set_result(&client->response, result);
        client->next_sequence++;
        return ESP_OK;
    }

    return last_error;
}

static esp_err_t upgrade_client_simple_request(upgrade_client_t *client,
                                               upgrade_subfunction_t subfunction,
                                               uint32_t session_id,
                                               upgrade_client_response_t *result)
{
    return upgrade_client_transact(client,
                                   subfunction,
                                   session_id,
                                   0U,
                                   NULL,
                                   0U,
                                   GATEWAY_NORMAL_REQUEST_TIMEOUT_MS,
                                   GATEWAY_MAX_RETRY_COUNT,
                                   result);
}

void upgrade_client_init(upgrade_client_t *client, uint8_t address)
{
    if (client == NULL) {
        return;
    }
    memset(client, 0, sizeof(*client));
    client->address = address;
}

esp_err_t upgrade_client_hello(upgrade_client_t *client,
                               uint32_t timeout_ms,
                               uint32_t attempts,
                               upgrade_hello_response_t *hello,
                               upgrade_client_response_t *result)
{
    esp_err_t status;

    if (hello == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    status = upgrade_client_transact(client,
                                     UPG_SUB_HELLO,
                                     0U,
                                     0U,
                                     NULL,
                                     0U,
                                     timeout_ms,
                                     attempts,
                                     result);
    if ((status == ESP_OK) &&
        (client->response.flags_or_status == UPG_STATUS_OK) &&
        (upgrade_hello_response_decode(client->response.payload,
                                       client->response.payload_length,
                                       hello) != PROTOCOL_OK)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return status;
}

esp_err_t upgrade_client_get_info(upgrade_client_t *client,
                                  upgrade_device_info_t *info,
                                  upgrade_client_response_t *result)
{
    esp_err_t status;

    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    status = upgrade_client_simple_request(client,
                                           UPG_SUB_GET_INFO,
                                           0U,
                                           result);
    if ((status == ESP_OK) &&
        (client->response.flags_or_status == UPG_STATUS_OK) &&
        (upgrade_device_info_decode(client->response.payload,
                                    client->response.payload_length,
                                    info) != PROTOCOL_OK)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return status;
}

esp_err_t upgrade_client_enter_boot(upgrade_client_t *client,
                                    uint32_t session_id,
                                    upgrade_client_response_t *result)
{
    return upgrade_client_simple_request(client,
                                         UPG_SUB_ENTER_BOOT,
                                         session_id,
                                         result);
}

esp_err_t upgrade_client_start(upgrade_client_t *client,
                               uint32_t session_id,
                               const upgrade_start_manifest_t *manifest,
                               upgrade_client_response_t *result)
{
    uint8_t payload[UPGRADE_START_MANIFEST_SIZE];

    if ((manifest == NULL) ||
        (upgrade_start_manifest_encode(manifest,
                                       payload,
                                       sizeof(payload)) != PROTOCOL_OK)) {
        return ESP_ERR_INVALID_ARG;
    }
    return upgrade_client_transact(client,
                                   UPG_SUB_START,
                                   session_id,
                                   0U,
                                   payload,
                                   sizeof(payload),
                                   GATEWAY_NORMAL_REQUEST_TIMEOUT_MS,
                                   GATEWAY_MAX_RETRY_COUNT,
                                   result);
}

esp_err_t upgrade_client_erase(upgrade_client_t *client,
                               uint32_t session_id,
                               upgrade_client_response_t *result)
{
    return upgrade_client_simple_request(client,
                                         UPG_SUB_ERASE,
                                         session_id,
                                         result);
}

esp_err_t upgrade_client_query_progress(upgrade_client_t *client,
                                        uint32_t timeout_ms,
                                        uint32_t attempts,
                                        upgrade_progress_t *progress,
                                        upgrade_client_response_t *result)
{
    esp_err_t status;

    if (progress == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    status = upgrade_client_transact(client,
                                     UPG_SUB_QUERY_PROGRESS,
                                     0U,
                                     0U,
                                     NULL,
                                     0U,
                                     timeout_ms,
                                     attempts,
                                     result);
    if ((status == ESP_OK) &&
        ((client->response.flags_or_status == UPG_STATUS_OK) ||
         (client->response.flags_or_status == UPG_STATUS_BUSY)) &&
        (upgrade_progress_decode(client->response.payload,
                                 client->response.payload_length,
                                 progress) != PROTOCOL_OK)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return status;
}

esp_err_t upgrade_client_send_data(upgrade_client_t *client,
                                   uint32_t session_id,
                                   uint32_t image_offset,
                                   const uint8_t *data,
                                   uint16_t length,
                                   upgrade_data_ack_t *ack,
                                   upgrade_client_response_t *result)
{
    esp_err_t status;

    if ((data == NULL) || (length == 0U) ||
        (length > UPGRADE_MAX_PAYLOAD_SIZE) || (ack == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    status = upgrade_client_transact(client,
                                     UPG_SUB_DATA,
                                     session_id,
                                     image_offset,
                                     data,
                                     length,
                                     GATEWAY_NORMAL_REQUEST_TIMEOUT_MS,
                                     GATEWAY_MAX_RETRY_COUNT,
                                     result);
    if ((status == ESP_OK) &&
        (upgrade_data_ack_decode(client->response.payload,
                                 client->response.payload_length,
                                 ack) != PROTOCOL_OK)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if ((status == ESP_OK) && (ack->accepted_sequence != client->request.sequence)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return status;
}

esp_err_t upgrade_client_verify(upgrade_client_t *client,
                                uint32_t session_id,
                                upgrade_client_response_t *result)
{
    return upgrade_client_simple_request(client,
                                         UPG_SUB_VERIFY,
                                         session_id,
                                         result);
}

esp_err_t upgrade_client_activate(upgrade_client_t *client,
                                  uint32_t session_id,
                                  upgrade_client_response_t *result)
{
    /*
     * ACTIVATE resets the target after sending its response.  If that response
     * is lost or truncated, retrying may send ACTIVATE to a rebooting target or
     * to the APP service.  The manager resolves this ambiguous outcome by
     * probing the APP after the Bootloader recovery window instead.
     */
    return upgrade_client_transact(client,
                                   UPG_SUB_ACTIVATE,
                                   session_id,
                                   0U,
                                   NULL,
                                   0U,
                                   GATEWAY_NORMAL_REQUEST_TIMEOUT_MS,
                                   1U,
                                   result);
}

esp_err_t upgrade_client_abort(upgrade_client_t *client,
                               uint32_t session_id,
                               upgrade_client_response_t *result)
{
    return upgrade_client_simple_request(client,
                                         UPG_SUB_ABORT,
                                         session_id,
                                         result);
}
