#include "flash_if.h"

#include "flash_layout.h"
#include "stm32f4xx_hal.h"

#define FLASH_IF_INVALID_ADDRESS 0xFFFFFFFFUL
#define FLASH_IF_CLEAR_FLAGS     (FLASH_FLAG_EOP    | FLASH_FLAG_OPERR | \
                                  FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | \
                                  FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR)

static uint32_t s_last_hal_error;
static uint32_t s_last_failure_address = FLASH_IF_INVALID_ADDRESS;

static void flash_if_reset_diagnostics(void)
{
    s_last_hal_error = HAL_FLASH_ERROR_NONE;
    s_last_failure_address = FLASH_IF_INVALID_ADDRESS;
}

static uint32_t flash_if_sector_start_address(uint32_t sector)
{
    if ((sector < FLASH_LAYOUT_APP_FIRST_SECTOR) ||
        (sector > FLASH_LAYOUT_APP_LAST_SECTOR))
    {
        return FLASH_IF_INVALID_ADDRESS;
    }

    return FLASH_LAYOUT_APP_BASE_ADDR +
           ((sector - FLASH_LAYOUT_APP_FIRST_SECTOR) *
            FLASH_LAYOUT_APP_SECTOR_SIZE);
}

static flash_if_status_t flash_if_finish_unlocked_operation(
    flash_if_status_t operation_status)
{
    HAL_StatusTypeDef lock_status;

    lock_status = HAL_FLASH_Lock();
    if ((lock_status != HAL_OK) && (operation_status == FLASH_IF_OK))
    {
        s_last_hal_error = HAL_FLASH_GetError();
        return FLASH_IF_LOCK_ERROR;
    }

    return operation_status;
}

bool flash_if_is_app_range(uint32_t offset, uint32_t length)
{
    if ((length == 0U) || (offset >= FLASH_LAYOUT_APP_MAX_SIZE))
    {
        return false;
    }

    /* Subtraction is used deliberately so offset + length cannot overflow. */
    if (length > (FLASH_LAYOUT_APP_MAX_SIZE - offset))
    {
        return false;
    }

    return true;
}

flash_if_status_t flash_if_get_erase_plan(uint32_t image_size,
                                          uint32_t *first_sector,
                                          uint32_t *sector_count)
{
    if ((first_sector == NULL) || (sector_count == NULL) ||
        (image_size == 0U))
    {
        return FLASH_IF_INVALID_ARGUMENT;
    }

    if (image_size > FLASH_LAYOUT_APP_MAX_SIZE)
    {
        return FLASH_IF_OUT_OF_RANGE;
    }

    *first_sector = FLASH_LAYOUT_APP_FIRST_SECTOR;
    *sector_count = 1U +
                    ((image_size - 1U) / FLASH_LAYOUT_APP_SECTOR_SIZE);

    if (*sector_count > FLASH_LAYOUT_APP_SECTOR_COUNT)
    {
        return FLASH_IF_INTERNAL_ERROR;
    }

    return FLASH_IF_OK;
}

flash_if_status_t flash_if_erase_app(uint32_t image_size)
{
    FLASH_EraseInitTypeDef erase_init;
    HAL_StatusTypeDef hal_status;
    flash_if_status_t status;
    uint32_t first_sector;
    uint32_t sector_count;
    uint32_t sector_error;

    flash_if_reset_diagnostics();

    status = flash_if_get_erase_plan(image_size,
                                     &first_sector,
                                     &sector_count);
    if (status != FLASH_IF_OK)
    {
        return status;
    }

    hal_status = HAL_FLASH_Unlock();
    if (hal_status != HAL_OK)
    {
        s_last_hal_error = HAL_FLASH_GetError();
        return FLASH_IF_UNLOCK_ERROR;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_IF_CLEAR_FLAGS);

    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks = FLASH_BANK_1;
    erase_init.Sector = first_sector;
    erase_init.NbSectors = sector_count;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    sector_error = FLASH_IF_INVALID_ADDRESS;

    hal_status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    if (hal_status != HAL_OK)
    {
        s_last_hal_error = HAL_FLASH_GetError();
        s_last_failure_address = flash_if_sector_start_address(sector_error);
        status = FLASH_IF_ERASE_ERROR;
    }

    return flash_if_finish_unlocked_operation(status);
}

