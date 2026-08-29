#ifndef UART_RTU_TRANSPORT_H
#define UART_RTU_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "modbus_rtu.h"

typedef struct
{
    uint32_t received_bytes;
    uint32_t dropped_bytes;
    uint32_t uart_errors;
    uint32_t malformed_frames;
} uart_rtu_transport_diagnostics_t;

/*
 * USART1 is interrupt-driven. TIM2 is reserved as a free-running 1 MHz
 * timestamp source so Modbus t1.5/t3.5 timing is independent of HAL_GetTick.
 */
bool uart_rtu_transport_init(void);
void uart_rtu_transport_deinit(void);

bool uart_rtu_transport_receive(uint8_t *frame,
                                size_t capacity,
                                size_t *length);

bool uart_rtu_transport_send(const uint8_t *frame, size_t length);

uint32_t uart_rtu_transport_time_us(void);
void uart_rtu_transport_get_diagnostics(
    uart_rtu_transport_diagnostics_t *diagnostics);

#endif /* UART_RTU_TRANSPORT_H */
