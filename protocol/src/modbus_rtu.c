#include "modbus_rtu.h"

#include "crc16_modbus.h"

static protocol_status_t modbus_rtu_validate_address(uint8_t address)
{
    if ((address < MODBUS_RTU_MIN_SLAVE_ADDRESS) ||
        (address > MODBUS_RTU_MAX_SLAVE_ADDRESS))
    {
        return PROTOCOL_ADDRESS_ERROR;
    }

    return PROTOCOL_OK;
}

protocol_status_t modbus_rtu_encode(const modbus_rtu_frame_t *frame,
                                    uint8_t *output,
                                    size_t output_capacity,
                                    size_t *output_length)
{
    protocol_status_t status;
    uint16_t crc;
    size_t adu_length;
    size_t index;

    if ((frame == NULL) || (output == NULL) || (output_length == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    status = modbus_rtu_validate_address(frame->address);
    if (status != PROTOCOL_OK)
    {
        return status;
    }

    if (frame->function == 0U)
    {
        return PROTOCOL_FUNCTION_ERROR;
    }

    if (frame->data_length > MODBUS_RTU_MAX_DATA_SIZE)
    {
        return PROTOCOL_LENGTH_ERROR;
    }

    adu_length = frame->data_length + MODBUS_RTU_MIN_ADU_SIZE;
    if (output_capacity < adu_length)
    {
        return PROTOCOL_BUFFER_TOO_SMALL;
    }

    output[0] = frame->address;
    output[1] = frame->function;
    for (index = 0U; index < frame->data_length; index++)
    {
        output[2U + index] = frame->data[index];
    }

    crc = crc16_modbus_calculate(output, 2U + frame->data_length);
    output[2U + frame->data_length] = (uint8_t)(crc & 0xFFU);
    output[3U + frame->data_length] = (uint8_t)((crc >> 8U) & 0xFFU);
    *output_length = adu_length;

    return PROTOCOL_OK;
}

protocol_status_t modbus_rtu_decode(const uint8_t *input,
                                    size_t input_length,
                                    modbus_rtu_frame_t *frame)
{
    protocol_status_t status;
    uint16_t calculated_crc;
    uint16_t received_crc;
    size_t data_length;
    size_t index;

    if ((input == NULL) || (frame == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if ((input_length < MODBUS_RTU_MIN_ADU_SIZE) ||
        (input_length > MODBUS_RTU_MAX_ADU_SIZE))
    {
        return PROTOCOL_LENGTH_ERROR;
    }

    status = modbus_rtu_validate_address(input[0]);
    if (status != PROTOCOL_OK)
    {
        return status;
    }

    if (input[1] == 0U)
    {
        return PROTOCOL_FUNCTION_ERROR;
    }

    calculated_crc = crc16_modbus_calculate(input, input_length - 2U);
    received_crc = (uint16_t)((uint16_t)input[input_length - 2U] |
                              ((uint16_t)input[input_length - 1U] << 8U));
    if (calculated_crc != received_crc)
    {
        return PROTOCOL_CRC_ERROR;
    }

    data_length = input_length - MODBUS_RTU_MIN_ADU_SIZE;
    frame->address = input[0];
    frame->function = input[1];
    frame->data_length = data_length;

    for (index = 0U; index < data_length; index++)
    {
        frame->data[index] = input[2U + index];
    }

    return PROTOCOL_OK;
}

protocol_status_t modbus_rtu_encode_exception(
    uint8_t address,
    uint8_t request_function,
    modbus_exception_code_t exception_code,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    modbus_rtu_frame_t frame;

    if ((request_function == 0U) ||
        ((request_function & MODBUS_RTU_EXCEPTION_MASK) != 0U))
    {
        return PROTOCOL_FUNCTION_ERROR;
    }

    frame.address = address;
    frame.function = (uint8_t)(request_function |
                               MODBUS_RTU_EXCEPTION_MASK);
    frame.data_length = 1U;
    frame.data[0] = (uint8_t)exception_code;

    return modbus_rtu_encode(&frame,
                             output,
                             output_capacity,
                             output_length);
}

protocol_status_t modbus_rtu_decode_exception(
    const modbus_rtu_frame_t *frame,
    uint8_t request_function,
    modbus_exception_code_t *exception_code)
{
    if ((frame == NULL) || (exception_code == NULL))
    {
        return PROTOCOL_NULL_ARGUMENT;
    }

    if ((request_function == 0U) ||
        ((request_function & MODBUS_RTU_EXCEPTION_MASK) != 0U) ||
        (frame->function !=
         (uint8_t)(request_function | MODBUS_RTU_EXCEPTION_MASK)))
    {
        return PROTOCOL_FUNCTION_ERROR;
    }

    if (frame->data_length != 1U)
    {
        return PROTOCOL_LENGTH_ERROR;
    }

    *exception_code = (modbus_exception_code_t)frame->data[0];
    return PROTOCOL_OK;
}
