#include "boot_metadata.h"

#include <stddef.h>

#include "stm32f4xx_hal.h"

#define BOOT_METADATA_INVALID_ADDRESS 0xFFFFFFFFUL
#define BOOT_METADATA_INVALID_SLOT    0xFFFFFFFFUL
#define BOOT_METADATA_CLEAR_FLAGS     (FLASH_FLAG_EOP    | FLASH_FLAG_OPERR | \
                                       FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | \
                                       FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR)
#define BOOT_METADATA_CRC32_POLYNOMIAL 0xEDB88320UL

typedef struct
{
    bool has_valid_record;
    bool has_used_slot;
    uint32_t first_erased_slot;
    uint32_t free_record_count;
    boot_metadata_record_t latest_record;
} boot_metadata_scan_t;

static uint32_t s_last_hal_error;
static uint32_t s_last_failure_address = BOOT_METADATA_INVALID_ADDRESS;

static void boot_metadata_reset_diagnostics(void)
{
    s_last_hal_error = HAL_FLASH_ERROR_NONE;
    s_last_failure_address = BOOT_METADATA_INVALID_ADDRESS;
}

static uint32_t boot_metadata_slot_address(uint32_t slot)
{
    return FLASH_LAYOUT_METADATA_BASE_ADDR +
           (slot * BOOT_METADATA_RECORD_SIZE);
}

static void boot_metadata_read_slot(uint32_t slot,
                                    boot_metadata_record_t *record)
{
    volatile const uint32_t *source;
    uint32_t *destination;
    uint32_t word_index;

    source = (volatile const uint32_t *)boot_metadata_slot_address(slot);
    destination = (uint32_t *)record;

    for (word_index = 0U;
         word_index < (BOOT_METADATA_RECORD_SIZE / sizeof(uint32_t));
         word_index++)
    {
        destination[word_index] = source[word_index];
    }
}

static bool boot_metadata_slot_is_erased(uint32_t slot)
{
    volatile const uint32_t *words;
    uint32_t word_index;

    words = (volatile const uint32_t *)boot_metadata_slot_address(slot);
    for (word_index = 0U;
         word_index < (BOOT_METADATA_RECORD_SIZE / sizeof(uint32_t));
         word_index++)
    {
        if (words[word_index] != 0xFFFFFFFFUL)
        {
            return false;
        }
    }

    return true;
}

