#ifndef BOOT_METADATA_H
#define BOOT_METADATA_H

#include <stdbool.h>
#include <stdint.h>

#include "flash_layout.h"

#define BOOT_METADATA_RECORD_MAGIC          0x42544D44UL
#define BOOT_METADATA_COMMIT_MARKER          0x434D4954UL
#define BOOT_METADATA_FORMAT_VERSION         1U
#define BOOT_METADATA_SHA256_SIZE            32U
#define BOOT_METADATA_RECORD_SIZE            76UL
#define BOOT_METADATA_RECORD_CAPACITY        \
    (FLASH_LAYOUT_METADATA_MAX_SIZE / BOOT_METADATA_RECORD_SIZE)

typedef enum
{
    BOOT_STATE_EMPTY = 0,
    BOOT_STATE_APP_VALID,
    BOOT_STATE_UPDATE_REQUESTED,
    BOOT_STATE_ERASING,
    BOOT_STATE_RECEIVING,
    BOOT_STATE_VERIFYING,
    BOOT_STATE_PENDING_BOOT,
    BOOT_STATE_CONFIRMED,
    BOOT_STATE_FAILED
} boot_state_t;

/*
 * The field order is the persistent on-Flash format. All fields are naturally
 * 32-bit aligned and the compile-time size check below guards layout drift.
 * commit_marker is programmed last so an interrupted append is never valid.
 */
typedef struct
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t state;
    uint32_t sequence_number;
    uint32_t session_id;
    uint32_t firmware_version;
    uint32_t image_size;
    uint32_t received_bytes;
    uint32_t image_crc32;
    uint8_t image_sha256[BOOT_METADATA_SHA256_SIZE];
    uint32_t error_code;
    uint32_t record_crc32;
    uint32_t commit_marker;
} boot_metadata_record_t;

typedef char boot_metadata_record_size_must_be_76_bytes[
    (sizeof(boot_metadata_record_t) == BOOT_METADATA_RECORD_SIZE) ? 1 : -1];

typedef enum
{
    BOOT_METADATA_OK = 0,
    BOOT_METADATA_EMPTY,
    BOOT_METADATA_INVALID_ARGUMENT,
    BOOT_METADATA_INVALID_RECORD,
    BOOT_METADATA_CORRUPT,
    BOOT_METADATA_FULL,
    BOOT_METADATA_UNSAFE_STATE,
    BOOT_METADATA_SEQUENCE_EXHAUSTED,
    BOOT_METADATA_UNLOCK_ERROR,
    BOOT_METADATA_ERASE_ERROR,
    BOOT_METADATA_PROGRAM_ERROR,
    BOOT_METADATA_VERIFY_ERROR,
    BOOT_METADATA_LOCK_ERROR,
    BOOT_METADATA_INTERNAL_ERROR
} boot_metadata_status_t;

void boot_metadata_record_init(boot_metadata_record_t *record,
                               boot_state_t state);

bool boot_metadata_is_record_valid(const boot_metadata_record_t *record);
bool boot_metadata_state_allows_app_boot(boot_state_t state);
bool boot_metadata_state_is_safe_to_compact(boot_state_t state);

boot_metadata_status_t boot_metadata_load_latest(
    boot_metadata_record_t *record);

boot_metadata_status_t boot_metadata_append(
    const boot_metadata_record_t *desired_record,
    boot_metadata_record_t *written_record);

boot_metadata_status_t boot_metadata_get_free_record_count(
    uint32_t *free_record_count);

/*
 * Call this before START while the latest state is safe. It compacts only when
 * fewer than required_free_records erased slots remain. During an active
 * update it returns BOOT_METADATA_UNSAFE_STATE instead of erasing Sector 4.
 */
boot_metadata_status_t boot_metadata_compact_if_needed(
    uint32_t required_free_records);

uint32_t boot_metadata_get_last_hal_error(void);
uint32_t boot_metadata_get_last_failure_address(void);

#endif /* BOOT_METADATA_H */
