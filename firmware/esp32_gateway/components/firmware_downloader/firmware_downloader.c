#include "firmware_downloader.h"

#include <ctype.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_http_client.h"
#include "firmware_store.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "gateway_log.h"
#include "gateway_wifi.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

#define GATEWAY_DOWNLOAD_NVS_NAMESPACE       "fw_download"
#define GATEWAY_DOWNLOAD_NVS_KEY             "checkpoint"
#define GATEWAY_DOWNLOAD_RECORD_MAGIC        0x314C4447UL
#define GATEWAY_DOWNLOAD_RECORD_SCHEMA        1U
#define GATEWAY_DOWNLOAD_MANIFEST_MAX_SIZE   4096U
#define GATEWAY_DOWNLOAD_URL_MAX_LENGTH      256U
#define GATEWAY_DOWNLOAD_RANGE_MAX_LENGTH    64U
#define GATEWAY_DOWNLOAD_RESPONSE_VALUE_MAX  128U

typedef struct
{
    uint32_t magic;
    uint32_t schema_version;
    uint32_t package_size;
    uint32_t received_size;
    uint8_t package_sha256[GATEWAY_FIRMWARE_SHA256_SIZE];
    char firmware_id[GATEWAY_FIRMWARE_DOWNLOAD_ID_MAX_LENGTH + 1U];
    char etag[GATEWAY_FIRMWARE_DOWNLOAD_ETAG_MAX_LENGTH + 1U];
} gateway_download_record_t;

typedef struct
{
    uint32_t firmware_version_code;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t package_size;
    uint8_t image_sha256[GATEWAY_FIRMWARE_SHA256_SIZE];
    uint8_t package_sha256[GATEWAY_FIRMWARE_SHA256_SIZE];
    char firmware_id[GATEWAY_FIRMWARE_DOWNLOAD_ID_MAX_LENGTH + 1U];
    char download_path[GATEWAY_DOWNLOAD_URL_MAX_LENGTH];
    char etag[GATEWAY_FIRMWARE_DOWNLOAD_ETAG_MAX_LENGTH + 1U];
} gateway_remote_manifest_t;

typedef struct
{
    char etag[GATEWAY_FIRMWARE_DOWNLOAD_ETAG_MAX_LENGTH + 1U];
    char content_range[GATEWAY_DOWNLOAD_RESPONSE_VALUE_MAX];
} gateway_http_metadata_t;

static const char *TAG = "firmware_downloader";
extern const uint8_t m10_ca_pem_start[] asm("_binary_m10_ca_pem_start");
extern const uint8_t m10_ca_pem_end[] asm("_binary_m10_ca_pem_end");
static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static bool s_cancel_requested;
static gateway_firmware_download_progress_t s_progress;
static char s_requested_firmware_id[GATEWAY_FIRMWARE_DOWNLOAD_ID_MAX_LENGTH + 1U];