static uint32_t boot_metadata_crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc;
    uint32_t byte_index;
    uint32_t bit_index;

    crc = 0xFFFFFFFFUL;
    for (byte_index = 0U; byte_index < length; byte_index++)
    {
        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            if ((crc & 1UL) != 0U)
            {
                crc = (crc >> 1U) ^ BOOT_METADATA_CRC32_POLYNOMIAL;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return ~crc;
}

static bool boot_metadata_state_is_valid(uint16_t state)
{
    return state <= (uint16_t)BOOT_STATE_FAILED;
}

static bool boot_metadata_payload_is_valid(
    const boot_metadata_record_t *record)
{
    if (!boot_metadata_state_is_valid(record->state))
    {
        return false;
    }

    if (record->image_size > FLASH_LAYOUT_APP_MAX_SIZE)
    {
        return false;
    }

    if (record->received_bytes > record->image_size)
    {
        return false;
    }

    return true;
}

bool boot_metadata_is_record_valid(const boot_metadata_record_t *record)
{
    uint32_t expected_crc;

    if (record == NULL)
    {
        return false;
    }

    if ((record->magic != BOOT_METADATA_RECORD_MAGIC) ||
        (record->format_version != BOOT_METADATA_FORMAT_VERSION) ||
        (record->commit_marker != BOOT_METADATA_COMMIT_MARKER) ||
        (record->sequence_number == 0U) ||
        (record->sequence_number == 0xFFFFFFFFUL) ||
        (!boot_metadata_payload_is_valid(record)))
    {
        return false;
    }

    expected_crc = boot_metadata_crc32(
        (const uint8_t *)record,
        (uint32_t)offsetof(boot_metadata_record_t, record_crc32));

    return record->record_crc32 == expected_crc;
}

static void boot_metadata_scan(boot_metadata_scan_t *scan)
{
    boot_metadata_record_t candidate;
    uint32_t slot;

    scan->has_valid_record = false;
    scan->has_used_slot = false;
    scan->first_erased_slot = BOOT_METADATA_INVALID_SLOT;
    scan->free_record_count = 0U;

    for (slot = 0U; slot < BOOT_METADATA_RECORD_CAPACITY; slot++)
    {
        if (boot_metadata_slot_is_erased(slot))
        {
            if (scan->first_erased_slot == BOOT_METADATA_INVALID_SLOT)
            {
                scan->first_erased_slot = slot;
            }
            scan->free_record_count++;
            continue;
        }

        scan->has_used_slot = true;
        boot_metadata_read_slot(slot, &candidate);

        if (boot_metadata_is_record_valid(&candidate) &&
            ((!scan->has_valid_record) ||
             (candidate.sequence_number >
              scan->latest_record.sequence_number)))
        {
            scan->latest_record = candidate;
            scan->has_valid_record = true;
        }
    }
}

static boot_metadata_status_t boot_metadata_finish_unlocked_operation(
    boot_metadata_status_t operation_status)
{
    HAL_StatusTypeDef lock_status;

    lock_status = HAL_FLASH_Lock();
    if ((lock_status != HAL_OK) &&
        (operation_status == BOOT_METADATA_OK))
    {
        s_last_hal_error = HAL_FLASH_GetError();
        return BOOT_METADATA_LOCK_ERROR;
    }

    return operation_status;
}

static boot_metadata_status_t boot_metadata_write_slot(
    uint32_t slot,
    const boot_metadata_record_t *record)
{
    HAL_StatusTypeDef hal_status;
    boot_metadata_status_t status;
    const uint32_t *words;
    uint32_t address;
    uint32_t word_index;
    uint32_t commit_word_index;

    boot_metadata_reset_diagnostics();

    if ((record == NULL) || (slot >= BOOT_METADATA_RECORD_CAPACITY))
    {
        return BOOT_METADATA_INVALID_ARGUMENT;
    }

    if (!boot_metadata_slot_is_erased(slot))
    {
        return BOOT_METADATA_INVALID_RECORD;
    }

    hal_status = HAL_FLASH_Unlock();
    if (hal_status != HAL_OK)
    {
        s_last_hal_error = HAL_FLASH_GetError();
        return BOOT_METADATA_UNLOCK_ERROR;
    }

    __HAL_FLASH_CLEAR_FLAG(BOOT_METADATA_CLEAR_FLAGS);
    status = BOOT_METADATA_OK;
    words = (const uint32_t *)record;
    address = boot_metadata_slot_address(slot);
    commit_word_index =
        (uint32_t)(offsetof(boot_metadata_record_t, commit_marker) /
                   sizeof(uint32_t));

    /* Program the payload and CRC first. The commit marker is the last word. */
    for (word_index = 0U; word_index < commit_word_index; word_index++)
    {
        hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                       address,
                                       (uint64_t)words[word_index]);
        if (hal_status != HAL_OK)
        {
            s_last_hal_error = HAL_FLASH_GetError();
            s_last_failure_address = address;
            status = BOOT_METADATA_PROGRAM_ERROR;
            break;
        }

        if (*(volatile uint32_t *)address != words[word_index])
        {
            s_last_failure_address = address;
            status = BOOT_METADATA_VERIFY_ERROR;
            break;
        }

        address += sizeof(uint32_t);
    }

    if (status == BOOT_METADATA_OK)
    {
        hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                       address,
                                       (uint64_t)record->commit_marker);
        if (hal_status != HAL_OK)
        {
            s_last_hal_error = HAL_FLASH_GetError();
            s_last_failure_address = address;
            status = BOOT_METADATA_PROGRAM_ERROR;
        }
        else if (*(volatile uint32_t *)address != record->commit_marker)
        {
            s_last_failure_address = address;
            status = BOOT_METADATA_VERIFY_ERROR;
        }
    }

    return boot_metadata_finish_unlocked_operation(status);
}

