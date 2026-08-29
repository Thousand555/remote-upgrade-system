#include "boot_metadata_self_test.h"

#include <stddef.h>

#include "stm32f4xx_hal.h"

#define BOOT_METADATA_TEST_SESSION_ID       0x4D340001UL
#define BOOT_METADATA_TEST_FIRMWARE_VERSION 0x00010002UL
#define BOOT_METADATA_TEST_IMAGE_SIZE       8192UL
#define BOOT_METADATA_TEST_CHECKPOINT       4096UL
#define BOOT_METADATA_TEST_IMAGE_CRC32      0x12345678UL
#define BOOT_METADATA_TEST_FNV_OFFSET       2166136261UL
#define BOOT_METADATA_TEST_FNV_PRIME        16777619UL
#define BOOT_METADATA_TEST_CLEAR_FLAGS      \
    (FLASH_FLAG_EOP    | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | \
     FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR)

static uint32_t boot_metadata_self_test_protected_hash(void)
{
    volatile const uint8_t *flash_data;
    uint32_t address;
    uint32_t hash;

    flash_data = (volatile const uint8_t *)FLASH_LAYOUT_FLASH_BASE_ADDR;
    hash = BOOT_METADATA_TEST_FNV_OFFSET;

    for (address = FLASH_LAYOUT_FLASH_BASE_ADDR;
         address < FLASH_LAYOUT_METADATA_BASE_ADDR;
         address++)
    {
        hash ^= flash_data[address - FLASH_LAYOUT_FLASH_BASE_ADDR];
        hash *= BOOT_METADATA_TEST_FNV_PRIME;
    }

    for (address = FLASH_LAYOUT_APP_BASE_ADDR;
         address < FLASH_LAYOUT_FLASH_END_ADDR;
         address++)
    {
        hash ^= flash_data[address - FLASH_LAYOUT_FLASH_BASE_ADDR];
        hash *= BOOT_METADATA_TEST_FNV_PRIME;
    }

    return hash;
}

static boot_metadata_status_t boot_metadata_self_test_erase_sector(void)
{
    FLASH_EraseInitTypeDef erase_init;
    HAL_StatusTypeDef hal_status;
    uint32_t sector_error;

    hal_status = HAL_FLASH_Unlock();
    if (hal_status != HAL_OK)
    {
        return BOOT_METADATA_UNLOCK_ERROR;
    }

    __HAL_FLASH_CLEAR_FLAG(BOOT_METADATA_TEST_CLEAR_FLAGS);
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks = FLASH_BANK_1;
    erase_init.Sector = FLASH_LAYOUT_METADATA_SECTOR;
    erase_init.NbSectors = 1U;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    sector_error = 0xFFFFFFFFUL;

    hal_status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    if (hal_status != HAL_OK)
    {
        (void)HAL_FLASH_Lock();
        return BOOT_METADATA_ERASE_ERROR;
    }

    if (HAL_FLASH_Lock() != HAL_OK)
    {
        return BOOT_METADATA_LOCK_ERROR;
    }

    return BOOT_METADATA_OK;
}

static boot_metadata_status_t boot_metadata_self_test_write_incomplete_slot(
    uint32_t slot)
{
    HAL_StatusTypeDef hal_status;
    uint32_t slot_address;
    uint32_t commit_address;

    slot_address = FLASH_LAYOUT_METADATA_BASE_ADDR +
                   (slot * BOOT_METADATA_RECORD_SIZE);
    commit_address = slot_address +
                     (uint32_t)offsetof(boot_metadata_record_t,
                                        commit_marker);

    hal_status = HAL_FLASH_Unlock();
    if (hal_status != HAL_OK)
    {
        return BOOT_METADATA_UNLOCK_ERROR;
    }

    __HAL_FLASH_CLEAR_FLAG(BOOT_METADATA_TEST_CLEAR_FLAGS);
    hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                   slot_address,
                                   BOOT_METADATA_RECORD_MAGIC);
    if (hal_status == HAL_OK)
    {
        /* Commit with a missing payload deliberately creates an invalid slot. */
        hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                       commit_address,
                                       BOOT_METADATA_COMMIT_MARKER);
    }

    if (HAL_FLASH_Lock() != HAL_OK)
    {
        return BOOT_METADATA_LOCK_ERROR;
    }

    return (hal_status == HAL_OK) ? BOOT_METADATA_OK :
                                   BOOT_METADATA_PROGRAM_ERROR;
}

static bool boot_metadata_self_test_record_matches(
    const boot_metadata_record_t *record,
    boot_state_t state,
    uint32_t sequence,
    uint32_t received_bytes)
{
    return (record->state == (uint16_t)state) &&
           (record->sequence_number == sequence) &&
           (record->session_id == BOOT_METADATA_TEST_SESSION_ID) &&
           (record->firmware_version ==
            BOOT_METADATA_TEST_FIRMWARE_VERSION) &&
           (record->image_size == BOOT_METADATA_TEST_IMAGE_SIZE) &&
           (record->received_bytes == received_bytes) &&
           (record->image_crc32 == BOOT_METADATA_TEST_IMAGE_CRC32) &&
           boot_metadata_is_record_valid(record);
}

