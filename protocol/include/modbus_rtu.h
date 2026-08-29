#ifndef MODBUS_RTU_H
#define MODBUS_RTU_H

#include <stddef.h>
#include <stdint.h>

#include "protocol_status.h"

#define MODBUS_RTU_MIN_ADU_SIZE       4U
#define MODBUS_RTU_MAX_ADU_SIZE       256U
#define MODBUS_RTU_MAX_DATA_SIZE      252U
#define MODBUS_RTU_MIN_SLAVE_ADDRESS  1U
#define MODBUS_RTU_MAX_SLAVE_ADDRESS  247U
#define MODBUS_RTU_EXCEPTION_MASK     0x80U

typedef enum
{
    MODBUS_EXCEPTION_ILLEGAL_FUNCTION = 0x01,
    MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS = 0x02,
    MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE = 0x03,
    MODBUS_EXCEPTION_SERVER_DEVICE_FAILURE = 0x04,
    MODBUS_EXCEPTION_SERVER_DEVICE_BUSY = 0x06
} modbus_exception_code_t;

typedef struct
{
    uint8_t address;
    uint8_t function;
    size_t data_length;
    uint8_t data[MODBUS_RTU_MAX_DATA_SIZE];
} modbus_rtu_frame_t;

protocol_status_t modbus_rtu_encode(const modbus_rtu_frame_t *frame,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *output_length);

protocol_status_t modbus_rtu_decode(const uint8_t *input,
                                    size_t input_length,
                                    modbus_rtu_frame_t *frame);

protocol_status_t modbus_rtu_encode_exception(
    uint8_t address,
    uint8_t request_function,
    modbus_exception_code_t exception_code,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length);

protocol_status_t modbus_rtu_decode_exception(
    const modbus_rtu_frame_t *frame,
    uint8_t request_function,
    modbus_exception_code_t *exception_code);

#endif /* MODBUS_RTU_H */
