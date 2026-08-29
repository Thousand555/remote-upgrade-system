#ifndef PROTOCOL_BYTE_ORDER_H
#define PROTOCOL_BYTE_ORDER_H

#include <stdint.h>

uint16_t protocol_read_le16(const uint8_t *data);
uint32_t protocol_read_le32(const uint8_t *data);
void protocol_write_le16(uint8_t *data, uint16_t value);
void protocol_write_le32(uint8_t *data, uint32_t value);

#endif /* PROTOCOL_BYTE_ORDER_H */
