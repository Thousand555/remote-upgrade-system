#include "upgrade_manager.h"

#include <stdbool.h>
#include <string.h>

#include "esp_random.h"
#include "esp_timer.h"
#include "firmware_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gateway_config.h"
#include "gateway_log.h"
#include "upgrade_client.h"

#if CONFIG_GATEWAY_RELIABILITY_TEST
#include "reliability_fault.h"
#endif

static const char *TAG = "upgrade_manager";
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static bool s_probe_active;
static volatile bool s_abort_requested;
static gateway_upgrade_progress_t s_progress;
static upgrade_client_t s_client;

static void upgrade_manager_lock(void)
{
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void upgrade_manager_unlock(void)
{
    (void)xSemaphoreGive(s_lock);
}

static void upgrade_manager_set_state(gateway_upgrade_state_t state)
{
    upgrade_manager_lock();
    s_progress.state = state;
    upgrade_manager_unlock();
    GW_LOGI(TAG, "State -> %s", upgrade_manager_state_name(state));
}

static void upgrade_manager_set_remote(const upgrade_client_response_t *response,
                                       const upgrade_progress_t *remote)
{
    upgrade_manager_lock();
    if (response != NULL) {
        s_progress.last_device_status = response->status;
    }
    if (remote != NULL) {
        s_progress.remote_boot_state = remote->boot_state;
        s_progress.transferred_bytes = remote->received_bytes;
    }
    upgrade_manager_unlock();
}

static uint32_t upgrade_manager_random_session(void)
{
    uint32_t session_id = esp_random();
    return (session_id == 0U) ? 1U : session_id;
}

static bool upgrade_manager_is_aborted(void)
{
    return s_abort_requested;
}

static esp_err_t upgrade_manager_require_status(
    const upgrade_client_response_t *response,
    upgrade_status_t first,
    upgrade_status_t second)
{
    upgrade_manager_set_remote(response, NULL);
    if ((response->status != first) && (response->status != second)) {
        GW_LOGE(TAG, "STM32 returned upgrade status %u", (unsigned int)response->status);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t upgrade_manager_find_bootloader(void)
{
    upgrade_hello_response_t hello;
    upgrade_client_response_t response;
    int64_t deadline;
    esp_err_t status;
    bool enter_requested;
    uint32_t enter_session;

    deadline = esp_timer_get_time() +
               ((int64_t)GATEWAY_DISCOVERY_TIMEOUT_MS * 1000LL);
    enter_requested = false;
    enter_session = upgrade_manager_random_session();

    while (esp_timer_get_time() < deadline) {
        if (upgrade_manager_is_aborted()) {
            return ESP_ERR_INVALID_STATE;
        }

        status = upgrade_client_hello(&s_client,
                                      GATEWAY_DISCOVERY_REQUEST_TIMEOUT_MS,
                                      1U,
                                      &hello,
                                      &response);
        if ((status == ESP_OK) && (response.status == UPG_STATUS_OK)) {
            if (hello.max_payload_size != UPGRADE_MAX_PAYLOAD_SIZE) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            if ((hello.capabilities & GATEWAY_UPGRADE_CAP_BOOTLOADER) != 0U) {
                GW_LOGI(TAG,
                        "STM32 Bootloader connected, version=0x%08lx",
                        (unsigned long)hello.service_version);
                return ESP_OK;
            }
            if ((hello.capabilities & GATEWAY_UPGRADE_CAP_ENTER_BOOT) != 0U) {
                if (!enter_requested) {
                    upgrade_manager_set_state(GW_UPG_ENTER_BOOT);
                    status = upgrade_client_enter_boot(&s_client,
                                                       enter_session,
                                                       &response);
                    if (status != ESP_OK) {
                        return status;
                    }
                    status = upgrade_manager_require_status(&response,
                                                            UPG_STATUS_OK,
                                                            UPG_STATUS_OK);
                    if (status != ESP_OK) {
                        return status;
                    }
                    enter_requested = true;
                    upgrade_manager_set_state(GW_UPG_DISCOVER);
                    vTaskDelay(pdMS_TO_TICKS(300U));
                }
            } else {
                return ESP_ERR_NOT_SUPPORTED;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20U));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t upgrade_manager_wait_for_application(uint32_t firmware_version)
{
    upgrade_hello_response_t hello;
    upgrade_device_info_t info;
    upgrade_client_response_t response;
    int64_t deadline;
    esp_err_t status;

    deadline = esp_timer_get_time() +
               ((int64_t)GATEWAY_DISCOVERY_TIMEOUT_MS * 1000LL);
    while (esp_timer_get_time() < deadline) {
        status = upgrade_client_hello(&s_client,
                                      GATEWAY_DISCOVERY_REQUEST_TIMEOUT_MS,
                                      1U,
                                      &hello,
                                      &response);
        if ((status == ESP_OK) && (response.status == UPG_STATUS_OK) &&
            ((hello.capabilities & GATEWAY_UPGRADE_CAP_BOOTLOADER) == 0U) &&
            ((hello.capabilities & GATEWAY_UPGRADE_CAP_ENTER_BOOT) != 0U)) {
            status = upgrade_client_get_info(&s_client, &info, &response);
            if ((status != ESP_OK) || (response.status != UPG_STATUS_OK)) {
                return (status == ESP_OK) ? ESP_ERR_INVALID_RESPONSE : status;
            }
            if ((info.product_id != GATEWAY_STM32_PRODUCT_ID) ||
                (info.hardware_id != GATEWAY_STM32_HARDWARE_ID) ||
                (info.application_version != firmware_version)) {
                return ESP_ERR_INVALID_VERSION;
            }
            GW_LOGI(TAG,
                    "STM32 APP connected, version=%lu",
                    (unsigned long)info.application_version);
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t upgrade_manager_query(upgrade_progress_t *remote,
                                       upgrade_client_response_t *response,
                                       uint32_t attempts)
{
    esp_err_t status;

    status = upgrade_client_query_progress(&s_client,
                                           GATEWAY_NORMAL_REQUEST_TIMEOUT_MS,
                                           attempts,
                                           remote,
                                           response);
    if (status != ESP_OK) {
        return status;
    }
    status = upgrade_manager_require_status(response,
                                            UPG_STATUS_OK,
                                            UPG_STATUS_BUSY);
    if (status == ESP_OK) {
        upgrade_manager_set_remote(response, remote);
    }
    return status;
}

static bool upgrade_manager_remote_is_active(uint16_t state)
{
    return (state == GATEWAY_STM32_BOOT_STATE_ERASING) ||
           (state == GATEWAY_STM32_BOOT_STATE_RECEIVING) ||
           (state == GATEWAY_STM32_BOOT_STATE_VERIFYING) ||
           (state == GATEWAY_STM32_BOOT_STATE_PENDING_BOOT);
}

static esp_err_t upgrade_manager_poll_erase(upgrade_progress_t *remote,
                                            upgrade_client_response_t *response)
{
    int64_t deadline;
    esp_err_t status;

    deadline = esp_timer_get_time() +
               ((int64_t)GATEWAY_ERASE_TOTAL_TIMEOUT_MS * 1000LL);
    while (esp_timer_get_time() < deadline) {
        if (upgrade_manager_is_aborted()) {
            return ESP_ERR_INVALID_STATE;
        }
        status = upgrade_manager_query(remote, response, 1U);
        if (status == ESP_OK) {
            if (remote->boot_state == GATEWAY_STM32_BOOT_STATE_RECEIVING) {
                return ESP_OK;
            }
            if (remote->boot_state == GATEWAY_STM32_BOOT_STATE_FAILED) {
                return ESP_FAIL;
            }
        } else if (status != ESP_ERR_TIMEOUT) {
            return status;
        }
        vTaskDelay(pdMS_TO_TICKS(GATEWAY_ERASE_POLL_INTERVAL_MS));
    }
    return ESP_ERR_TIMEOUT;
}

static uint32_t upgrade_manager_next_chunk_end(uint32_t offset,
                                               uint32_t image_size)
{
    uint32_t end = offset + UPGRADE_MAX_PAYLOAD_SIZE;
    uint32_t checkpoint_end =
        ((offset / GATEWAY_STM32_CHECKPOINT_SIZE) + 1U) *
        GATEWAY_STM32_CHECKPOINT_SIZE;

    if (end > checkpoint_end) {
        end = checkpoint_end;
    }
    if (end > image_size) {
        end = image_size;
    }
    return end;
}

static esp_err_t upgrade_manager_transfer(uint32_t session_id,
                                          uint32_t image_size,
                                          uint32_t start_offset)
{
    uint8_t chunk[UPGRADE_MAX_PAYLOAD_SIZE];
    upgrade_data_ack_t ack;
    upgrade_client_response_t response;
    uint32_t offset;
    uint32_t end;
    uint16_t length;
    esp_err_t status;

    offset = start_offset;
    while (offset < image_size) {
        if (upgrade_manager_is_aborted()) {
            return ESP_ERR_INVALID_STATE;
        }

        end = upgrade_manager_next_chunk_end(offset, image_size);
        length = (uint16_t)(end - offset);
        status = firmware_store_read(offset, chunk, length);
        if (status != ESP_OK) {
            return status;
        }

#if CONFIG_GATEWAY_RELIABILITY_TEST
        if (reliability_fault_consume(REL_FAULT_GAP_OFFSET)) {
            uint32_t gap_offset = end;

            if ((gap_offset + length) > image_size) {
                GW_LOGE(TAG, "TEST: image is too short for a gap-offset injection");
                return ESP_ERR_INVALID_SIZE;
            }
            status = upgrade_client_send_data(&s_client,
                                              session_id,
                                              gap_offset,
                                              chunk,
                                              length,
                                              &ack,
                                              &response);
            if (status != ESP_OK) {
                return status;
            }
            upgrade_manager_set_remote(&response, NULL);
            if ((response.status != UPG_STATUS_BAD_OFFSET) ||
                (ack.status != UPG_STATUS_BAD_OFFSET) ||
                (ack.next_expected_offset != offset)) {
                GW_LOGE(TAG,
                        "TEST: gap offset was not rejected as expected, status=%u, next=%lu",
                        (unsigned int)response.status,
                        (unsigned long)ack.next_expected_offset);
                return ESP_ERR_INVALID_RESPONSE;
            }
            GW_LOGW(TAG,
                    "TEST: gap offset %lu rejected; device requested %lu",
                    (unsigned long)gap_offset,
                    (unsigned long)ack.next_expected_offset);
        }
#endif

        status = upgrade_client_send_data(&s_client,
                                          session_id,
                                          offset,
                                          chunk,
                                          length,
                                          &ack,
                                          &response);
        if (status != ESP_OK) {
            return status;
        }
        upgrade_manager_set_remote(&response, NULL);
        if ((response.status != ack.status) ||
            (ack.next_expected_offset > image_size)) {
            return ESP_ERR_INVALID_RESPONSE;
        }

#if CONFIG_GATEWAY_RELIABILITY_TEST
        if ((response.status == UPG_STATUS_OK) &&
            reliability_fault_consume(REL_FAULT_DUPLICATE_DATA)) {
            uint32_t accepted_offset = ack.next_expected_offset;

            status = upgrade_client_send_data(&s_client,
                                              session_id,
                                              offset,
                                              chunk,
                                              length,
                                              &ack,
                                              &response);
            if (status != ESP_OK) {
                return status;
            }
            upgrade_manager_set_remote(&response, NULL);
            if ((response.status != UPG_STATUS_OK) ||
                (ack.status != UPG_STATUS_OK) ||
                (ack.next_expected_offset != accepted_offset)) {
                GW_LOGE(TAG,
                        "TEST: duplicate DATA changed progress, status=%u, next=%lu/%lu",
                        (unsigned int)response.status,
                        (unsigned long)ack.next_expected_offset,
                        (unsigned long)accepted_offset);
                return ESP_ERR_INVALID_RESPONSE;
            }
            GW_LOGW(TAG,
                    "TEST: duplicate DATA at offset %lu accepted without progress change",
                    (unsigned long)offset);
        }
#endif

        if (response.status == UPG_STATUS_OK) {
            if (ack.next_expected_offset <= offset) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            offset = ack.next_expected_offset;
        } else if (response.status == UPG_STATUS_BAD_OFFSET) {
            if (ack.next_expected_offset == offset) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            GW_LOGW(TAG,
                    "STM32 requested offset resynchronization: %lu -> %lu",
                    (unsigned long)offset,
                    (unsigned long)ack.next_expected_offset);
            offset = ack.next_expected_offset;
        } else {
            return ESP_ERR_INVALID_RESPONSE;
        }

        upgrade_manager_lock();
        s_progress.transferred_bytes = offset;
        upgrade_manager_unlock();
        if ((offset == image_size) ||
            ((offset % GATEWAY_STM32_CHECKPOINT_SIZE) == 0U)) {
            GW_LOGI(TAG,
                    "Transferred %lu/%lu bytes (%lu%%)",
                    (unsigned long)offset,
                    (unsigned long)image_size,
                    (unsigned long)((offset * 100U) / image_size));
        }
    }
    return ESP_OK;
}

static void upgrade_manager_finish(esp_err_t status,
                                   upgrade_status_t device_status)
{
    upgrade_manager_lock();
    s_progress.last_error = status;
    s_progress.last_device_status = device_status;
    s_progress.state = (status == ESP_OK) ? GW_UPG_SUCCESS : GW_UPG_FAILED;
    s_task = NULL;
    upgrade_manager_unlock();

    if (status == ESP_OK) {
        GW_LOGI(TAG, "M7 local UART upgrade completed successfully");
    } else {
        GW_LOGE(TAG,
                "M7 upgrade failed: %s, device_status=%u",
                esp_err_to_name(status),
                (unsigned int)device_status);
    }
}

static void upgrade_manager_task(void *argument)
{
    const gateway_firmware_manifest_t *local;
    upgrade_start_manifest_t manifest;
    upgrade_device_info_t info;
    upgrade_progress_t remote;
    upgrade_client_response_t response;
    uint32_t session_id;
    uint32_t index;
    bool resume_session;
    bool verified;
    esp_err_t status;

    (void)argument;
    status = firmware_store_validate();
    if (status != ESP_OK) {
        goto finished;
    }
    local = firmware_store_manifest();
    if (local == NULL) {
        status = ESP_ERR_INVALID_STATE;
        goto finished;
    }

    upgrade_manager_lock();
    s_progress.firmware_version = local->firmware_version;
    s_progress.image_size = local->image_size;
    s_progress.transferred_bytes = 0U;
    upgrade_manager_unlock();

    memset(&manifest, 0, sizeof(manifest));
    manifest.firmware_version = local->firmware_version;
    manifest.image_size = local->image_size;
    manifest.image_crc32 = local->image_crc32;
    manifest.product_id = local->product_id;
    manifest.hardware_id = local->hardware_id;
    for (index = 0U; index < UPGRADE_SHA256_SIZE; index++) {
        manifest.image_sha256[index] = local->image_sha256[index];
    }

#if CONFIG_GATEWAY_RELIABILITY_TEST
    if (reliability_fault_consume(REL_FAULT_BAD_MANIFEST_CRC)) {
        manifest.image_crc32 ^= 1U;
        GW_LOGW(TAG,
                "TEST: START manifest CRC32 overridden to 0x%08lx",
                (unsigned long)manifest.image_crc32);
    }
#endif

    upgrade_manager_set_state(GW_UPG_DISCOVER);
    status = upgrade_manager_find_bootloader();
    if (status != ESP_OK) {
        goto finished;
    }

    upgrade_manager_set_state(GW_UPG_GET_INFO);
    status = upgrade_client_get_info(&s_client, &info, &response);
    if (status != ESP_OK) {
        goto finished;
    }
    status = upgrade_manager_require_status(&response,
                                            UPG_STATUS_OK,
                                            UPG_STATUS_OK);
    if (status != ESP_OK) {
        goto finished;
    }
    if ((info.product_id != local->product_id) ||
        (info.hardware_id != local->hardware_id) ||
        (info.application_max_size < local->image_size) ||
        ((info.capabilities & GATEWAY_UPGRADE_CAP_CRC32) == 0U)) {
        status = ESP_ERR_NOT_SUPPORTED;
        goto finished;
    }

    status = upgrade_manager_query(&remote,
                                   &response,
                                   GATEWAY_MAX_RETRY_COUNT);
    if (status != ESP_OK) {
        goto finished;
    }
    resume_session = upgrade_manager_remote_is_active(remote.boot_state) &&
                     (response.session_id != 0U);
    if (resume_session && (remote.image_size != local->image_size)) {
        GW_LOGE(TAG,
                "STM32 has an active session for a different image size (%lu)",
                (unsigned long)remote.image_size);
        status = ESP_ERR_INVALID_STATE;
        goto finished;
    }
    session_id = resume_session ? response.session_id :
                                  upgrade_manager_random_session();

    upgrade_manager_lock();
    s_progress.session_id = session_id;
    upgrade_manager_unlock();

    upgrade_manager_set_state(GW_UPG_START);
    status = upgrade_client_start(&s_client,
                                  session_id,
                                  &manifest,
                                  &response);
    if (status != ESP_OK) {
        goto finished;
    }
    upgrade_manager_set_remote(&response, NULL);
    if ((response.status != UPG_STATUS_OK) &&
        !((response.status == UPG_STATUS_BUSY) && resume_session &&
          ((remote.boot_state == GATEWAY_STM32_BOOT_STATE_ERASING) ||
           (remote.boot_state == GATEWAY_STM32_BOOT_STATE_VERIFYING)))) {
        status = ESP_ERR_INVALID_RESPONSE;
        goto finished;
    }

    status = upgrade_manager_query(&remote,
                                   &response,
                                   GATEWAY_MAX_RETRY_COUNT);
    if (status != ESP_OK) {
        goto finished;
    }
    if ((response.session_id != session_id) ||
        (remote.image_size != local->image_size)) {
        status = ESP_ERR_INVALID_STATE;
        goto finished;
    }

    verified = (remote.boot_state == GATEWAY_STM32_BOOT_STATE_PENDING_BOOT);
    if (!verified &&
        (remote.boot_state != GATEWAY_STM32_BOOT_STATE_VERIFYING)) {
        upgrade_manager_set_state(GW_UPG_ERASE);
        status = upgrade_client_erase(&s_client, session_id, &response);
        if (status != ESP_OK) {
            goto finished;
        }
        status = upgrade_manager_require_status(&response,
                                                UPG_STATUS_OK,
                                                UPG_STATUS_BUSY);
        if (status != ESP_OK) {
            goto finished;
        }
        status = upgrade_manager_poll_erase(&remote, &response);
        if (status != ESP_OK) {
            goto finished;
        }

        if ((remote.image_size != local->image_size) ||
            (remote.received_bytes > local->image_size)) {
            status = ESP_ERR_INVALID_SIZE;
            goto finished;
        }
        upgrade_manager_set_state(GW_UPG_TRANSFER);
        status = upgrade_manager_transfer(session_id,
                                          local->image_size,
                                          remote.received_bytes);
        if (status != ESP_OK) {
            goto finished;
        }
    }

    if (!verified) {
        upgrade_manager_set_state(GW_UPG_VERIFY);
        status = upgrade_client_verify(&s_client, session_id, &response);
        if (status == ESP_OK) {
            upgrade_manager_set_remote(&response, NULL);
        }
        if ((status != ESP_OK) && (status != ESP_ERR_TIMEOUT)) {
            goto finished;
        }
        if ((status == ESP_OK) &&
            (response.status != UPG_STATUS_OK) &&
            (response.status != UPG_STATUS_BUSY)) {
            status = ESP_ERR_INVALID_CRC;
            goto finished;
        }
        status = upgrade_manager_query(&remote,
                                       &response,
                                       GATEWAY_MAX_RETRY_COUNT);
        if (status != ESP_OK) {
            goto finished;
        }
        if (remote.boot_state != GATEWAY_STM32_BOOT_STATE_PENDING_BOOT) {
            status = ESP_ERR_INVALID_CRC;
            goto finished;
        }
    }

    upgrade_manager_set_state(GW_UPG_ACTIVATE);
    status = upgrade_client_activate(&s_client, session_id, &response);
    if (status == ESP_OK) {
        status = upgrade_manager_require_status(&response,
                                                UPG_STATUS_OK,
                                                UPG_STATUS_OK);
        if (status != ESP_OK) {
            goto finished;
        }
    } else if ((status == ESP_ERR_TIMEOUT) ||
               (status == ESP_ERR_INVALID_RESPONSE)) {
        GW_LOGW(TAG,
                "ACTIVATE response was ambiguous (%s); probing APP because STM32 may already be resetting",
                esp_err_to_name(status));
    } else {
        goto finished;
    }

    upgrade_manager_set_state(GW_UPG_WAIT_APP);
    /*
     * The STM32 Bootloader exposes a 500 ms recovery window after reset and
     * remains in protocol mode when it sees an addressed frame in that window.
     * Keep UART silent long enough for a valid image to jump to the APP before
     * sending the first discovery HELLO.
     */
    vTaskDelay(pdMS_TO_TICKS(GATEWAY_ACTIVATE_APP_SILENCE_MS));
    status = upgrade_manager_wait_for_application(local->firmware_version);

finished:
    if ((status != ESP_OK) && upgrade_manager_is_aborted() &&
        (s_progress.session_id != 0U)) {
        (void)upgrade_client_abort(&s_client,
                                   s_progress.session_id,
                                   &response);
    }
    upgrade_manager_finish(status,
                           (status == ESP_OK) ? UPG_STATUS_OK :
                                               s_progress.last_device_status);
    vTaskDelete(NULL);
}

esp_err_t upgrade_manager_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    memset(&s_progress, 0, sizeof(s_progress));
    s_progress.state = GW_UPG_IDLE;
    s_progress.last_error = ESP_OK;
    s_progress.last_device_status = UPG_STATUS_OK;
    s_abort_requested = false;
    s_task = NULL;
    s_probe_active = false;
    upgrade_client_init(&s_client, GATEWAY_STM32_NODE_ADDRESS);
    GW_LOGI(TAG, "Initialized in IDLE state");
    return ESP_OK;
}

esp_err_t upgrade_manager_probe(gateway_upgrade_probe_t *probe)
{
    upgrade_hello_response_t hello;
    upgrade_device_info_t info;
    upgrade_client_response_t response;
    esp_err_t status;

    if (probe == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    upgrade_manager_lock();
    if ((s_task != NULL) || s_probe_active) {
        upgrade_manager_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_probe_active = true;
    upgrade_manager_unlock();

    upgrade_client_init(&s_client, GATEWAY_STM32_NODE_ADDRESS);
    status = upgrade_client_hello(&s_client,
                                  GATEWAY_NORMAL_REQUEST_TIMEOUT_MS,
                                  GATEWAY_MAX_RETRY_COUNT,
                                  &hello,
                                  &response);
    if ((status == ESP_OK) && (response.status != UPG_STATUS_OK)) {
        status = ESP_ERR_INVALID_RESPONSE;
    }
    if ((status == ESP_OK) &&
        (hello.max_payload_size != UPGRADE_MAX_PAYLOAD_SIZE)) {
        status = ESP_ERR_NOT_SUPPORTED;
    }
    if (status == ESP_OK) {
        status = upgrade_client_get_info(&s_client, &info, &response);
    }
    if ((status == ESP_OK) && (response.status != UPG_STATUS_OK)) {
        status = ESP_ERR_INVALID_RESPONSE;
    }

    if (status == ESP_OK) {
        probe->capabilities = hello.capabilities;
        probe->max_payload_size = hello.max_payload_size;
        probe->service_version = hello.service_version;
        probe->product_id = info.product_id;
        probe->hardware_id = info.hardware_id;
        probe->bootloader_version = info.bootloader_version;
        probe->application_version = info.application_version;
        probe->application_base = info.application_base;
        probe->application_max_size = info.application_max_size;
        probe->boot_state = info.boot_state;
    }

    upgrade_manager_lock();
    s_probe_active = false;
    upgrade_manager_unlock();
    return status;
}

esp_err_t upgrade_manager_start(void)
{
    BaseType_t task_created;

    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!firmware_store_is_ready()) {
        GW_LOGI(TAG, "Revalidating local firmware package before start");
    }

    upgrade_manager_lock();
    if ((s_task != NULL) || s_probe_active) {
        upgrade_manager_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_progress, 0, sizeof(s_progress));
    s_progress.state = GW_UPG_VALIDATE_IMAGE;
    s_progress.last_error = ESP_OK;
    s_progress.last_device_status = UPG_STATUS_OK;
    s_abort_requested = false;
    upgrade_manager_unlock();

    upgrade_client_init(&s_client, GATEWAY_STM32_NODE_ADDRESS);
    task_created = xTaskCreate(upgrade_manager_task,
                               "gateway_upgrade",
                               GATEWAY_TRANSFER_TASK_STACK_SIZE,
                               NULL,
                               GATEWAY_TRANSFER_TASK_PRIORITY,
                               &s_task);
    if (task_created != pdPASS) {
        upgrade_manager_finish(ESP_ERR_NO_MEM, UPG_STATUS_OK);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t upgrade_manager_abort(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    upgrade_manager_lock();
    if (s_task == NULL) {
        upgrade_manager_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_abort_requested = true;
    upgrade_manager_unlock();
    return ESP_OK;
}

gateway_upgrade_state_t upgrade_manager_state(void)
{
    gateway_upgrade_state_t state;

    if (s_lock == NULL) {
        return GW_UPG_IDLE;
    }
    upgrade_manager_lock();
    state = s_progress.state;
    upgrade_manager_unlock();
    return state;
}

esp_err_t upgrade_manager_get_progress(gateway_upgrade_progress_t *progress)
{
    if (progress == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    upgrade_manager_lock();
    *progress = s_progress;
    upgrade_manager_unlock();
    return ESP_OK;
}

const char *upgrade_manager_state_name(gateway_upgrade_state_t state)
{
    switch (state) {
        case GW_UPG_IDLE: return "IDLE";
        case GW_UPG_VALIDATE_IMAGE: return "VALIDATE_IMAGE";
        case GW_UPG_DISCOVER: return "DISCOVER";
        case GW_UPG_ENTER_BOOT: return "ENTER_BOOT";
        case GW_UPG_GET_INFO: return "GET_INFO";
        case GW_UPG_START: return "START";
        case GW_UPG_ERASE: return "ERASE";
        case GW_UPG_TRANSFER: return "TRANSFER";
        case GW_UPG_VERIFY: return "VERIFY";
        case GW_UPG_ACTIVATE: return "ACTIVATE";
        case GW_UPG_WAIT_APP: return "WAIT_APP";
        case GW_UPG_SUCCESS: return "SUCCESS";
        case GW_UPG_FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}
