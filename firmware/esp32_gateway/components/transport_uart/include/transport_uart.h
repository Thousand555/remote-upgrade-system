#ifndef TRANSPORT_UART_H
#define TRANSPORT_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool transport_uart_is_configured(void);
esp_err_t transport_uart_init(void);
esp_err_t transport_uart_exchange(const uint8_t *request,
                                  size_t request_length,
                                  uint8_t *response,
                                  size_t response_capacity,
                                  size_t *response_length,
                                  uint32_t timeout_ms);
void transport_uart_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_UART_H */
