#include "test_support.h"

#include "crc16_modbus.h"

bool test_crc16_modbus_suite(void)
{
    static const uint8_t standard_text[] =
        {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    static const uint8_t read_request[] =
        {0x01U, 0x03U, 0x00U, 0x00U, 0x00U, 0x0AU};
    static const uint8_t single_byte[] = {0x00U};

    TEST_CHECK_EQ(0x4B37U,
                  crc16_modbus_calculate(standard_text,
                                         sizeof(standard_text)));
    TEST_CHECK_EQ(0xCDC5U,
                  crc16_modbus_calculate(read_request,
                                         sizeof(read_request)));
    TEST_CHECK_EQ(0xFFFFU, crc16_modbus_calculate(NULL, 0U));
    TEST_CHECK_EQ(0x40BFU,
                  crc16_modbus_calculate(single_byte,
                                         sizeof(single_byte)));
    TEST_CHECK_EQ(0U, crc16_modbus_calculate(NULL, 1U));
    return true;
}