flash_if_status_t flash_if_write_app(uint32_t offset,
                                     const uint8_t *data,
                                     uint32_t length,
                                     bool final_chunk)
{
    HAL_StatusTypeDef hal_status;
    flash_if_status_t status;
    uint32_t address;
    uint32_t data_index;
    uint32_t byte_index;
    uint32_t programmed_length;
    uint32_t word;

    flash_if_reset_diagnostics();

    if (data == NULL)
    {
        return FLASH_IF_INVALID_ARGUMENT;
    }

    if (!flash_if_is_app_range(offset, length))
    {
        return (length == 0U) ? FLASH_IF_INVALID_ARGUMENT :
                               FLASH_IF_OUT_OF_RANGE;
    }

    if ((offset % FLASH_LAYOUT_PROGRAM_WORD_SIZE) != 0U)
    {
        return FLASH_IF_ALIGNMENT_ERROR;
    }

    if ((!final_chunk) &&
        ((length % FLASH_LAYOUT_PROGRAM_WORD_SIZE) != 0U))
    {
        return FLASH_IF_ALIGNMENT_ERROR;
    }

    programmed_length = (length + (FLASH_LAYOUT_PROGRAM_WORD_SIZE - 1U)) &
                        ~(FLASH_LAYOUT_PROGRAM_WORD_SIZE - 1U);
    if (programmed_length > (FLASH_LAYOUT_APP_MAX_SIZE - offset))
    {
        return FLASH_IF_OUT_OF_RANGE;
    }

    hal_status = HAL_FLASH_Unlock();
    if (hal_status != HAL_OK)
    {
        s_last_hal_error = HAL_FLASH_GetError();
        return FLASH_IF_UNLOCK_ERROR;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_IF_CLEAR_FLAGS);
    status = FLASH_IF_OK;
    address = FLASH_LAYOUT_APP_BASE_ADDR + offset;
    data_index = 0U;

    while (data_index < length)
    {
        word = 0xFFFFFFFFUL;

        for (byte_index = 0U;
             (byte_index < FLASH_LAYOUT_PROGRAM_WORD_SIZE) &&
             (data_index < length);
             byte_index++, data_index++)
        {
            word &= ~(0xFFUL << (byte_index * 8U));
            word |= ((uint32_t)data[data_index]) << (byte_index * 8U);
        }

        hal_status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                                       address,
                                       (uint64_t)word);
        if (hal_status != HAL_OK)
        {
            s_last_hal_error = HAL_FLASH_GetError();
            s_last_failure_address = address;
            status = FLASH_IF_PROGRAM_ERROR;
            break;
        }

        if (*(volatile uint32_t *)address != word)
        {
            s_last_failure_address = address;
            status = FLASH_IF_VERIFY_ERROR;
            break;
        }

        address += FLASH_LAYOUT_PROGRAM_WORD_SIZE;
    }

    return flash_if_finish_unlocked_operation(status);
}

flash_if_status_t flash_if_verify_app(uint32_t offset,
                                      const uint8_t *data,
                                      uint32_t length)
{
    volatile const uint8_t *flash_data;
    uint32_t index;

    flash_if_reset_diagnostics();

    if (data == NULL)
    {
        return FLASH_IF_INVALID_ARGUMENT;
    }

    if (!flash_if_is_app_range(offset, length))
    {
        return (length == 0U) ? FLASH_IF_INVALID_ARGUMENT :
                               FLASH_IF_OUT_OF_RANGE;
    }

    flash_data = (volatile const uint8_t *)
                 (FLASH_LAYOUT_APP_BASE_ADDR + offset);

    for (index = 0U; index < length; index++)
    {
        if (flash_data[index] != data[index])
        {
            s_last_failure_address = FLASH_LAYOUT_APP_BASE_ADDR +
                                     offset + index;
            return FLASH_IF_VERIFY_ERROR;
        }
    }

    return FLASH_IF_OK;
}

uint32_t flash_if_get_last_hal_error(void)
{
    return s_last_hal_error;
}

uint32_t flash_if_get_last_failure_address(void)
{
    return s_last_failure_address;
}
