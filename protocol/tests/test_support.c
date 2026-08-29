#include "test_support.h"

bool test_memory_equal(const uint8_t *expected,
                       const uint8_t *actual,
                       size_t length)
{
    size_t index;

    for (index = 0U; index < length; index++)
    {
        if (expected[index] != actual[index])
        {
            (void)printf("FAIL byte %lu: expected 0x%02X, actual 0x%02X\n",
                         (unsigned long)index,
                         (unsigned int)expected[index],
                         (unsigned int)actual[index]);
            return false;
        }
    }

    return true;
}