static boot_metadata_status_t boot_metadata_erase_sector(void)
{
    FLASH_EraseInitTypeDef erase_init;
    HAL_StatusTypeDef hal_status;
    boot_metadata_status_t status;
    uint32_t sector_error;

    boot_metadata_reset_diagnostics();

    hal_status = HAL_FLASH_Unlock();
    if (hal_status != HAL_OK)
    {
        s_last_hal_error = HAL_FLASH_GetError();
        return BOOT_METADATA_UNLOCK_ERROR;
    }

    __HAL_FLASH_CLEAR_FLAG(BOOT_METADATA_CLEAR_FLAGS);

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks = FLASH_BANK_1;
    erase_init.Sector = FLASH_LAYOUT_METADATA_SECTOR;
    erase_init.NbSectors = 1U;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    sector_error = BOOT_METADATA_INVALID_ADDRESS;
    status = BOOT_METADATA_OK;

    hal_status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    if (hal_status != HAL_OK)
    {
        s_last_hal_error = HAL_FLASH_GetError();
        s_last_failure_address = FLASH_LAYOUT_METADATA_BASE_ADDR;
        status = BOOT_METADATA_ERASE_ERROR;
    }

    return boot_metadata_finish_unlocked_operation(status);
}

static boot_metadata_status_t boot_metadata_compact_latest(
    const boot_metadata_record_t *latest_record)
{
    boot_metadata_status_t status;

    if ((latest_record == NULL) ||
        (!boot_metadata_is_record_valid(latest_record)))
    {
        return BOOT_METADATA_INVALID_RECORD;
    }

    if (!boot_metadata_state_is_safe_to_compact(
            (boot_state_t)latest_record->state))
    {
        return BOOT_METADATA_UNSAFE_STATE;
    }

    status = boot_metadata_erase_sector();
    if (status != BOOT_METADATA_OK)
    {
        return status;
    }

    return boot_metadata_write_slot(0U, latest_record);
}

void boot_metadata_record_init(boot_metadata_record_t *record,
                               boot_state_t state)
{
    uint32_t *words;
    uint32_t word_index;

    if (record == NULL)
    {
        return;
    }

    words = (uint32_t *)record;
    for (word_index = 0U;
         word_index < (BOOT_METADATA_RECORD_SIZE / sizeof(uint32_t));
         word_index++)
    {
        words[word_index] = 0U;
    }

    record->state = (uint16_t)state;
}

bool boot_metadata_state_allows_app_boot(boot_state_t state)
{
    return (state == BOOT_STATE_EMPTY) ||
           (state == BOOT_STATE_APP_VALID) ||
           (state == BOOT_STATE_PENDING_BOOT) ||
           (state == BOOT_STATE_CONFIRMED);
}

bool boot_metadata_state_is_safe_to_compact(boot_state_t state)
{
    return (state == BOOT_STATE_EMPTY) ||
           (state == BOOT_STATE_APP_VALID) ||
           (state == BOOT_STATE_CONFIRMED);
}

boot_metadata_status_t boot_metadata_load_latest(
    boot_metadata_record_t *record)
{
    boot_metadata_scan_t scan;

    if (record == NULL)
    {
        return BOOT_METADATA_INVALID_ARGUMENT;
    }

    boot_metadata_scan(&scan);
    if (scan.has_valid_record)
    {
        *record = scan.latest_record;
        return BOOT_METADATA_OK;
    }

    return scan.has_used_slot ? BOOT_METADATA_CORRUPT :
                                BOOT_METADATA_EMPTY;
}

