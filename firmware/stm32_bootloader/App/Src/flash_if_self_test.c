#include "flash_if_self_test.h"

#include <stddef.h>

#include "flash_layout.h"

#define FLASH_IF_SELF_TEST_LENGTH 256U
#define FLASH_IF_SELF_TEST_TAIL_LENGTH 3U
#define FLASH_IF_FNV1A_OFFSET_BASIS 2166136261UL
#define FLASH_IF_FNV1A_PRIME 16777619UL

static uint32_t flash_if_self_test_protected_hash(void)
{
    volatile const uint8_t *flash_data;
    uint32_t hash;
    uint32_t index;

    flash_data = (volatile const uint8_t *)FLASH_LAYOUT_FLASH_BASE_ADDR;
    hash = FLASH_IF_FNV1A_OFFSET_BASIS;

    for (index = 0U;
         index < (FLASH_LAYOUT_APP_BASE_ADDR -
                  FLASH_LAYOUT_FLASH_BASE_ADDR);
         index++)
    {
        hash ^= flash_data[index];
        hash *= FLASH_IF_FNV1A_PRIME;
    }

    return hash;
}

static bool flash_if_self_test_plan_matches(uint32_t image_size,
                                            uint32_t expected_sector_count)
{
    flash_if_status_t status;
    uint32_t first_sector;
    uint32_t sector_count;

    status = flash_if_get_erase_plan(image_size,
                                     &first_sector,
                                     &sector_count);

    return (status == FLASH_IF_OK) &&
           (first_sector == FLASH_LAYOUT_APP_FIRST_SECTOR) &&
           (sector_count == expected_sector_count);
}

static bool flash_if_self_test_guards_reject(void)
{
    uint8_t data[4] = {0x12U, 0x34U, 0x56U, 0x78U};
    uint32_t first_sector;
    uint32_t sector_count;

    return (!flash_if_is_app_range(0U, 0U)) &&
           (!flash_if_is_app_range(FLASH_LAYOUT_APP_MAX_SIZE, 1U)) &&
           (!flash_if_is_app_range(0xFFFFFFFCUL, 8U)) &&
           (flash_if_get_erase_plan(0U,
                                    &first_sector,
                                    &sector_count) ==
            FLASH_IF_INVALID_ARGUMENT) &&
           (flash_if_get_erase_plan(FLASH_LAYOUT_APP_MAX_SIZE + 1U,
                                    &first_sector,
                                    &sector_count) ==
            FLASH_IF_OUT_OF_RANGE) &&
           (flash_if_write_app(1U, data, 4U, false) ==
            FLASH_IF_ALIGNMENT_ERROR) &&
           (flash_if_write_app(0U, data, 3U, false) ==
            FLASH_IF_ALIGNMENT_ERROR) &&
           (flash_if_write_app(FLASH_LAYOUT_APP_MAX_SIZE,
                               data,
                               4U,
                               false) == FLASH_IF_OUT_OF_RANGE) &&
           (flash_if_write_app(0U, NULL, 4U, false) ==
            FLASH_IF_INVALID_ARGUMENT);
}

flash_if_status_t flash_if_self_test_run(void)
{
    flash_if_status_t status;
    uint8_t pattern[FLASH_IF_SELF_TEST_LENGTH];
    uint32_t index;
    uint32_t protected_hash;

    /* Check boundary math before performing the destructive board test. */
    if ((!flash_if_self_test_plan_matches(1U, 1U)) ||
        (!flash_if_self_test_plan_matches(FLASH_LAYOUT_APP_SECTOR_SIZE, 1U)) ||
        (!flash_if_self_test_plan_matches(FLASH_LAYOUT_APP_SECTOR_SIZE + 1U,
                                          2U)) ||
        (!flash_if_self_test_plan_matches(FLASH_LAYOUT_APP_MAX_SIZE,
                                          FLASH_LAYOUT_APP_SECTOR_COUNT)) ||
        (!flash_if_is_app_range(FLASH_LAYOUT_APP_MAX_SIZE - 1U, 1U)) ||
        (!flash_if_self_test_guards_reject()))
    {
        return FLASH_IF_INTERNAL_ERROR;
    }

    protected_hash = flash_if_self_test_protected_hash();

    for (index = 0U; index < FLASH_IF_SELF_TEST_LENGTH; index++)
    {
        pattern[index] = (uint8_t)((index * 37U) + 0x5AU);
    }

    status = flash_if_erase_app(FLASH_IF_SELF_TEST_LENGTH);
    if (status != FLASH_IF_OK)
    {
        return status;
    }

    status = flash_if_write_app(0U,
                                pattern,
                                FLASH_IF_SELF_TEST_LENGTH,
                                true);
    if (status != FLASH_IF_OK)
    {
        return status;
    }

    status = flash_if_verify_app(0U,
                                 pattern,
                                 FLASH_IF_SELF_TEST_LENGTH);
    if (status != FLASH_IF_OK)
    {
        return status;
    }

    /* Exercise final-word 0xFF padding without overlapping the first block. */
    status = flash_if_write_app(FLASH_IF_SELF_TEST_LENGTH,
                                pattern,
                                FLASH_IF_SELF_TEST_TAIL_LENGTH,
                                true);
    if (status != FLASH_IF_OK)
    {
        return status;
    }

    status = flash_if_verify_app(FLASH_IF_SELF_TEST_LENGTH,
                                 pattern,
                                 FLASH_IF_SELF_TEST_TAIL_LENGTH);
    if (status != FLASH_IF_OK)
    {
        return status;
    }

    if (flash_if_self_test_protected_hash() != protected_hash)
    {
        return FLASH_IF_INTERNAL_ERROR;
    }

    return FLASH_IF_OK;
}
