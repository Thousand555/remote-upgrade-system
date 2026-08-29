#include <stdbool.h>
#include <stdio.h>

bool test_crc16_modbus_suite(void);
bool test_modbus_rtu_suite(void);
bool test_upgrade_protocol_suite(void);
bool test_modbus_rtu_stream_suite(void);

typedef bool (*test_suite_fn_t)(void);

typedef struct
{
    const char *name;
    test_suite_fn_t run;
} test_suite_t;

int main(void)
{
    static const test_suite_t suites[] =
    {
        {"crc16_modbus", test_crc16_modbus_suite},
        {"modbus_rtu", test_modbus_rtu_suite},
        {"upgrade_protocol", test_upgrade_protocol_suite},
        {"modbus_rtu_stream", test_modbus_rtu_stream_suite}
    };
    size_t index;
    size_t passed;

    passed = 0U;
    for (index = 0U; index < (sizeof(suites) / sizeof(suites[0])); index++)
    {
        (void)printf("[TEST] %s\n", suites[index].name);
        if (suites[index].run())
        {
            passed++;
            (void)printf("[PASS] %s\n", suites[index].name);
        }
        else
        {
            (void)printf("[FAIL] %s\n", suites[index].name);
        }
    }

    (void)printf("%lu/%lu suites passed\n",
                 (unsigned long)passed,
                 (unsigned long)(sizeof(suites) / sizeof(suites[0])));
    return (passed == (sizeof(suites) / sizeof(suites[0]))) ? 0 : 1;
}