boot_metadata_status_t boot_metadata_append(
    const boot_metadata_record_t *desired_record,
    boot_metadata_record_t *written_record)
{
    boot_metadata_record_t prepared_record;
    boot_metadata_scan_t scan;
    boot_metadata_status_t status;
    uint32_t sequence_number;

    if ((desired_record == NULL) ||
        (!boot_metadata_payload_is_valid(desired_record)))
    {
        return BOOT_METADATA_INVALID_ARGUMENT;
    }

    boot_metadata_scan(&scan);
    if (scan.has_valid_record)
    {
        if (scan.latest_record.sequence_number >= 0xFFFFFFFEUL)
        {
            return BOOT_METADATA_SEQUENCE_EXHAUSTED;
        }
        sequence_number = scan.latest_record.sequence_number + 1U;
    }
    else
    {
        if (scan.has_used_slot)
        {
            return BOOT_METADATA_CORRUPT;
        }
        sequence_number = 1U;
    }

    if (scan.first_erased_slot == BOOT_METADATA_INVALID_SLOT)
    {
        if (!scan.has_valid_record)
        {
            return BOOT_METADATA_FULL;
        }

        status = boot_metadata_compact_latest(&scan.latest_record);
        if (status != BOOT_METADATA_OK)
        {
            return status;
        }
        scan.first_erased_slot = 1U;
    }

    prepared_record = *desired_record;
    prepared_record.magic = BOOT_METADATA_RECORD_MAGIC;
    prepared_record.format_version = BOOT_METADATA_FORMAT_VERSION;
    prepared_record.sequence_number = sequence_number;
    prepared_record.record_crc32 = boot_metadata_crc32(
        (const uint8_t *)&prepared_record,
        (uint32_t)offsetof(boot_metadata_record_t, record_crc32));
    prepared_record.commit_marker = BOOT_METADATA_COMMIT_MARKER;

    status = boot_metadata_write_slot(scan.first_erased_slot,
                                      &prepared_record);
    if ((status == BOOT_METADATA_OK) && (written_record != NULL))
    {
        *written_record = prepared_record;
    }

    return status;
}

boot_metadata_status_t boot_metadata_get_free_record_count(
    uint32_t *free_record_count)
{
    boot_metadata_scan_t scan;

    if (free_record_count == NULL)
    {
        return BOOT_METADATA_INVALID_ARGUMENT;
    }

    boot_metadata_scan(&scan);
    *free_record_count = scan.free_record_count;

    if ((!scan.has_valid_record) && scan.has_used_slot)
    {
        return BOOT_METADATA_CORRUPT;
    }

    return scan.has_valid_record ? BOOT_METADATA_OK :
                                   BOOT_METADATA_EMPTY;
}

boot_metadata_status_t boot_metadata_compact_if_needed(
    uint32_t required_free_records)
{
    boot_metadata_scan_t scan;

    if ((required_free_records == 0U) ||
        (required_free_records > (BOOT_METADATA_RECORD_CAPACITY - 1U)))
    {
        return BOOT_METADATA_INVALID_ARGUMENT;
    }

    boot_metadata_scan(&scan);
    if (scan.free_record_count >= required_free_records)
    {
        return scan.has_valid_record ? BOOT_METADATA_OK :
               (scan.has_used_slot ? BOOT_METADATA_CORRUPT :
                                     BOOT_METADATA_EMPTY);
    }

    if (!scan.has_valid_record)
    {
        return scan.has_used_slot ? BOOT_METADATA_CORRUPT :
                                    BOOT_METADATA_FULL;
    }

    return boot_metadata_compact_latest(&scan.latest_record);
}

uint32_t boot_metadata_get_last_hal_error(void)
{
    return s_last_hal_error;
}

uint32_t boot_metadata_get_last_failure_address(void)
{
    return s_last_failure_address;
}
