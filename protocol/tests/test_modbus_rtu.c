#include "test_support.h"

#include "crc16_modbus.h"
#include "modbus_rtu.h"

static void test_set_crc(uint8_t *frame, size_t length)
{
    uint16_t crc;

    crc = crc16_modbus_calculate(frame, length - 2U);
    frame[length - 2U] = (uint8_t)(crc & 0xFFU);
    frame[length - 1U] = (uint8_t)((crc >> 8U) & 0xFFU);
}

bool test_modbus_rtu_suite(void)
{
    modbus_exception_code_t exception_code;
    modbus_rtu_frame_t decoded;
    modbus_rtu_frame_t frame;
    protocol_status_t status;
    uint8_t adu[MODBUS_RTU_MAX_ADU_SIZE + 1U];
    size_t adu_length;
    size_t index;
    size_t length;

    frame.address = 1U;
    frame.function = 0x41U;
    frame.data_length = 0U;
    status = modbus_rtu_encode(&frame, adu, sizeof(adu), &adu_length);
    TEST_CHECK_EQ(PROTOCOL_OK, status);
    TEST_CHECK_EQ(MODBUS_RTU_MIN_ADU_SIZE, adu_length);
    TEST_CHECK_EQ(PROTOCOL_OK,
                  modbus_rtu_decode(adu, adu_length, &decoded));
    TEST_CHECK_EQ(frame.address, decoded.address);
    TEST_CHECK_EQ(frame.function, decoded.function);
    TEST_CHECK_EQ(0U, decoded.data_length);

    frame.data_length = MODBUS_RTU_MAX_DATA_SIZE;
    for (index = 0U; index < frame.data_length; index++)
    {
        frame.data[index] = (uint8_t)(index ^ 0x5AU);
    }
    TEST_CHECK_EQ(PROTOCOL_OK,
                  modbus_rtu_encode(&frame,
                                    adu,
                                    MODBUS_RTU_MAX_ADU_SIZE,
                                    &adu_length));
    TEST_CHECK_EQ(MODBUS_RTU_MAX_ADU_SIZE, adu_length);
    TEST_CHECK_EQ(PROTOCOL_OK,
                  modbus_rtu_decode(adu, adu_length, &decoded));
    TEST_CHECK_EQ(frame.data_length, decoded.data_length);
    TEST_CHECK(test_memory_equal(frame.data,
                                 decoded.data,
                                 frame.data_length));

    /* Exercise every legal RTU Data length, not only the two boundaries. */
    for (length = 0U; length <= MODBUS_RTU_MAX_DATA_SIZE; length++)
    {
        frame.address = (uint8_t)(1U + (length % 247U));
        frame.function = 0x41U;
        frame.data_length = length;
        for (index = 0U; index < length; index++)
        {
            frame.data[index] = (uint8_t)(index + length);
        }

        TEST_CHECK_EQ(PROTOCOL_OK,
                      modbus_rtu_encode(&frame,
                                        adu,
                                        sizeof(adu),
                                        &adu_length));
        TEST_CHECK_EQ(length + MODBUS_RTU_MIN_ADU_SIZE, adu_length);
        TEST_CHECK_EQ(PROTOCOL_OK,
                      modbus_rtu_decode(adu, adu_length, &decoded));
        TEST_CHECK_EQ(length, decoded.data_length);
        TEST_CHECK(test_memory_equal(frame.data, decoded.data, length));
    }

    TEST_CHECK_EQ(PROTOCOL_BUFFER_TOO_SMALL,
                  modbus_rtu_encode(&frame,
                                    adu,
                                    MODBUS_RTU_MAX_ADU_SIZE - 1U,
                                    &adu_length));
    TEST_CHECK_EQ(PROTOCOL_LENGTH_ERROR,
                  modbus_rtu_decode(adu,
                                    MODBUS_RTU_MAX_ADU_SIZE + 1U,
                                    &decoded));
    TEST_CHECK_EQ(PROTOCOL_LENGTH_ERROR,
                  modbus_rtu_decode(adu, 3U, &decoded));

    adu[10] ^= 0x01U;
    TEST_CHECK_EQ(PROTOCOL_CRC_ERROR,
                  modbus_rtu_decode(adu,
                                    MODBUS_RTU_MAX_ADU_SIZE,
                                    &decoded));
    adu[10] ^= 0x01U;

    adu[0] = 0U;
    test_set_crc(adu, MODBUS_RTU_MAX_ADU_SIZE);
    TEST_CHECK_EQ(PROTOCOL_ADDRESS_ERROR,
                  modbus_rtu_decode(adu,
                                    MODBUS_RTU_MAX_ADU_SIZE,
                                    &decoded));
    adu[0] = 1U;
    adu[1] = 0U;
    test_set_crc(adu, MODBUS_RTU_MAX_ADU_SIZE);
    TEST_CHECK_EQ(PROTOCOL_FUNCTION_ERROR,
                  modbus_rtu_decode(adu,
                                    MODBUS_RTU_MAX_ADU_SIZE,
                                    &decoded));

    frame.address = 0U;
    frame.function = 0x41U;
    frame.data_length = 0U;
    TEST_CHECK_EQ(PROTOCOL_ADDRESS_ERROR,
                  modbus_rtu_encode(&frame,
                                    adu,
                                    sizeof(adu),
                                    &adu_length));
    frame.address = 248U;
    TEST_CHECK_EQ(PROTOCOL_ADDRESS_ERROR,
                  modbus_rtu_encode(&frame,
                                    adu,
                                    sizeof(adu),
                                    &adu_length));
    frame.address = 1U;
    frame.function = 0U;
    TEST_CHECK_EQ(PROTOCOL_FUNCTION_ERROR,
                  modbus_rtu_encode(&frame,
                                    adu,
                                    sizeof(adu),
                                    &adu_length));

    TEST_CHECK_EQ(PROTOCOL_OK,
                  modbus_rtu_encode_exception(
                      7U,
                      0x41U,
                      MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE,
                      adu,
                      sizeof(adu),
                      &adu_length));
    TEST_CHECK_EQ(5U, adu_length);
    TEST_CHECK_EQ(PROTOCOL_OK,
                  modbus_rtu_decode(adu, adu_length, &decoded));
    TEST_CHECK_EQ(0xC1U, decoded.function);
    TEST_CHECK_EQ(PROTOCOL_OK,
                  modbus_rtu_decode_exception(
                      &decoded, 0x41U, &exception_code));
    TEST_CHECK_EQ(MODBUS_EXCEPTION_ILLEGAL_DATA_VALUE,
                  exception_code);
    TEST_CHECK_EQ(PROTOCOL_FUNCTION_ERROR,
                  modbus_rtu_decode_exception(
                      &decoded, 0x42U, &exception_code));
    return true;
}
