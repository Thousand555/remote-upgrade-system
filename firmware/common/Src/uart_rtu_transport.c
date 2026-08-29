#include "uart_rtu_transport.h"

#include "main.h"
#include "modbus_rtu_stream.h"
#include "upgrade_config.h"
#include "usart.h"

#define UART_RTU_RING_CAPACITY       512U
#define UART_RTU_RING_INDEX_MASK     (UART_RTU_RING_CAPACITY - 1U)
#define UART_RTU_TIMESTAMP_HZ        1000000UL

#if (UART_RTU_RING_CAPACITY & UART_RTU_RING_INDEX_MASK) != 0U
#error "UART_RTU_RING_CAPACITY must be a power of two"
#endif

typedef struct
{
    uint8_t byte;
    uint32_t timestamp_us;
} uart_rtu_rx_item_t;

static uart_rtu_rx_item_t s_rx_ring[UART_RTU_RING_CAPACITY];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static volatile uint32_t s_received_bytes;
static volatile uint32_t s_dropped_bytes;
static volatile uint32_t s_uart_errors;
static uint32_t s_malformed_frames;
static uint8_t s_rx_byte;
static bool s_initialized;
static modbus_rtu_stream_t s_stream;

static bool uart_rtu_timestamp_init(void)
{
    uint32_t pclk1_hz;
    uint32_t timer_clock_hz;
    uint32_t prescaler;

    pclk1_hz = HAL_RCC_GetPCLK1Freq();
    timer_clock_hz = pclk1_hz;
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != RCC_HCLK_DIV1)
    {
        timer_clock_hz *= 2U;
    }

    if ((timer_clock_hz < UART_RTU_TIMESTAMP_HZ) ||
        ((timer_clock_hz % UART_RTU_TIMESTAMP_HZ) != 0U))
    {
        return false;
    }

    prescaler = (timer_clock_hz / UART_RTU_TIMESTAMP_HZ) - 1U;
    if (prescaler > 0xFFFFUL)
    {
        return false;
    }

    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1 = 0U;
    TIM2->PSC = prescaler;
    TIM2->ARR = 0xFFFFFFFFUL;
    TIM2->CNT = 0U;
    TIM2->EGR = TIM_EGR_UG;
    TIM2->SR = 0U;
    TIM2->CR1 = TIM_CR1_CEN;
    return true;
}

static bool uart_rtu_ring_pop(uart_rtu_rx_item_t *item)
{
    uint16_t tail;

    if (item == NULL)
    {
        return false;
    }

    tail = s_rx_tail;
    if (tail == s_rx_head)
    {
        return false;
    }

    *item = s_rx_ring[tail];
    __DMB();
    s_rx_tail = (uint16_t)((tail + 1U) & UART_RTU_RING_INDEX_MASK);
    return true;
}

static bool uart_rtu_take_ready_frame(uint8_t *frame,
                                      size_t capacity,
                                      size_t *length)
{
    modbus_stream_status_t status;

    status = modbus_rtu_stream_take_frame(&s_stream,
                                          frame,
                                          capacity,
                                          length);
    return status == MODBUS_STREAM_FRAME_READY;
}

bool uart_rtu_transport_init(void)
{
    if (s_initialized)
    {
        return true;
    }

    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_received_bytes = 0U;
    s_dropped_bytes = 0U;
    s_uart_errors = 0U;
    s_malformed_frames = 0U;

    if (!uart_rtu_timestamp_init())
    {
        return false;
    }

    if (modbus_rtu_stream_init(&s_stream,
                               MODBUS_RTU_115200_T1_5_US,
                               MODBUS_RTU_115200_T3_5_US) !=
        MODBUS_STREAM_NO_FRAME)
    {
        return false;
    }

    s_initialized = true;
    if (HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1U) != HAL_OK)
    {
        s_initialized = false;
        return false;
    }

    return true;
}

void uart_rtu_transport_deinit(void)
{
    if (!s_initialized)
    {
        return;
    }

    (void)HAL_UART_AbortReceive(&huart1);
    TIM2->CR1 = 0U;
    __HAL_RCC_TIM2_CLK_DISABLE();
    modbus_rtu_stream_reset(&s_stream);
    s_initialized = false;
}

bool uart_rtu_transport_receive(uint8_t *frame,
                                size_t capacity,
                                size_t *length)
{
    uart_rtu_rx_item_t item;
    modbus_stream_status_t status;

    if ((!s_initialized) || (frame == NULL) || (length == NULL))
    {
        return false;
    }

    if (uart_rtu_take_ready_frame(frame, capacity, length))
    {
        return true;
    }

    while (uart_rtu_ring_pop(&item))
    {
        status = modbus_rtu_stream_feed(&s_stream,
                                        item.byte,
                                        item.timestamp_us);
        if (status == MODBUS_STREAM_FRAME_READY)
        {
            return uart_rtu_take_ready_frame(frame, capacity, length);
        }

        if (status == MODBUS_STREAM_OVERFLOW)
        {
            s_malformed_frames++;
            modbus_rtu_stream_reset(&s_stream);
        }
        else if (status == MODBUS_STREAM_BUSY)
        {
            s_malformed_frames++;
            break;
        }
    }

    status = modbus_rtu_stream_poll(&s_stream,
                                    uart_rtu_transport_time_us());
    if (status == MODBUS_STREAM_FRAME_READY)
    {
        return uart_rtu_take_ready_frame(frame, capacity, length);
    }

    return false;
}

bool uart_rtu_transport_send(const uint8_t *frame, size_t length)
{
    if ((!s_initialized) || (frame == NULL) || (length == 0U) ||
        (length > MODBUS_RTU_MAX_ADU_SIZE))
    {
        return false;
    }

    return HAL_UART_Transmit(&huart1,
                             (uint8_t *)frame,
                             (uint16_t)length,
                             UPGRADE_UART_TX_TIMEOUT_MS) == HAL_OK;
}

uint32_t uart_rtu_transport_time_us(void)
{
    return TIM2->CNT;
}

void uart_rtu_transport_get_diagnostics(
    uart_rtu_transport_diagnostics_t *diagnostics)
{
    if (diagnostics == NULL)
    {
        return;
    }

    diagnostics->received_bytes = s_received_bytes;
    diagnostics->dropped_bytes = s_dropped_bytes;
    diagnostics->uart_errors = s_uart_errors;
    diagnostics->malformed_frames = s_malformed_frames;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint16_t head;
    uint16_t next_head;

    if ((huart == NULL) || (huart->Instance != USART1) || (!s_initialized))
    {
        return;
    }

    head = s_rx_head;
    next_head = (uint16_t)((head + 1U) & UART_RTU_RING_INDEX_MASK);
    if (next_head == s_rx_tail)
    {
        s_dropped_bytes++;
    }
    else
    {
        s_rx_ring[head].byte = s_rx_byte;
        s_rx_ring[head].timestamp_us = uart_rtu_transport_time_us();
        __DMB();
        s_rx_head = next_head;
        s_received_bytes++;
    }

    if (HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1U) != HAL_OK)
    {
        s_uart_errors++;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance != USART1) || (!s_initialized))
    {
        return;
    }

    s_uart_errors++;
    (void)HAL_UART_AbortReceive(huart);
    (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1U);
}
