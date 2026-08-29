#ifndef FLASH_IF_H
#define FLASH_IF_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    FLASH_IF_OK = 0,
    FLASH_IF_INVALID_ARGUMENT,
    FLASH_IF_OUT_OF_RANGE,
    FLASH_IF_ALIGNMENT_ERROR,
    FLASH_IF_UNLOCK_ERROR,
    FLASH_IF_ERASE_ERROR,
    FLASH_IF_PROGRAM_ERROR,
    FLASH_IF_VERIFY_ERROR,
    FLASH_IF_LOCK_ERROR,
    FLASH_IF_INTERNAL_ERROR
} flash_if_status_t;

/*
 * All offsets are relative to FLASH_LAYOUT_APP_BASE_ADDR. Absolute addresses
 * are intentionally not accepted by this public interface.
 */
bool flash_if_is_app_range(uint32_t offset, uint32_t length);

flash_if_status_t flash_if_get_erase_plan(uint32_t image_size,
                                          uint32_t *first_sector,
                                          uint32_t *sector_count);

flash_if_status_t flash_if_erase_app(uint32_t image_size);

/*
 * offset must be 4-byte aligned. Non-final chunks must also have a length that
 * is a multiple of four. Only the final chunk may be padded internally with
 * erased bytes (0xFF) to form its last 32-bit Flash word.
 */
flash_if_status_t flash_if_write_app(uint32_t offset,
                                     const uint8_t *data,
                                     uint32_t length,
                                     bool final_chunk);

flash_if_status_t flash_if_verify_app(uint32_t offset,
                                      const uint8_t *data,
                                      uint32_t length);

uint32_t flash_if_get_last_hal_error(void);
uint32_t flash_if_get_last_failure_address(void);

#endif /* FLASH_IF_H */
