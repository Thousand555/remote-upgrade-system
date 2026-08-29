#ifndef CRC16_MODBUS_H
#define CRC16_MODBUS_H

#include <stddef.h>
#include <stdint.h>

uint16_t crc16_modbus_calculate(const uint8_t *data, size_t length);

#endif /* CRC16_MODBUS_H */