static void firmware_downloader_lock(void)
{
    (void)xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void firmware_downloader_unlock(void)
{
    (void)xSemaphoreGive(s_lock);
}

static bool firmware_downloader_is_safe_id(const char *firmware_id)
{
    const unsigned char *cursor = (const unsigned char *)firmware_id;
    size_t length = 0U;

    if (firmware_id == NULL) {
        return false;
    }
    while (*cursor != '\0') {
        if ((!isalnum(*cursor)) && (*cursor != '-') && (*cursor != '_') &&
            (*cursor != '.')) {
            return false;
        }
        length++;
        if (length > GATEWAY_FIRMWARE_DOWNLOAD_ID_MAX_LENGTH) {
            return false;
        }
        cursor++;
    }
    return (length != 0U);
}

static bool firmware_downloader_is_http_server_configured(void)
{
    char server_url[GATEWAY_FIRMWARE_SERVER_URL_MAX_LENGTH];

    return gateway_wifi_get_server_url(server_url, sizeof(server_url)) == ESP_OK;
}

static bool firmware_downloader_url_is_https(const char *url)
{
    return (url != NULL) && (strncmp(url, "https://", 8U) == 0);
}

static void firmware_downloader_set_state(gateway_firmware_download_state_t state)
{
    firmware_downloader_lock();
    s_progress.state = state;
    firmware_downloader_unlock();
}

static void firmware_downloader_set_received(uint32_t received_size)
{
    firmware_downloader_lock();
    s_progress.received_size = received_size;
    firmware_downloader_unlock();
}

static void firmware_downloader_set_resume(bool can_resume)
{
    firmware_downloader_lock();
    s_progress.can_resume = can_resume;
    firmware_downloader_unlock();
}

static bool firmware_downloader_cancel_requested(void)
{
    bool requested;

    firmware_downloader_lock();
    requested = s_cancel_requested;
    firmware_downloader_unlock();
    return requested;
}

static esp_err_t firmware_downloader_load_checkpoint(gateway_download_record_t *record)
{
    nvs_handle_t handle;
    size_t length = sizeof(*record);
    esp_err_t status;

    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    status = nvs_open(GATEWAY_DOWNLOAD_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (status != ESP_OK) {
        return status;
    }
    status = nvs_get_blob(handle, GATEWAY_DOWNLOAD_NVS_KEY, record, &length);
    nvs_close(handle);
    if (status != ESP_OK) {
        return status;
    }
    if ((length != sizeof(*record)) ||
        (record->magic != GATEWAY_DOWNLOAD_RECORD_MAGIC) ||
        (record->schema_version != GATEWAY_DOWNLOAD_RECORD_SCHEMA) ||
        !firmware_downloader_is_safe_id(record->firmware_id) ||
        (record->package_size < GATEWAY_FIRMWARE_IMAGE_OFFSET) ||
        (record->received_size > record->package_size) ||
        (record->etag[0] == '\0')) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

static esp_err_t firmware_downloader_save_checkpoint(
    const gateway_download_record_t *record)
{
    nvs_handle_t handle;
    esp_err_t status;

    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    status = nvs_open(GATEWAY_DOWNLOAD_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    status = nvs_set_blob(handle, GATEWAY_DOWNLOAD_NVS_KEY, record, sizeof(*record));
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    return status;
}

static esp_err_t firmware_downloader_clear_checkpoint(void)
{
    nvs_handle_t handle;
    esp_err_t status;

    status = nvs_open(GATEWAY_DOWNLOAD_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (status != ESP_OK) {
        return status;
    }
    status = nvs_erase_key(handle, GATEWAY_DOWNLOAD_NVS_KEY);
    if (status == ESP_ERR_NVS_NOT_FOUND) {
        status = ESP_OK;
    }
    if (status == ESP_OK) {
        status = nvs_commit(handle);
    }
    nvs_close(handle);
    return status;
}

static esp_err_t firmware_downloader_http_event(esp_http_client_event_t *event)
{
    gateway_http_metadata_t *metadata =
        (gateway_http_metadata_t *)event->user_data;

    if ((event->event_id != HTTP_EVENT_ON_HEADER) || (metadata == NULL) ||
        (event->header_key == NULL) || (event->header_value == NULL)) {
        return ESP_OK;
    }
    if (strcasecmp(event->header_key, "ETag") == 0) {
        if (strlen(event->header_value) >= sizeof(metadata->etag)) {
            return ESP_ERR_INVALID_SIZE;
        }
        strcpy(metadata->etag, event->header_value);
    } else if (strcasecmp(event->header_key, "Content-Range") == 0) {
        if (strlen(event->header_value) >= sizeof(metadata->content_range)) {
            return ESP_ERR_INVALID_SIZE;
        }
        strcpy(metadata->content_range, event->header_value);
    }
    return ESP_OK;
}

static esp_err_t firmware_downloader_http_open(
    const char *url,
    const char *range,
    const char *if_range,
    esp_http_client_handle_t *client_out,
    gateway_http_metadata_t *metadata,
    int *status_code_out,
    int64_t *content_length_out)
{
    esp_http_client_config_t config;
    esp_http_client_handle_t client;
    esp_err_t status;
    int64_t content_length;

    if ((url == NULL) || (client_out == NULL) || (metadata == NULL) ||
        (status_code_out == NULL) || (content_length_out == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&config, 0, sizeof(config));
    memset(metadata, 0, sizeof(*metadata));
    config.url = url;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = GATEWAY_FIRMWARE_HTTP_TIMEOUT_MS;
    config.buffer_size = GATEWAY_FIRMWARE_DOWNLOAD_CHUNK_SIZE;
    config.event_handler = firmware_downloader_http_event;
    config.user_data = metadata;
    config.disable_auto_redirect = true;
    if (firmware_downloader_url_is_https(url)) {
        config.cert_pem = (const char *)m10_ca_pem_start;
        config.cert_len = (size_t)(m10_ca_pem_end - m10_ca_pem_start);
        config.skip_cert_common_name_check = false;
    } else if (!GATEWAY_ALLOW_INSECURE_HTTP) {
        return ESP_ERR_INVALID_ARG;
    }
    client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (range != NULL) {
        status = esp_http_client_set_header(client, "Range", range);
        if (status != ESP_OK) {
            esp_http_client_cleanup(client);
            return status;
        }
    }
    if (if_range != NULL) {
        status = esp_http_client_set_header(client, "If-Range", if_range);
        if (status != ESP_OK) {
            esp_http_client_cleanup(client);
            return status;
        }
    }
    status = esp_http_client_open(client, 0);
    if (status != ESP_OK) {
        esp_http_client_cleanup(client);
        return status;
    }
    content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }
    *status_code_out = esp_http_client_get_status_code(client);
    *content_length_out = content_length;
    *client_out = client;
    return ESP_OK;
}

static void firmware_downloader_http_close(esp_http_client_handle_t client)
{
    if (client != NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
}

static esp_err_t firmware_downloader_read_exact(esp_http_client_handle_t client,
                                                 char *buffer,
                                                 size_t length)
{
    size_t received = 0U;

    while (received < length) {
        int read_length = esp_http_client_read(client,
                                               buffer + received,
                                               (int)(length - received));
        if (read_length <= 0) {
            return (read_length < 0) ? read_length : ESP_ERR_INVALID_RESPONSE;
        }
        received += (size_t)read_length;
    }
    return ESP_OK;
}

static bool firmware_downloader_json_u32(const cJSON *root,
                                         const char *name,
                                         uint32_t *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    uint32_t candidate;

    if ((item == NULL) || !cJSON_IsNumber(item) ||
        (item->valuedouble < 0.0) ||
        (item->valuedouble > (double)UINT32_MAX)) {
        return false;
    }
    candidate = (uint32_t)item->valuedouble;
    if ((double)candidate != item->valuedouble) {
        return false;
    }
    *value = candidate;
    return true;
}

static bool firmware_downloader_json_string(const cJSON *root,
                                            const char *name,
                                            char *destination,
                                            size_t destination_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    size_t length;

    if ((item == NULL) || !cJSON_IsString(item) || (item->valuestring == NULL)) {
        return false;
    }
    length = strlen(item->valuestring);
    if ((length == 0U) || (length >= destination_size)) {
        return false;
    }
    memcpy(destination, item->valuestring, length + 1U);
    return true;
}

static bool firmware_downloader_hex_nibble(char character, uint8_t *value)
{
    if ((character >= '0') && (character <= '9')) {
        *value = (uint8_t)(character - '0');
        return true;
    }
    if ((character >= 'A') && (character <= 'F')) {
        *value = (uint8_t)(character - 'A' + 10);
        return true;
    }
    if ((character >= 'a') && (character <= 'f')) {
        *value = (uint8_t)(character - 'a' + 10);
        return true;
    }
    return false;
}

static bool firmware_downloader_parse_sha256(const char *text, uint8_t digest[32])
{
    size_t index;

    if ((text == NULL) || (strlen(text) != (GATEWAY_FIRMWARE_SHA256_SIZE * 2U))) {
        return false;
    }
    for (index = 0U; index < GATEWAY_FIRMWARE_SHA256_SIZE; index++) {
        uint8_t high;
        uint8_t low;

        if (!firmware_downloader_hex_nibble(text[index * 2U], &high) ||
            !firmware_downloader_hex_nibble(text[(index * 2U) + 1U], &low)) {
            return false;
        }
        digest[index] = (uint8_t)((high << 4U) | low);
    }
    return true;
}

static bool firmware_downloader_parse_crc32(const char *text, uint32_t *crc32)
{
    size_t index;
    uint32_t value = 0U;

    if ((text == NULL) || (strlen(text) != 8U) || (crc32 == NULL)) {
        return false;
    }
    for (index = 0U; index < 8U; index++) {
        uint8_t nibble;

        if (!firmware_downloader_hex_nibble(text[index], &nibble)) {
            return false;
        }
        value = (value << 4U) | nibble;
    }
    *crc32 = value;
    return true;
}

static esp_err_t firmware_downloader_parse_manifest(const char *payload,
                                                    size_t payload_length,
                                                    const char *requested_id,
                                                    const char *etag,
                                                    gateway_remote_manifest_t *manifest)
{
    cJSON *root;
    char crc32_text[9];
    char package_crc32_text[9];
    char image_sha256_text[65];
    char package_sha256_text[65];
    char expected_path[GATEWAY_DOWNLOAD_URL_MAX_LENGTH];
    uint32_t value;
    int path_length;
    esp_err_t status = ESP_ERR_INVALID_RESPONSE;

    if ((payload == NULL) || (requested_id == NULL) || (etag == NULL) ||
        (etag[0] == '\0') || (manifest == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    root = cJSON_ParseWithLength(payload, payload_length);
    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    memset(manifest, 0, sizeof(*manifest));
    if (!firmware_downloader_json_u32(root, "schema_version", &value) || (value != 1U) ||
        !firmware_downloader_json_string(root, "firmware_id", manifest->firmware_id,
                                         sizeof(manifest->firmware_id)) ||
        !firmware_downloader_json_u32(root, "product_id", &value) ||
        (value != GATEWAY_STM32_PRODUCT_ID) ||
        !firmware_downloader_json_u32(root, "hardware_id", &value) ||
        (value != GATEWAY_STM32_HARDWARE_ID) ||
        !firmware_downloader_json_u32(root, "firmware_version_code",
                                      &manifest->firmware_version_code) ||
        (manifest->firmware_version_code == 0U) ||
        !firmware_downloader_json_u32(root, "image_size", &manifest->image_size) ||
        (manifest->image_size == 0U) ||
        (manifest->image_size > GATEWAY_STM32_APP_MAX_SIZE) ||
        !firmware_downloader_json_string(root, "crc32", crc32_text, sizeof(crc32_text)) ||
        !firmware_downloader_json_string(root, "sha256", image_sha256_text,
                                         sizeof(image_sha256_text)) ||
        !firmware_downloader_json_u32(root, "package_format", &value) ||
        (value != GATEWAY_FIRMWARE_PACKAGE_FORMAT) ||
        !firmware_downloader_json_u32(root, "package_header_size", &value) ||
        (value != GATEWAY_FIRMWARE_HEADER_SIZE) ||
        !firmware_downloader_json_u32(root, "package_image_offset", &value) ||
        (value != GATEWAY_FIRMWARE_IMAGE_OFFSET) ||
        !firmware_downloader_json_u32(root, "package_size", &manifest->package_size) ||
        !firmware_downloader_json_string(root, "package_crc32", package_crc32_text,
                                         sizeof(package_crc32_text)) ||
        !firmware_downloader_json_string(root, "package_sha256", package_sha256_text,
                                         sizeof(package_sha256_text)) ||
        !firmware_downloader_json_string(root, "download_url", manifest->download_path,
                                         sizeof(manifest->download_path))) {
        goto finished;
    }
    if ((strcmp(manifest->firmware_id, requested_id) != 0) ||
        !firmware_downloader_is_safe_id(manifest->firmware_id) ||
        (manifest->package_size !=
         (GATEWAY_FIRMWARE_IMAGE_OFFSET + manifest->image_size)) ||
        (manifest->package_size > firmware_store_partition()->size) ||
        !firmware_downloader_parse_crc32(crc32_text, &manifest->image_crc32) ||
        !firmware_downloader_parse_crc32(package_crc32_text, &value) ||
        !firmware_downloader_parse_sha256(image_sha256_text, manifest->image_sha256) ||
        !firmware_downloader_parse_sha256(package_sha256_text, manifest->package_sha256)) {
        goto finished;
    }
    path_length = snprintf(expected_path,
                           sizeof(expected_path),
                           "/api/v1/firmwares/%s/binary",
                           requested_id);
    if ((path_length < 0) || ((size_t)path_length >= sizeof(expected_path)) ||
        (strcmp(manifest->download_path, expected_path) != 0) ||
        (strlen(etag) >= sizeof(manifest->etag))) {
        goto finished;
    }
    strcpy(manifest->etag, etag);
    status = ESP_OK;

finished:
    cJSON_Delete(root);
    return status;
}

static esp_err_t firmware_downloader_make_url(const char *path,
                                              char *url,
                                              size_t url_size)
{
    char server_url[GATEWAY_FIRMWARE_SERVER_URL_MAX_LENGTH];
    size_t base_length;
    int result;
    esp_err_t status;

    if ((path == NULL) || (url == NULL) || (path[0] != '/')) {
        return ESP_ERR_INVALID_STATE;
    }
    status = gateway_wifi_get_server_url(server_url, sizeof(server_url));
    if (status != ESP_OK) {
        return status;
    }
    base_length = strlen(server_url);
    while ((base_length > 0U) &&
           (server_url[base_length - 1U] == '/')) {
        base_length--;
    }
    result = snprintf(url, url_size, "%.*s%s", (int)base_length,
                      server_url, path);
    return ((result < 0) || ((size_t)result >= url_size)) ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

static esp_err_t firmware_downloader_fetch_manifest(const char *firmware_id,
                                                    gateway_remote_manifest_t *manifest)
{
    char path[GATEWAY_DOWNLOAD_URL_MAX_LENGTH];
    char url[GATEWAY_DOWNLOAD_URL_MAX_LENGTH];
    char payload[GATEWAY_DOWNLOAD_MANIFEST_MAX_SIZE + 1U];
    gateway_http_metadata_t metadata;
    esp_http_client_handle_t client = NULL;
    int status_code;
    int64_t content_length;
    int result;
    esp_err_t status;

    result = snprintf(path, sizeof(path), "/api/v1/firmwares/%s/manifest", firmware_id);
    if ((result < 0) || ((size_t)result >= sizeof(path))) {
        return ESP_ERR_INVALID_SIZE;
    }
    status = firmware_downloader_make_url(path, url, sizeof(url));
    if (status != ESP_OK) {
        return status;
    }
    status = firmware_downloader_http_open(url, NULL, NULL, &client, &metadata,
                                           &status_code, &content_length);
    if (status != ESP_OK) {
        return status;
    }
    if ((status_code != HttpStatus_Ok) || (content_length <= 0) ||
        ((uint64_t)content_length > GATEWAY_DOWNLOAD_MANIFEST_MAX_SIZE)) {
        firmware_downloader_http_close(client);
        return ESP_ERR_INVALID_RESPONSE;
    }
    status = firmware_downloader_read_exact(client, payload, (size_t)content_length);
    firmware_downloader_http_close(client);
    if (status != ESP_OK) {
        return status;
    }
    payload[content_length] = '\0';
    return firmware_downloader_parse_manifest(payload, (size_t)content_length,
                                              firmware_id, metadata.etag, manifest);
}

static bool firmware_downloader_checkpoint_matches(
    const gateway_download_record_t *record,
    const gateway_remote_manifest_t *manifest)
{
    return (strcmp(record->firmware_id, manifest->firmware_id) == 0) &&
           (record->package_size == manifest->package_size) &&
           (memcmp(record->package_sha256, manifest->package_sha256,
                   GATEWAY_FIRMWARE_SHA256_SIZE) == 0) &&
           (strcmp(record->etag, manifest->etag) == 0);
}

static void firmware_downloader_init_record(gateway_download_record_t *record,
                                            const gateway_remote_manifest_t *manifest)
{
    memset(record, 0, sizeof(*record));
    record->magic = GATEWAY_DOWNLOAD_RECORD_MAGIC;
    record->schema_version = GATEWAY_DOWNLOAD_RECORD_SCHEMA;
    record->package_size = manifest->package_size;
    memcpy(record->package_sha256, manifest->package_sha256,
           GATEWAY_FIRMWARE_SHA256_SIZE);
    strcpy(record->firmware_id, manifest->firmware_id);
    strcpy(record->etag, manifest->etag);
}

static esp_err_t firmware_downloader_parse_content_range(const char *text,
                                                         uint32_t *start,
                                                         uint32_t *end,
                                                         uint32_t *total)
{
    unsigned long parsed_start;
    unsigned long parsed_end;
    unsigned long parsed_total;
    char trailing;

    if ((text == NULL) || (start == NULL) || (end == NULL) || (total == NULL) ||
        (sscanf(text, "bytes %lu-%lu/%lu%c", &parsed_start, &parsed_end,
                &parsed_total, &trailing) != 3) ||
        (parsed_start > UINT32_MAX) || (parsed_end > UINT32_MAX) ||
        (parsed_total > UINT32_MAX) || (parsed_end < parsed_start)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *start = (uint32_t)parsed_start;
    *end = (uint32_t)parsed_end;
    *total = (uint32_t)parsed_total;
    return ESP_OK;
}

static esp_err_t firmware_downloader_download_binary(
    const gateway_remote_manifest_t *manifest,
    gateway_download_record_t *record,
    bool resume)
{
    char url[GATEWAY_DOWNLOAD_URL_MAX_LENGTH];
    char range[GATEWAY_DOWNLOAD_RANGE_MAX_LENGTH];
    uint8_t buffer[GATEWAY_FIRMWARE_DOWNLOAD_CHUNK_SIZE];
    gateway_http_metadata_t metadata;
    esp_http_client_handle_t client = NULL;
    uint32_t received = record->received_size;
    uint32_t stored_checkpoint = received;
    int status_code;
    int64_t content_length;
    esp_err_t status;

    if (received == manifest->package_size) {
        return ESP_OK;
    }
    status = firmware_downloader_make_url(manifest->download_path, url, sizeof(url));
    if (status != ESP_OK) {
        return status;
    }
    if (resume && (received > 0U)) {
        int result = snprintf(range, sizeof(range), "bytes=%" PRIu32 "-", received);

        if ((result < 0) || ((size_t)result >= sizeof(range))) {
            return ESP_ERR_INVALID_SIZE;
        }
        GW_LOGI(TAG,
                "Requesting HTTP resume at byte %" PRIu32 " with If-Range %s",
                received,
                record->etag);
        status = firmware_downloader_http_open(url, range, record->etag, &client,
                                               &metadata, &status_code, &content_length);
    } else {
        status = firmware_downloader_http_open(url, NULL, NULL, &client,
                                               &metadata, &status_code, &content_length);
    }
    if (status != ESP_OK) {
        return status;
    }
    if ((metadata.etag[0] == '\0') || (strcmp(metadata.etag, manifest->etag) != 0)) {
        firmware_downloader_http_close(client);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (resume && (received > 0U) && (status_code == HttpStatus_Ok)) {
        GW_LOGW(TAG,
                "M8 returned HTTP 200 for checkpoint at byte %" PRIu32
                "; restarting from byte 0",
                received);
        firmware_downloader_http_close(client);
        status = firmware_store_download_begin(manifest->package_size);
        if (status != ESP_OK) {
            return status;
        }
        firmware_downloader_init_record(record, manifest);
        status = firmware_downloader_save_checkpoint(record);
        if (status != ESP_OK) {
            return status;
        }
        firmware_downloader_set_received(0U);
        firmware_downloader_set_resume(true);
        return firmware_downloader_download_binary(manifest, record, false);
    }
    if (resume && (received > 0U)) {
        uint32_t range_start;
        uint32_t range_end;
        uint32_t range_total;

        status = firmware_downloader_parse_content_range(metadata.content_range,
                                                         &range_start, &range_end,
                                                         &range_total);
        if ((status != ESP_OK) || (status_code != HttpStatus_PartialContent) ||
            (range_start != received) || (range_total != manifest->package_size) ||
            (range_end != (manifest->package_size - 1U)) ||
            (content_length != (int64_t)(manifest->package_size - received))) {
            firmware_downloader_http_close(client);
            return ESP_ERR_INVALID_RESPONSE;
        }
        GW_LOGI(TAG, "HTTP resume accepted: %s", metadata.content_range);
    } else if ((status_code != HttpStatus_Ok) ||
               (content_length != (int64_t)manifest->package_size)) {
        firmware_downloader_http_close(client);
        return ESP_ERR_INVALID_RESPONSE;
    }

    while (received < manifest->package_size) {
        uint32_t remaining = manifest->package_size - received;
        int read_length;

        if (firmware_downloader_cancel_requested()) {
            firmware_downloader_http_close(client);
            return ESP_ERR_INVALID_STATE;
        }
        read_length = esp_http_client_read(client, (char *)buffer,
                                           (remaining > sizeof(buffer)) ?
                                           (int)sizeof(buffer) : (int)remaining);
        if (read_length <= 0) {
            firmware_downloader_http_close(client);
            return (read_length < 0) ? read_length : ESP_ERR_INVALID_RESPONSE;
        }
        status = firmware_store_download_write(received, buffer, (size_t)read_length);
        if (status != ESP_OK) {
            firmware_downloader_http_close(client);
            return status;
        }
        received += (uint32_t)read_length;
        record->received_size = received;
        firmware_downloader_set_received(received);
        if ((received == manifest->package_size) ||
            ((received / GATEWAY_FIRMWARE_DOWNLOAD_CHECKPOINT_SIZE) >
             (stored_checkpoint / GATEWAY_FIRMWARE_DOWNLOAD_CHECKPOINT_SIZE))) {
            status = firmware_downloader_save_checkpoint(record);
            if (status != ESP_OK) {
                firmware_downloader_http_close(client);
                return status;
            }
            stored_checkpoint = received;
            firmware_downloader_set_resume(true);
            GW_LOGI(TAG, "Downloaded %" PRIu32 "/%" PRIu32 " bytes",
                    received, manifest->package_size);
        }
    }
    firmware_downloader_http_close(client);
    return ESP_OK;
}

static void firmware_downloader_restore_marker_for_hash(uint32_t package_offset,
                                                        uint8_t *buffer,
                                                        size_t length)
{
    const uint32_t marker_offset =
        (uint32_t)offsetof(gateway_firmware_manifest_t, valid_marker);
    const uint32_t marker_end = marker_offset + sizeof(uint32_t);
    const uint32_t buffer_end = package_offset + (uint32_t)length;
    const uint32_t marker = GATEWAY_FIRMWARE_VALID_MARKER;
    uint32_t copy_start;
    uint32_t copy_end;

    if ((buffer == NULL) || (length == 0U) ||
        (package_offset >= marker_end) || (buffer_end <= marker_offset)) {
        return;
    }
    copy_start = (package_offset > marker_offset) ? package_offset : marker_offset;
    copy_end = (buffer_end < marker_end) ? buffer_end : marker_end;
    memcpy(buffer + (copy_start - package_offset),
           ((const uint8_t *)&marker) + (copy_start - marker_offset),
           copy_end - copy_start);
}

static esp_err_t firmware_downloader_verify_package(
    const gateway_remote_manifest_t *manifest)
{
    gateway_firmware_manifest_t header;
    mbedtls_sha256_context sha256;
    uint8_t buffer[GATEWAY_FIRMWARE_DOWNLOAD_CHUNK_SIZE];
    uint8_t digest[GATEWAY_FIRMWARE_SHA256_SIZE];
    uint32_t offset = 0U;
    esp_err_t status = ESP_OK;

    mbedtls_sha256_init(&sha256);
    if (mbedtls_sha256_starts(&sha256, false) != 0) {
        mbedtls_sha256_free(&sha256);
        return ESP_FAIL;
    }
    while (offset < manifest->package_size) {
        size_t length = manifest->package_size - offset;

        if (firmware_downloader_cancel_requested()) {
            status = ESP_ERR_INVALID_STATE;
            goto finished;
        }
        if (length > sizeof(buffer)) {
            length = sizeof(buffer);
        }
        status = firmware_store_download_read(offset, buffer, length);
        if (status != ESP_OK) {
            goto finished;
        }
        /*
         * Flash intentionally keeps valid_marker erased until commit().
         * Reconstruct the authenticated package bytes only in this hash buffer
         * so the server SHA-256 can be checked without making the staged package
         * visible to M7 before all validations have passed.
         */
        firmware_downloader_restore_marker_for_hash(offset, buffer, length);
        if (mbedtls_sha256_update(&sha256, buffer, length) != 0) {
            status = ESP_FAIL;
            goto finished;
        }
        offset += (uint32_t)length;
    }
    if (mbedtls_sha256_finish(&sha256, digest) != 0) {
        status = ESP_FAIL;
        goto finished;
    }
    if (memcmp(digest, manifest->package_sha256, sizeof(digest)) != 0) {
        GW_LOGE(TAG, "Package SHA-256 mismatch after staged Flash reread");
        status = ESP_ERR_INVALID_CRC;
        goto finished;
    }
    status = firmware_store_download_read(0U, &header, sizeof(header));
    if (status != ESP_OK) {
        goto finished;
    }
    if ((header.magic != GATEWAY_FIRMWARE_PACKAGE_MAGIC) ||
        (header.format_version != GATEWAY_FIRMWARE_PACKAGE_FORMAT) ||
        (header.header_size != GATEWAY_FIRMWARE_HEADER_SIZE) ||
        (header.firmware_version != manifest->firmware_version_code) ||
        (header.image_size != manifest->image_size) ||
        (header.image_crc32 != manifest->image_crc32) ||
        (header.product_id != GATEWAY_STM32_PRODUCT_ID) ||
        (header.hardware_id != GATEWAY_STM32_HARDWARE_ID) ||
        (header.valid_marker != UINT32_MAX) ||
        (memcmp(header.image_sha256, manifest->image_sha256,
                GATEWAY_FIRMWARE_SHA256_SIZE) != 0)) {
        status = ESP_ERR_INVALID_RESPONSE;
    }

finished:
    mbedtls_sha256_free(&sha256);
    return status;
}

static void firmware_downloader_finish(esp_err_t status)
{
    gateway_firmware_download_state_t final_state;

    if (status == ESP_OK) {
        final_state = GW_FW_DL_READY;
    } else if (firmware_downloader_cancel_requested()) {
        final_state = GW_FW_DL_CANCELED;
    } else {
        final_state = GW_FW_DL_FAILED;
    }
    firmware_downloader_lock();
    s_progress.state = final_state;
    s_progress.last_error = status;
    s_task = NULL;
    s_cancel_requested = false;
    firmware_downloader_unlock();

    if (final_state == GW_FW_DL_READY) {
        GW_LOGI(TAG, "Package download completed; run 'upgrade start' explicitly to update STM32");
    } else if (final_state == GW_FW_DL_CANCELED) {
        GW_LOGW(TAG, "Package download canceled; a checkpoint is available for the same release");
    } else {
        GW_LOGE(TAG, "Package download failed: %s", esp_err_to_name(status));
    }
}

static void firmware_downloader_task(void *argument)
{
    gateway_remote_manifest_t manifest;
    gateway_download_record_t record;
    char firmware_id[GATEWAY_FIRMWARE_DOWNLOAD_ID_MAX_LENGTH + 1U];
    bool resume = false;
    esp_err_t status;

    (void)argument;
    firmware_downloader_lock();
    strcpy(firmware_id, s_requested_firmware_id);
    firmware_downloader_unlock();

    firmware_downloader_set_state(GW_FW_DL_WAIT_WIFI);
    status = gateway_wifi_wait_connected(GATEWAY_WIFI_CONNECT_TIMEOUT_MS);
    if (status != ESP_OK) {
        goto finished;
    }
    if (firmware_downloader_cancel_requested()) {
        status = ESP_ERR_INVALID_STATE;
        goto finished;
    }
    firmware_downloader_set_state(GW_FW_DL_WAIT_TIME);
    status = gateway_wifi_wait_time_synced(GATEWAY_TIME_SYNC_TIMEOUT_MS);
    if (status != ESP_OK) {
        goto finished;
    }
    if (firmware_downloader_cancel_requested()) {
        status = ESP_ERR_INVALID_STATE;
        goto finished;
    }
    firmware_downloader_set_state(GW_FW_DL_FETCH_MANIFEST);
    status = firmware_downloader_fetch_manifest(firmware_id, &manifest);
    if (status != ESP_OK) {
        goto finished;
    }
    firmware_downloader_set_state(GW_FW_DL_VALIDATE_MANIFEST);
    if (firmware_downloader_cancel_requested()) {
        status = ESP_ERR_INVALID_STATE;
        goto finished;
    }

    status = firmware_downloader_load_checkpoint(&record);
    if (status == ESP_OK) {
        if (firmware_downloader_checkpoint_matches(&record, &manifest)) {
            status = firmware_store_download_resume(manifest.package_size);
            resume = (status == ESP_OK);
        } else {
            GW_LOGW(TAG,
                    "Saved checkpoint does not match the current manifest; restarting from byte 0");
        }
    }
    if (!resume) {
        firmware_downloader_set_state(GW_FW_DL_PREPARE);
        status = firmware_store_download_begin(manifest.package_size);
        if (status != ESP_OK) {
            goto finished;
        }
        firmware_downloader_init_record(&record, &manifest);
        status = firmware_downloader_save_checkpoint(&record);
        if (status != ESP_OK) {
            goto finished;
        }
    }
    firmware_downloader_lock();
    s_progress.package_size = manifest.package_size;
    s_progress.received_size = record.received_size;
    s_progress.can_resume = true;
    firmware_downloader_unlock();

    firmware_downloader_set_state(GW_FW_DL_DOWNLOAD);
    status = firmware_downloader_download_binary(&manifest, &record, resume);
    if (status != ESP_OK) {
        if (firmware_downloader_cancel_requested()) {
            (void)firmware_downloader_save_checkpoint(&record);
            firmware_downloader_set_resume(true);
        }
        goto finished;
    }
    firmware_downloader_set_state(GW_FW_DL_VERIFY_PACKAGE);
    status = firmware_downloader_verify_package(&manifest);
    if (status != ESP_OK) {
        if (firmware_downloader_cancel_requested()) {
            (void)firmware_downloader_save_checkpoint(&record);
            firmware_downloader_set_resume(true);
        }
        goto finished;
    }
    firmware_downloader_set_state(GW_FW_DL_COMMIT_PACKAGE);
    if (firmware_downloader_cancel_requested()) {
        status = ESP_ERR_INVALID_STATE;
        goto finished;
    }
    status = firmware_store_download_commit();
    if (status != ESP_OK) {
        goto finished;
    }
    status = firmware_downloader_clear_checkpoint();
    if (status != ESP_OK) {
        GW_LOGW(TAG,
                "Package was committed but checkpoint cleanup failed: %s",
                esp_err_to_name(status));
    }
    firmware_downloader_set_resume(false);
    status = ESP_OK;

finished:
    firmware_downloader_finish(status);
    vTaskDelete(NULL);
}

esp_err_t firmware_downloader_init(void)
{
    gateway_download_record_t record;
    esp_err_t status;

    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    firmware_downloader_lock();
    memset(&s_progress, 0, sizeof(s_progress));
    s_progress.state = GW_FW_DL_IDLE;
    s_progress.last_error = ESP_OK;
    s_task = NULL;
    s_cancel_requested = false;
    firmware_downloader_unlock();

    if (firmware_store_is_ready()) {
        status = firmware_downloader_clear_checkpoint();
        if (status != ESP_OK) {
            GW_LOGW(TAG, "Unable to clear stale M9 checkpoint: %s", esp_err_to_name(status));
        }
        return ESP_OK;
    }

    status = firmware_downloader_load_checkpoint(&record);
    if (status == ESP_OK) {
        firmware_downloader_lock();
        s_progress.package_size = record.package_size;
        s_progress.received_size = record.received_size;
        s_progress.can_resume = true;
        strcpy(s_progress.firmware_id, record.firmware_id);
        firmware_downloader_unlock();
        GW_LOGW(TAG, "Found M9 resume checkpoint: id=%s, received=%" PRIu32 "/%" PRIu32,
                record.firmware_id, record.received_size, record.package_size);
    }
    return ESP_OK;
}

esp_err_t firmware_downloader_start(const char *firmware_id)
{
    BaseType_t created;

    if ((s_lock == NULL) || !firmware_downloader_is_safe_id(firmware_id) ||
        !gateway_wifi_is_configured() || !firmware_downloader_is_http_server_configured()) {
        return ESP_ERR_INVALID_ARG;
    }
    firmware_downloader_lock();
    if (s_task != NULL) {
        firmware_downloader_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    memset(&s_progress, 0, sizeof(s_progress));
    s_progress.state = GW_FW_DL_WAIT_WIFI;
    s_progress.last_error = ESP_OK;
    strcpy(s_progress.firmware_id, firmware_id);
    strcpy(s_requested_firmware_id, firmware_id);
    s_cancel_requested = false;
    firmware_downloader_unlock();

    created = xTaskCreate(firmware_downloader_task,
                          "firmware_download",
                          GATEWAY_FIRMWARE_DOWNLOAD_TASK_STACK_SIZE,
                          NULL,
                          GATEWAY_FIRMWARE_DOWNLOAD_TASK_PRIORITY,
                          &s_task);
    if (created != pdPASS) {
        firmware_downloader_finish(ESP_ERR_NO_MEM);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t firmware_downloader_cancel(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    firmware_downloader_lock();
    if (s_task == NULL) {
        firmware_downloader_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_progress.state == GW_FW_DL_COMMIT_PACKAGE) {
        firmware_downloader_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    s_cancel_requested = true;
    firmware_downloader_unlock();
    return ESP_OK;
}

bool firmware_downloader_is_active(void)
{
    bool active;

    if (s_lock == NULL) {
        return false;
    }
    firmware_downloader_lock();
    active = (s_task != NULL);
    firmware_downloader_unlock();
    return active;
}

esp_err_t firmware_downloader_get_progress(
    gateway_firmware_download_progress_t *progress)
{
    if ((progress == NULL) || (s_lock == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    firmware_downloader_lock();
    *progress = s_progress;
    firmware_downloader_unlock();
    return ESP_OK;
}

const char *firmware_downloader_state_name(
    gateway_firmware_download_state_t state)
{
    switch (state) {
        case GW_FW_DL_IDLE: return "IDLE";
        case GW_FW_DL_WAIT_WIFI: return "WAIT_WIFI";
        case GW_FW_DL_WAIT_TIME: return "WAIT_TIME";
        case GW_FW_DL_FETCH_MANIFEST: return "FETCH_MANIFEST";
        case GW_FW_DL_VALIDATE_MANIFEST: return "VALIDATE_MANIFEST";
        case GW_FW_DL_PREPARE: return "PREPARE";
        case GW_FW_DL_DOWNLOAD: return "DOWNLOAD";
        case GW_FW_DL_VERIFY_PACKAGE: return "VERIFY_PACKAGE";
        case GW_FW_DL_COMMIT_PACKAGE: return "COMMIT_PACKAGE";
        case GW_FW_DL_READY: return "READY";
        case GW_FW_DL_FAILED: return "FAILED";
        case GW_FW_DL_CANCELED: return "CANCELED";
        default: return "UNKNOWN";
    }
}