boot_metadata_status_t boot_metadata_self_test_run(void)
{
    boot_metadata_record_t desired;
    boot_metadata_record_t latest;
    boot_metadata_status_t status;
    uint32_t free_records;
    uint32_t protected_hash;

    protected_hash = boot_metadata_self_test_protected_hash();

    status = boot_metadata_self_test_erase_sector();
    if (status != BOOT_METADATA_OK)
    {
        return status;
    }

    status = boot_metadata_load_latest(&latest);
    if (status != BOOT_METADATA_EMPTY)
    {
        return BOOT_METADATA_INTERNAL_ERROR;
    }

    boot_metadata_record_init(&desired, BOOT_STATE_UPDATE_REQUESTED);
    desired.session_id = BOOT_METADATA_TEST_SESSION_ID;
    desired.firmware_version = BOOT_METADATA_TEST_FIRMWARE_VERSION;
    desired.image_size = BOOT_METADATA_TEST_IMAGE_SIZE;
    desired.image_crc32 = BOOT_METADATA_TEST_IMAGE_CRC32;

    status = boot_metadata_append(&desired, &latest);
    if ((status != BOOT_METADATA_OK) ||
        (!boot_metadata_self_test_record_matches(
            &latest, BOOT_STATE_UPDATE_REQUESTED, 1U, 0U)))
    {
        return (status == BOOT_METADATA_OK) ?
               BOOT_METADATA_INTERNAL_ERROR : status;
    }

    desired.state = (uint16_t)BOOT_STATE_RECEIVING;
    desired.received_bytes = BOOT_METADATA_TEST_CHECKPOINT;
    status = boot_metadata_append(&desired, &latest);
    if ((status != BOOT_METADATA_OK) ||
        (!boot_metadata_self_test_record_matches(
            &latest, BOOT_STATE_RECEIVING, 2U,
            BOOT_METADATA_TEST_CHECKPOINT)))
    {
        return (status == BOOT_METADATA_OK) ?
               BOOT_METADATA_INTERNAL_ERROR : status;
    }

    /* An active transfer must never trigger whole-sector compaction. */
    status = boot_metadata_compact_if_needed(
        BOOT_METADATA_RECORD_CAPACITY - 1U);
    if (status != BOOT_METADATA_UNSAFE_STATE)
    {
        return BOOT_METADATA_INTERNAL_ERROR;
    }

    /* Simulate a power-loss/torn record in slot 2. Scan must fall back to #2. */
    status = boot_metadata_self_test_write_incomplete_slot(2U);
    if (status != BOOT_METADATA_OK)
    {
        return status;
    }

    status = boot_metadata_load_latest(&latest);
    if ((status != BOOT_METADATA_OK) ||
        (!boot_metadata_self_test_record_matches(
            &latest, BOOT_STATE_RECEIVING, 2U,
            BOOT_METADATA_TEST_CHECKPOINT)))
    {
        return (status == BOOT_METADATA_OK) ?
               BOOT_METADATA_INTERNAL_ERROR : status;
    }

    desired.state = (uint16_t)BOOT_STATE_CONFIRMED;
    desired.received_bytes = BOOT_METADATA_TEST_IMAGE_SIZE;
    status = boot_metadata_append(&desired, &latest);
    if ((status != BOOT_METADATA_OK) ||
        (!boot_metadata_self_test_record_matches(
            &latest, BOOT_STATE_CONFIRMED, 3U,
            BOOT_METADATA_TEST_IMAGE_SIZE)))
    {
        return (status == BOOT_METADATA_OK) ?
               BOOT_METADATA_INTERNAL_ERROR : status;
    }

    /* Force the safe-state compaction branch without wearing all 862 slots. */
    status = boot_metadata_compact_if_needed(
        BOOT_METADATA_RECORD_CAPACITY - 1U);
    if (status != BOOT_METADATA_OK)
    {
        return status;
    }

    status = boot_metadata_load_latest(&latest);
    if ((status != BOOT_METADATA_OK) ||
        (!boot_metadata_self_test_record_matches(
            &latest, BOOT_STATE_CONFIRMED, 3U,
            BOOT_METADATA_TEST_IMAGE_SIZE)))
    {
        return (status == BOOT_METADATA_OK) ?
               BOOT_METADATA_INTERNAL_ERROR : status;
    }

    status = boot_metadata_get_free_record_count(&free_records);
    if ((status != BOOT_METADATA_OK) ||
        (free_records != (BOOT_METADATA_RECORD_CAPACITY - 1U)))
    {
        return BOOT_METADATA_INTERNAL_ERROR;
    }

    if (boot_metadata_self_test_protected_hash() != protected_hash)
    {
        return BOOT_METADATA_INTERNAL_ERROR;
    }

    return BOOT_METADATA_OK;
}
