#include "modbus_rtu_stream.h"

static void modbus_rtu_stream_start_frame(modbus_rtu_stream_t *stream,
                                          uint8_t byte,
                                          uint32_t timestamp_us)
{
    stream->current_frame[0] = byte;
    stream->current_length = 1U;
    stream->last_byte_timestamp_us = timestamp_us;
    stream->receiving = true;
}

static void modbus_rtu_stream_complete_current(
    modbus_rtu_stream_t *stream)
{
    size_t index;

    for (index = 0U; index < stream->current_length; index++)
    {
        stream->completed_frame[index] = stream->current_frame[index];
    }
    stream->completed_length = stream->current_length;
    stream->frame_ready = true;
    stream->current_length = 0U;
    stream->receiving = false;
}

modbus_stream_status_t modbus_rtu_stream_init(
    modbus_rtu_stream_t *stream,
    uint32_t interchar_timeout_us,
    uint32_t frame_gap_us)
{
    if (stream == NULL)
    {
        return MODBUS_STREAM_INVALID_ARGUMENT;
    }

    if ((interchar_timeout_us == 0U) ||
        (frame_gap_us <= interchar_timeout_us))
    {
        return MODBUS_STREAM_INVALID_ARGUMENT;
    }

    stream->interchar_timeout_us = interchar_timeout_us;
    stream->frame_gap_us = frame_gap_us;
    stream->last_byte_timestamp_us = 0U;
    stream->current_length = 0U;
    stream->completed_length = 0U;
    stream->receiving = false;
    stream->frame_ready = false;
    return MODBUS_STREAM_NO_FRAME;
}

void modbus_rtu_stream_reset(modbus_rtu_stream_t *stream)
{
    if (stream == NULL)
    {
        return;
    }

    stream->last_byte_timestamp_us = 0U;
    stream->current_length = 0U;
    stream->completed_length = 0U;
    stream->receiving = false;
    stream->frame_ready = false;
}

modbus_stream_status_t modbus_rtu_stream_feed(
    modbus_rtu_stream_t *stream,
    uint8_t byte,
    uint32_t timestamp_us)
{
    uint32_t elapsed_us;

    if (stream == NULL)
    {
        return MODBUS_STREAM_INVALID_ARGUMENT;
    }

    if (stream->frame_ready)
    {
        return MODBUS_STREAM_BUSY;
    }

    if (!stream->receiving)
    {
        modbus_rtu_stream_start_frame(stream, byte, timestamp_us);
        return MODBUS_STREAM_NO_FRAME;
    }

    elapsed_us = timestamp_us - stream->last_byte_timestamp_us;
    if (elapsed_us >= stream->frame_gap_us)
    {
        modbus_rtu_stream_complete_current(stream);
        modbus_rtu_stream_start_frame(stream, byte, timestamp_us);
        return MODBUS_STREAM_FRAME_READY;
    }

    if (elapsed_us > stream->interchar_timeout_us)
    {
        modbus_rtu_stream_start_frame(stream, byte, timestamp_us);
        return MODBUS_STREAM_INTERCHAR_TIMEOUT;
    }

    if (stream->current_length >= MODBUS_RTU_MAX_ADU_SIZE)
    {
        stream->current_length = 0U;
        stream->receiving = false;
        return MODBUS_STREAM_OVERFLOW;
    }

    stream->current_frame[stream->current_length] = byte;
    stream->current_length++;
    stream->last_byte_timestamp_us = timestamp_us;
    return MODBUS_STREAM_NO_FRAME;
}

modbus_stream_status_t modbus_rtu_stream_poll(
    modbus_rtu_stream_t *stream,
    uint32_t timestamp_us)
{
    uint32_t elapsed_us;

    if (stream == NULL)
    {
        return MODBUS_STREAM_INVALID_ARGUMENT;
    }

    if (stream->frame_ready)
    {
        return MODBUS_STREAM_FRAME_READY;
    }

    if (!stream->receiving)
    {
        return MODBUS_STREAM_NO_FRAME;
    }

    elapsed_us = timestamp_us - stream->last_byte_timestamp_us;
    if (elapsed_us >= stream->frame_gap_us)
    {
        modbus_rtu_stream_complete_current(stream);
        return MODBUS_STREAM_FRAME_READY;
    }

    return MODBUS_STREAM_NO_FRAME;
}

modbus_stream_status_t modbus_rtu_stream_take_frame(
    modbus_rtu_stream_t *stream,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    size_t index;

    if ((stream == NULL) || (output == NULL) || (output_length == NULL))
    {
        return MODBUS_STREAM_INVALID_ARGUMENT;
    }

    if (!stream->frame_ready)
    {
        return MODBUS_STREAM_NO_FRAME;
    }

    if (output_capacity < stream->completed_length)
    {
        return MODBUS_STREAM_OUTPUT_TOO_SMALL;
    }

    for (index = 0U; index < stream->completed_length; index++)
    {
        output[index] = stream->completed_frame[index];
    }
    *output_length = stream->completed_length;
    stream->completed_length = 0U;
    stream->frame_ready = false;
    return MODBUS_STREAM_FRAME_READY;
}
