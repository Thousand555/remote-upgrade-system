#include "firmware_store.h"

#include <stddef.h>

#include "gateway_config.h"
#include "gateway_log.h"

#define FIRMWARE_STORE_PARTITION_TYPE    ((esp_partition_type_t)0x40)
#define FIRMWARE_STORE_PARTITION_SUBTYPE ((esp_partition_subtype_t)0x00)
#define FIRMWARE_STORE_PARTITION_LABEL   "stm_fw"
#define FIRMWARE_STORE_FLASH_SECTOR_SIZE 4096U
#define FIRMWARE_STORE_VALID_MARKER_OFFSET \
    offsetof(gateway_firmware_manifest_t, valid_marker)

static const char *TAG = "firmware_store";
static const esp_partition_t *s_partition;
static gateway_firmware_manifest_t s_manifest;
static bool s_manifest_valid;
static bool s_download_active;
static uint32_t s_download_package_size;

typedef char gateway_firmware_manifest_size_must_be_128_bytes[
    (sizeof(gateway_firmware_manifest_t) == GATEWAY_FIRMWARE_HEADER_SIZE) ? 1 : -1];

static uint32_t firmware_store_crc32_update(uint32_t crc,
                                            const uint8_t *data,
                                            size_t length)
{
    size_t byte_index;
    uint32_t bit_index;

    for (byte_index = 0U; byte_index < length; byte_index++) {
        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < 8U; bit_index++) {
            crc = ((crc & 1U) != 0U) ?
                  ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return crc;
}

static esp_err_t firmware_store_validate_manifest(void)
{
    uint32_t calculated_crc;
    uint32_t image_end;

    if ((s_manifest.magic != GATEWAY_FIRMWARE_PACKAGE_MAGIC) ||
        (s_manifest.format_version != GATEWAY_FIRMWARE_PACKAGE_FORMAT) ||
        (s_manifest.header_size != GATEWAY_FIRMWARE_HEADER_SIZE) ||
        (s_manifest.valid_marker != GATEWAY_FIRMWARE_VALID_MARKER)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if ((s_manifest.firmware_version == 0U) ||
        (s_manifest.image_size == 0U) ||
        (s_manifest.image_size > GATEWAY_STM32_APP_MAX_SIZE)) {
        return ESP_ERR_INVALID_SIZE;
    }
    if ((s_manifest.product_id != GATEWAY_STM32_PRODUCT_ID) ||
        (s_manifest.hardware_id != GATEWAY_STM32_HARDWARE_ID)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    image_end = GATEWAY_FIRMWARE_IMAGE_OFFSET + s_manifest.image_size;
    if ((image_end < GATEWAY_FIRMWARE_IMAGE_OFFSET) ||
        (image_end > s_partition->size)) {
        return ESP_ERR_INVALID_SIZE;
    }

    calculated_crc = ~firmware_store_crc32_update(
        0xFFFFFFFFUL,
        (const uint8_t *)&s_manifest,
        offsetof(gateway_firmware_manifest_t, header_crc32));
    if (calculated_crc != s_manifest.header_crc32) {
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

static esp_err_t firmware_store_validate_download_range(uint32_t package_offset,
                                                        size_t length)
{
    uint32_t end_offset;

    if ((s_partition == NULL) || ((length != 0U) && (package_offset >= s_download_package_size))) {
        return ESP_ERR_INVALID_STATE;
    }
    if (length > UINT32_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    end_offset = package_offset + (uint32_t)length;
    if ((end_offset < package_offset) || (end_offset > s_download_package_size)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t firmware_store_validate_image(void)
{
    uint8_t buffer[1024];
    uint32_t crc;
    uint32_t offset;
    size_t chunk_size;
    esp_err_t status;

    crc = 0xFFFFFFFFUL;
    offset = 0U;
    while (offset < s_manifest.image_size) {
        chunk_size = s_manifest.image_size - offset;
        if (chunk_size > sizeof(buffer)) {
            chunk_size = sizeof(buffer);
        }
        status = esp_partition_read(s_partition,
                                    GATEWAY_FIRMWARE_IMAGE_OFFSET + offset,
                                    buffer,
                                    chunk_size);
        if (status != ESP_OK) {
            return status;
        }
        crc = firmware_store_crc32_update(crc, buffer, chunk_size);
        offset += (uint32_t)chunk_size;
    }
    crc = ~crc;
    if (crc != s_manifest.image_crc32) {
        GW_LOGE(TAG,
                "Firmware CRC mismatch: calculated=0x%08lx, expected=0x%08lx",
                (unsigned long)crc,
                (unsigned long)s_manifest.image_crc32);
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

esp_err_t firmware_store_open(void)
{
    esp_err_t status;

    s_manifest_valid = false;
    s_download_active = false;
    s_download_package_size = 0U;
    s_partition = esp_partition_find_first(FIRMWARE_STORE_PARTITION_TYPE,
                                           FIRMWARE_STORE_PARTITION_SUBTYPE,
                                           FIRMWARE_STORE_PARTITION_LABEL);
    if (s_partition == NULL) {
        GW_LOGE(TAG, "Partition '%s' was not found", FIRMWARE_STORE_PARTITION_LABEL);
        return ESP_ERR_NOT_FOUND;
    }

    if (s_partition->size <
        (GATEWAY_FIRMWARE_IMAGE_OFFSET + GATEWAY_STM32_APP_MAX_SIZE)) {
        GW_LOGE(TAG,
                "Partition is too small: size=0x%08x, required>=0x%08x",
                (unsigned int)s_partition->size,
                (unsigned int)(GATEWAY_FIRMWARE_IMAGE_OFFSET +
                               GATEWAY_STM32_APP_MAX_SIZE));
        s_partition = NULL;
        return ESP_ERR_INVALID_SIZE;
    }

    status = esp_partition_read(s_partition, 0U, &s_manifest, sizeof(s_manifest));
    if (status != ESP_OK) {
        return status;
    }

    status = firmware_store_validate_manifest();
    if (status == ESP_OK) {
        s_manifest_valid = true;
        GW_LOGI(TAG,
                "Local firmware package: version=%lu, size=%lu, CRC32=0x%08lx",
                (unsigned long)s_manifest.firmware_version,
                (unsigned long)s_manifest.image_size,
                (unsigned long)s_manifest.image_crc32);
    } else {
        GW_LOGW(TAG,
                "No valid local firmware package yet (%s); use the packaging tool before 'upgrade start'",
                esp_err_to_name(status));
    }

    /* An empty cache is valid during normal gateway boot. */
    return ESP_OK;
}

esp_err_t firmware_store_download_begin(uint32_t package_size)
{
    uint32_t erase_size;
    esp_err_t status;

    if ((s_partition == NULL) ||
        (package_size < GATEWAY_FIRMWARE_IMAGE_OFFSET) ||
        (package_size > s_partition->size)) {
        return ESP_ERR_INVALID_SIZE;
    }
    erase_size = (package_size + (FIRMWARE_STORE_FLASH_SECTOR_SIZE - 1U)) &
                 ~(FIRMWARE_STORE_FLASH_SECTOR_SIZE - 1U);
    if ((erase_size < package_size) || (erase_size > s_partition->size)) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_manifest_valid = false;
    s_download_active = false;
    s_download_package_size = 0U;
    status = esp_partition_erase_range(s_partition, 0U, erase_size);
    if (status != ESP_OK) {
        return status;
    }
    s_download_package_size = package_size;
    s_download_active = true;
    return ESP_OK;
}

esp_err_t firmware_store_download_resume(uint32_t package_size)
{
    uint32_t valid_marker;
    esp_err_t status;

    if ((s_partition == NULL) ||
        (package_size < GATEWAY_FIRMWARE_IMAGE_OFFSET) ||
        (package_size > s_partition->size)) {
        return ESP_ERR_INVALID_SIZE;
    }
    status = esp_partition_read(s_partition,
                                FIRMWARE_STORE_VALID_MARKER_OFFSET,
                                &valid_marker,
                                sizeof(valid_marker));
    if (status != ESP_OK) {
        return status;
    }
    if (valid_marker != UINT32_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    s_manifest_valid = false;
    s_download_package_size = package_size;
    s_download_active = true;
    return ESP_OK;
}

esp_err_t firmware_store_download_write(uint32_t package_offset,
                                        const void *buffer,
                                        size_t length)
{
    const uint8_t *bytes = (const uint8_t *)buffer;
    uint32_t marker_offset = (uint32_t)FIRMWARE_STORE_VALID_MARKER_OFFSET;
    uint32_t marker_end = marker_offset + sizeof(uint32_t);
    uint32_t end_offset;
    esp_err_t status;

    if ((buffer == NULL) && (length != 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_download_active) {
        return ESP_ERR_INVALID_STATE;
    }
    status = firmware_store_validate_download_range(package_offset, length);
    if (status != ESP_OK) {
        return status;
    }
    if (length == 0U) {
        return ESP_OK;
    }
    end_offset = package_offset + (uint32_t)length;

    /*
     * Do not write the marker from a downloaded header. The erased value stays
     * in flash until a complete package has been reread and authenticated.
     */
    if ((package_offset < marker_end) && (end_offset > marker_offset)) {
        if (package_offset < marker_offset) {
            status = esp_partition_write(s_partition,
                                         package_offset,
                                         bytes,
                                         marker_offset - package_offset);
            if (status != ESP_OK) {
                return status;
            }
        }
        if (end_offset > marker_end) {
            size_t tail_offset = marker_end - package_offset;
            status = esp_partition_write(s_partition,
                                         marker_end,
                                         bytes + tail_offset,
                                         end_offset - marker_end);
        }
        return status;
    }
    return esp_partition_write(s_partition, package_offset, buffer, length);
}

esp_err_t firmware_store_download_read(uint32_t package_offset,
                                       void *buffer,
                                       size_t length)
{
    esp_err_t status;

    if ((buffer == NULL) && (length != 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_download_active) {
        return ESP_ERR_INVALID_STATE;
    }
    status = firmware_store_validate_download_range(package_offset, length);
    if (status != ESP_OK) {
        return status;
    }
    return esp_partition_read(s_partition, package_offset, buffer, length);
}

esp_err_t firmware_store_download_commit(void)
{
    uint32_t marker = GATEWAY_FIRMWARE_VALID_MARKER;
    esp_err_t status;

    if (!s_download_active) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_download_package_size < GATEWAY_FIRMWARE_HEADER_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    status = esp_partition_read(s_partition, 0U, &s_manifest, sizeof(s_manifest));
    if (status != ESP_OK) {
        return status;
    }
    if (s_manifest.valid_marker != UINT32_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    s_manifest.valid_marker = GATEWAY_FIRMWARE_VALID_MARKER;
    status = firmware_store_validate_manifest();
    if (status != ESP_OK) {
        return status;
    }
    if (s_download_package_size !=
        (GATEWAY_FIRMWARE_IMAGE_OFFSET + s_manifest.image_size)) {
        return ESP_ERR_INVALID_SIZE;
    }
    status = firmware_store_validate_image();
    if (status != ESP_OK) {
        return status;
    }
    status = esp_partition_write(s_partition,
                                 FIRMWARE_STORE_VALID_MARKER_OFFSET,
                                 &marker,
                                 sizeof(marker));
    if (status != ESP_OK) {
        return status;
    }
    s_download_active = false;
    s_download_package_size = 0U;
    return firmware_store_validate();
}

esp_err_t firmware_store_validate(void)
{
    esp_err_t status;

    if (s_partition == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    status = esp_partition_read(s_partition, 0U, &s_manifest, sizeof(s_manifest));
    if (status != ESP_OK) {
        s_manifest_valid = false;
        return status;
    }
    status = firmware_store_validate_manifest();
    if (status != ESP_OK) {
        s_manifest_valid = false;
        return status;
    }

    status = firmware_store_validate_image();
    if (status != ESP_OK) {
        s_manifest_valid = false;
        return status;
    }

    s_manifest_valid = true;
    return ESP_OK;
}

esp_err_t firmware_store_read(uint32_t image_offset,
                              void *buffer,
                              size_t length)
{
    uint32_t end_offset;

    if ((buffer == NULL) && (length != 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_manifest_valid) {
        return ESP_ERR_INVALID_STATE;
    }

    end_offset = image_offset + (uint32_t)length;
    if ((end_offset < image_offset) || (end_offset > s_manifest.image_size)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return esp_partition_read(s_partition,
                              GATEWAY_FIRMWARE_IMAGE_OFFSET + image_offset,
                              buffer,
                              length);
}

bool firmware_store_is_ready(void)
{
    return s_manifest_valid;
}

const gateway_firmware_manifest_t *firmware_store_manifest(void)
{
    return s_manifest_valid ? &s_manifest : NULL;
}

const esp_partition_t *firmware_store_partition(void)
{
    return s_partition;
}
