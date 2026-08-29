#include "protocol_byte_order.h"

uint16_t protocol_read_le16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] |
                      ((uint16_t)data[1] << 8U));
}

uint32_t protocol_read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

void protocol_write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

void protocol_write_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFUL);
    data[1] = (uint8_t)((value >> 8U) & 0xFFUL);
    data[2] = (uint8_t)((value >> 16U) & 0xFFUL);
    data[3] = (uint8_t)((value >> 24U) & 0xFFUL);
}
