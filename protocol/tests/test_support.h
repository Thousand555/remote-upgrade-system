#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_CHECK(condition)                                                \
    do                                                                       \
    {                                                                        \
        if (!(condition))                                                    \
        {                                                                    \
            (void)printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                    \
        }                                                                    \
    } while (0)

#define TEST_CHECK_EQ(expected, actual)                                      \
    do                                                                       \
    {                                                                        \
        unsigned long test_expected_value;                                   \
        unsigned long test_actual_value;                                     \
        test_expected_value = (unsigned long)(expected);                     \
        test_actual_value = (unsigned long)(actual);                         \
        if (test_expected_value != test_actual_value)                        \
        {                                                                    \
            (void)printf("FAIL %s:%d: expected %lu, actual %lu\n",           \
                         __FILE__,                                           \
                         __LINE__,                                           \
                         test_expected_value,                                \
                         test_actual_value);                                 \
            return false;                                                    \
        }                                                                    \
    } while (0)

bool test_memory_equal(const uint8_t *expected,
                       const uint8_t *actual,
                       size_t length);

#endif /* TEST_SUPPORT_H */
