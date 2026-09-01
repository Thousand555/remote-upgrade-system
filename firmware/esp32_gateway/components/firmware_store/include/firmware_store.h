#ifndef FIRMWARE_STORE_H
#define FIRMWARE_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_partition.h"

#include "gateway_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_size;
    uint32_t firmware_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint16_t product_id;
    uint16_t hardware_id;
    uint8_t image_sha256[GATEWAY_FIRMWARE_SHA256_SIZE];
    uint32_t header_crc32;
    uint32_t valid_marker;
    uint8_t reserved[64];
} gateway_firmware_manifest_t;

esp_err_t firmware_store_open(void);
esp_err_t firmware_store_validate(void);
esp_err_t firmware_store_read(uint32_t image_offset,
                              void *buffer,
                              size_t length);
/*
 * M9 download staging API. The cache is erased by begin(), data is written at
 * package offsets, and the package valid marker remains erased until commit().
 * Callers must verify package authenticity before commit().
 */
esp_err_t firmware_store_download_begin(uint32_t package_size);
esp_err_t firmware_store_download_resume(uint32_t package_size);
esp_err_t firmware_store_download_write(uint32_t package_offset,
                                        const void *buffer,
                                        size_t length);
esp_err_t firmware_store_download_read(uint32_t package_offset,
                                       void *buffer,
                                       size_t length);
esp_err_t firmware_store_download_commit(void);
bool firmware_store_is_ready(void);
const gateway_firmware_manifest_t *firmware_store_manifest(void);
const esp_partition_t *firmware_store_partition(void);

#ifdef __cplusplus
}
#endif

#endif /* FIRMWARE_STORE_H */
