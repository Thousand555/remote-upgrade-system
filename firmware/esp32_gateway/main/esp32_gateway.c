#include <inttypes.h>

#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "firmware_store.h"
#include "gateway_config.h"
#include "gateway_console.h"
#include "nvs_flash.h"
#include "transport_uart.h"
#include "upgrade_manager.h"

static const char *TAG = "gateway";

static esp_err_t init_nvs(void)
{
    esp_err_t status = nvs_flash_init();

    if ((status == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (status == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_LOGW(TAG, "NVS requires recovery erase: %s", esp_err_to_name(status));
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        status = nvs_flash_init();
    }

    return status;
}

static void log_hardware_info(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0U;
    size_t psram_size = 0U;

    esp_chip_info(&chip_info);
    ESP_LOGI(TAG,
             "ESP-IDF=%s, cores=%u, revision=%u",
             esp_get_idf_version(),
             (unsigned int)chip_info.cores,
             (unsigned int)chip_info.revision);

    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash=%" PRIu32 " bytes (%" PRIu32 " MiB)",
                 flash_size,
                 flash_size / (1024U * 1024U));
        if (flash_size != GATEWAY_EXPECTED_FLASH_SIZE_BYTES) {
            ESP_LOGW(TAG,
                     "Expected 16 MiB Flash, detected %" PRIu32 " bytes",
                     flash_size);
        }
    } else {
        ESP_LOGE(TAG, "Unable to query Flash size");
    }

    if (esp_psram_is_initialized()) {
        psram_size = esp_psram_get_size();
        ESP_LOGI(TAG, "PSRAM=%u bytes (%u MiB)",
                 (unsigned int)psram_size,
                 (unsigned int)(psram_size / (1024U * 1024U)));
        if (psram_size != GATEWAY_EXPECTED_PSRAM_SIZE_BYTES) {
            ESP_LOGW(TAG, "Expected 8 MiB PSRAM, detected %u bytes",
                     (unsigned int)psram_size);
        }
    } else {
        ESP_LOGE(TAG, "PSRAM is not initialized");
    }

    ESP_LOGI(TAG,
             "Free heap: internal=%u bytes, PSRAM=%u bytes",
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "Reset reason=%d", (int)esp_reset_reason());
}

void app_main(void)
{
    const esp_partition_t *firmware_partition;
    esp_err_t status;

    ESP_LOGI(TAG, "ESP32 gateway M7 initialization starting");
    log_hardware_info();

    status = init_nvs();
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(status));
        return;
    }
    ESP_LOGI(TAG, "NVS initialized");

    status = firmware_store_open();
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "stm_fw partition check failed: %s", esp_err_to_name(status));
        return;
    }

    firmware_partition = firmware_store_partition();
    ESP_LOGI(TAG,
             "stm_fw partition: address=0x%08" PRIX32 ", size=0x%08" PRIX32,
             firmware_partition->address,
             firmware_partition->size);

    if (transport_uart_is_configured()) {
        status = transport_uart_init();
        if (status != ESP_OK) {
            ESP_LOGE(TAG, "Upgrade UART initialization failed: %s",
                     esp_err_to_name(status));
            return;
        }
        ESP_LOGI(TAG, "Upgrade UART initialized");
    } else {
        ESP_LOGW(TAG,
                 "Upgrade UART is intentionally disabled; configure TX/RX GPIO after checking the board schematic");
    }

    status = upgrade_manager_init();
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "Upgrade manager initialization failed: %s",
                 esp_err_to_name(status));
        return;
    }

#if CONFIG_GATEWAY_CONSOLE_ENABLE
    status = gateway_console_init();
    if (status != ESP_OK) {
        ESP_LOGE(TAG, "Gateway console initialization failed: %s",
                 esp_err_to_name(status));
        return;
    }
#endif

    ESP_LOGI(TAG, "M7 gateway ready; upgrades require an explicit console command");
}
