#include "crc16_modbus.h"

#define CRC16_MODBUS_INITIAL_VALUE 0xFFFFU
#define CRC16_MODBUS_POLYNOMIAL    0xA001U

uint16_t crc16_modbus_calculate(const uint8_t *data, size_t length)
{
    uint16_t crc;
    size_t byte_index;
    uint8_t bit_index;

    crc = CRC16_MODBUS_INITIAL_VALUE;
    if ((data == NULL) && (length != 0U))
    {
        return 0U;
    }

    for (byte_index = 0U; byte_index < length; byte_index++)
    {
        crc ^= data[byte_index];
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ CRC16_MODBUS_POLYNOMIAL);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}
