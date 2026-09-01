#include "transport_uart.h"

#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gateway_config.h"
#include "gateway_log.h"

#define TRANSPORT_UART_READ_SLICE_MS 10U
#define TRANSPORT_UART_T3_5_US       1750LL

static const char *TAG = "transport_uart";
static bool s_initialized;
static SemaphoreHandle_t s_mutex;

bool transport_uart_is_configured(void)
{
    return (CONFIG_GATEWAY_UPGRADE_UART_TX_GPIO >= 0) &&
           (CONFIG_GATEWAY_UPGRADE_UART_RX_GPIO >= 0);
}

esp_err_t transport_uart_init(void)
{
    const uart_port_t uart_port = (uart_port_t)CONFIG_GATEWAY_UPGRADE_UART_NUM;
    const uart_config_t uart_config = {
        .baud_rate = (int)GATEWAY_UPGRADE_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t status;

    if (s_initialized) {
        return ESP_OK;
    }
    if (!transport_uart_is_configured()) {
        return ESP_ERR_INVALID_STATE;
    }

    status = uart_driver_install(uart_port,
                                 (int)GATEWAY_UPGRADE_UART_RX_BUFFER_SIZE,
                                 0,
                                 0,
                                 NULL,
                                 0);
    if (status != ESP_OK) {
        return status;
    }

    status = uart_param_config(uart_port, &uart_config);
    if (status == ESP_OK) {
        status = uart_set_pin(uart_port,
                              CONFIG_GATEWAY_UPGRADE_UART_TX_GPIO,
                              CONFIG_GATEWAY_UPGRADE_UART_RX_GPIO,
                              UART_PIN_NO_CHANGE,
                              UART_PIN_NO_CHANGE);
    }

    if (status != ESP_OK) {
        (void)uart_driver_delete(uart_port);
        return status;
    }

    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        (void)uart_driver_delete(uart_port);
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    GW_LOGI(TAG,
            "UART%d configured: TX=%d, RX=%d, baud=%u",
            CONFIG_GATEWAY_UPGRADE_UART_NUM,
            CONFIG_GATEWAY_UPGRADE_UART_TX_GPIO,
            CONFIG_GATEWAY_UPGRADE_UART_RX_GPIO,
            (unsigned int)GATEWAY_UPGRADE_UART_BAUD_RATE);
    return ESP_OK;
}

esp_err_t transport_uart_exchange(const uint8_t *request,
                                  size_t request_length,
                                  uint8_t *response,
                                  size_t response_capacity,
                                  size_t *response_length,
                                  uint32_t timeout_ms)
{
    const uart_port_t uart_port = (uart_port_t)CONFIG_GATEWAY_UPGRADE_UART_NUM;
    int64_t deadline_us;
    int64_t last_rx_us;
    TickType_t lock_ticks;
    TickType_t read_ticks;
    size_t total_length;
    int received;
    int written;
    esp_err_t status;

    if ((request == NULL) || (request_length == 0U) ||
        (response == NULL) || (response_capacity == 0U) ||
        (response_length == NULL) || (timeout_ms == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((!s_initialized) || (s_mutex == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    *response_length = 0U;
    lock_ticks = pdMS_TO_TICKS(timeout_ms);
    if (lock_ticks == 0U) {
        lock_ticks = 1U;
    }
    if (xSemaphoreTake(s_mutex, lock_ticks) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    status = uart_flush_input(uart_port);
    if (status != ESP_OK) {
        goto done;
    }

    written = uart_write_bytes(uart_port, request, request_length);
    if (written != (int)request_length) {
        status = ESP_FAIL;
        goto done;
    }
    status = uart_wait_tx_done(uart_port, lock_ticks);
    if (status != ESP_OK) {
        goto done;
    }

    deadline_us = esp_timer_get_time() + ((int64_t)timeout_ms * 1000LL);
    last_rx_us = 0LL;
    total_length = 0U;
    read_ticks = pdMS_TO_TICKS(TRANSPORT_UART_READ_SLICE_MS);
    if (read_ticks == 0U) {
        read_ticks = 1U;
    }

    while (esp_timer_get_time() < deadline_us) {
        received = uart_read_bytes(uart_port,
                                   response + total_length,
                                   response_capacity - total_length,
                                   read_ticks);
        if (received < 0) {
            status = ESP_FAIL;
            goto done;
        }
        if (received > 0) {
            total_length += (size_t)received;
            last_rx_us = esp_timer_get_time();
            if (total_length == response_capacity) {
                break;
            }
            continue;
        }

        if ((total_length > 0U) &&
            ((esp_timer_get_time() - last_rx_us) >= TRANSPORT_UART_T3_5_US)) {
            break;
        }
    }

    if (total_length == 0U) {
        status = ESP_ERR_TIMEOUT;
        goto done;
    }

    *response_length = total_length;
    status = ESP_OK;

done:
    (void)xSemaphoreGive(s_mutex);
    return status;
}

void transport_uart_deinit(void)
{
    const uart_port_t uart_port = (uart_port_t)CONFIG_GATEWAY_UPGRADE_UART_NUM;

    if (!s_initialized) {
        return;
    }
    (void)uart_driver_delete(uart_port);
    if (s_mutex != NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    s_initialized = false;
}
