#ifndef MODBUS_RTU_STREAM_H
#define MODBUS_RTU_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "modbus_rtu.h"

#define MODBUS_RTU_115200_T1_5_US 750UL
#define MODBUS_RTU_115200_T3_5_US 1750UL

typedef enum
{
    MODBUS_STREAM_NO_FRAME = 0,
    MODBUS_STREAM_FRAME_READY,
    MODBUS_STREAM_INTERCHAR_TIMEOUT,
    MODBUS_STREAM_OVERFLOW,
    MODBUS_STREAM_BUSY,
    MODBUS_STREAM_INVALID_ARGUMENT,
    MODBUS_STREAM_OUTPUT_TOO_SMALL
} modbus_stream_status_t;

typedef struct
{
    uint8_t current_frame[MODBUS_RTU_MAX_ADU_SIZE];
    size_t current_length;
    uint8_t completed_frame[MODBUS_RTU_MAX_ADU_SIZE];
    size_t completed_length;
    uint32_t last_byte_timestamp_us;
    uint32_t interchar_timeout_us;
    uint32_t frame_gap_us;
    bool receiving;
    bool frame_ready;
} modbus_rtu_stream_t;

modbus_stream_status_t modbus_rtu_stream_init(
    modbus_rtu_stream_t *stream,
    uint32_t interchar_timeout_us,
    uint32_t frame_gap_us);

void modbus_rtu_stream_reset(modbus_rtu_stream_t *stream);

/*
 * Timestamps are free-running uint32 microseconds; unsigned subtraction makes
 * one counter wrap safe. A byte passed when BUSY is returned is not consumed.
 */
modbus_stream_status_t modbus_rtu_stream_feed(
    modbus_rtu_stream_t *stream,
    uint8_t byte,
    uint32_t timestamp_us);

modbus_stream_status_t modbus_rtu_stream_poll(
    modbus_rtu_stream_t *stream,
    uint32_t timestamp_us);

modbus_stream_status_t modbus_rtu_stream_take_frame(
    modbus_rtu_stream_t *stream,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length);

#endif /* MODBUS_RTU_STREAM_H */
