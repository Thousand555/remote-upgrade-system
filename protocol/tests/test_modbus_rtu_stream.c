#include "test_support.h"

#include "modbus_rtu_stream.h"

static bool test_build_frame(uint8_t function,
                             uint8_t marker,
                             uint8_t *adu,
                             size_t *adu_length)
{
    modbus_rtu_frame_t frame;

    frame.address = 1U;
    frame.function = function;
    frame.data_length = 2U;
    frame.data[0] = marker;
    frame.data[1] = (uint8_t)(marker + 1U);
    return modbus_rtu_encode(&frame,
                             adu,
                             MODBUS_RTU_MAX_ADU_SIZE,
                             adu_length) == PROTOCOL_OK;
}

bool test_modbus_rtu_stream_suite(void)
{
    modbus_rtu_stream_t stream;
    modbus_rtu_frame_t decoded;
    uint8_t adu1[MODBUS_RTU_MAX_ADU_SIZE];
    uint8_t adu2[MODBUS_RTU_MAX_ADU_SIZE];
    uint8_t output[MODBUS_RTU_MAX_ADU_SIZE];
    size_t adu1_length;
    size_t adu2_length;
    size_t output_length;
    size_t index;
    uint32_t timestamp;

    TEST_CHECK_EQ(MODBUS_STREAM_INVALID_ARGUMENT,
                  modbus_rtu_stream_init(NULL, 750U, 1750U));
    TEST_CHECK_EQ(MODBUS_STREAM_INVALID_ARGUMENT,
                  modbus_rtu_stream_init(&stream, 0U, 1750U));
    TEST_CHECK_EQ(MODBUS_STREAM_INVALID_ARGUMENT,
                  modbus_rtu_stream_init(&stream, 1750U, 1750U));
    TEST_CHECK_EQ(MODBUS_STREAM_NO_FRAME,
                  modbus_rtu_stream_init(
                      &stream,
                      MODBUS_RTU_115200_T1_5_US,
                      MODBUS_RTU_115200_T3_5_US));

    TEST_CHECK(test_build_frame(0x41U, 0x10U, adu1, &adu1_length));
    TEST_CHECK(test_build_frame(0x41U, 0x20U, adu2, &adu2_length));

    timestamp = 1000U;
    for (index = 0U; index < adu1_length; index++)
    {
        TEST_CHECK_EQ(MODBUS_STREAM_NO_FRAME,
                      modbus_rtu_stream_feed(&stream,
                                             adu1[index],
                                             timestamp));
        timestamp += 100U;
    }
    TEST_CHECK_EQ(MODBUS_STREAM_NO_FRAME,
                  modbus_rtu_stream_poll(
                      &stream,
                      timestamp + MODBUS_RTU_115200_T1_5_US));
    TEST_CHECK_EQ(MODBUS_STREAM_FRAME_READY,
                  modbus_rtu_stream_poll(
                      &stream,
                      (timestamp - 100U) +
                      MODBUS_RTU_115200_T3_5_US));
    TEST_CHECK_EQ(MODBUS_STREAM_OUTPUT_TOO_SMALL,
                  modbus_rtu_stream_take_frame(&stream,
                                               output,
                                               adu1_length - 1U,
                                               &output_length));
    TEST_CHECK_EQ(MODBUS_STREAM_FRAME_READY,
                  modbus_rtu_stream_take_frame(&stream,
                                               output,
                                               sizeof(output),
                                               &output_length));
    TEST_CHECK_EQ(adu1_length, output_length);
    TEST_CHECK(test_memory_equal(adu1, output, adu1_length));
    TEST_CHECK_EQ(PROTOCOL_OK,
                  modbus_rtu_decode(output, output_length, &decoded));

    modbus_rtu_stream_reset(&stream);
    timestamp = 10000U;
    for (index = 0U; index < adu1_length; index++)
    {
        TEST_CHECK_EQ(MODBUS_STREAM_NO_FRAME,
                      modbus_rtu_stream_feed(&stream,
                                             adu1[index],
                                             timestamp));
        timestamp += 100U;
    }

    timestamp += MODBUS_RTU_115200_T3_5_US;
    TEST_CHECK_EQ(MODBUS_STREAM_FRAME_READY,
                  modbus_rtu_stream_feed(&stream, adu2[0], timestamp));
    TEST_CHECK_EQ(MODBUS_STREAM_BUSY,
                  modbus_rtu_stream_feed(&stream,
                                         adu2[1],
                                         timestamp + 100U));
    TEST_CHECK_EQ(MODBUS_STREAM_FRAME_READY,
                  modbus_rtu_stream_take_frame(&stream,
                                               output,
                                               sizeof(output),
                                               &output_length));
    TEST_CHECK(test_memory_equal(adu1, output, adu1_length));

    for (index = 1U; index < adu2_length; index++)
    {
        timestamp += 100U;
        TEST_CHECK_EQ(MODBUS_STREAM_NO_FRAME,
                      modbus_rtu_stream_feed(&stream,
                                             adu2[index],
                                             timestamp));
    }
    TEST_CHECK_EQ(MODBUS_STREAM_FRAME_READY,
                  modbus_rtu_stream_poll(
                      &stream,
                      timestamp + MODBUS_RTU_115200_T3_5_US));
    TEST_CHECK_EQ(MODBUS_STREAM_FRAME_READY,
                  modbus_rtu_stream_take_frame(&stream,
                                               output,
                                               sizeof(output),
                                               &output_length));
    TEST_CHECK(test_memory_equal(adu2, output, adu2_length));

    modbus_rtu_stream_reset(&stream);
    TEST_CHECK_EQ(MODBUS_STREAM_NO_FRAME,
                  modbus_rtu_stream_feed(&stream, 0x01U, 0U));
    TEST_CHECK_EQ(MODBUS_STREAM_NO_FRAME,
                  modbus_rtu_stream_feed(&stream, 0x41U, 100U));
    TEST_CHECK_EQ(MODBUS_STREAM_INTERCHAR_TIMEOUT,
                  modbus_rtu_stream_feed(
                      &stream,
                      0x55U,
                      100U + MODBUS_RTU_115200_T1_5_US + 1U));
    TEST_CHECK_EQ(MODBUS_STREAM_FRAME_READY,
                  modbus_rtu_stream_poll(
                      &stream,
                      100U + MODBUS_RTU_115200_T1_5_US + 1U +
                      MODBUS_RTU_115200_T3_5_US));
    TEST_CHECK_EQ(MODBUS_STREAM_FRAME_READY,
                  modbus_rtu_stream_take_frame(&stream,
                                               output,
                                               sizeof(output),
                                               &output_length));
    TEST_CHECK_EQ(1U, output_length);
    TEST_CHECK_EQ(0x55U, output[0]);

    modbus_rtu_stream_reset(&stream);
    for (index = 0U; index < MODBUS_RTU_MAX_ADU_SIZE; index++)
    {
        TEST_CHECK_EQ(MODBUS_STREAM_NO_FRAME,
                      modbus_rtu_stream_feed(&stream,
                                             (uint8_t)index,
                                             (uint32_t)(index * 10U)));
    }
    TEST_CHECK_EQ(MODBUS_STREAM_OVERFLOW,
                  modbus_rtu_stream_feed(&stream,
                                         0xAAU,
                                         (uint32_t)(MODBUS_RTU_MAX_ADU_SIZE *
                                                    10U)));

    modbus_rtu_stream_reset(&stream);
    TEST_CHECK_EQ(MODBUS_STREAM_NO_FRAME,
                  modbus_rtu_stream_feed(&stream,
                                         0x01U,
                                         0xFFFFFF00UL));
    TEST_CHECK_EQ(MODBUS_STREAM_NO_FRAME,
                  modbus_rtu_stream_feed(&stream,
                                         0x41U,
                                         0xFFFFFF64UL));
    TEST_CHECK_EQ(MODBUS_STREAM_FRAME_READY,
                  modbus_rtu_stream_poll(
                      &stream,
                      (uint32_t)(0xFFFFFF64UL +
                                 MODBUS_RTU_115200_T3_5_US)));
    return true;
}
