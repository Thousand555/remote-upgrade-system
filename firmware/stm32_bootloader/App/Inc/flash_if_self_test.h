#ifndef FLASH_IF_SELF_TEST_H
#define FLASH_IF_SELF_TEST_H

#include "flash_if.h"

/*
 * Set to 1 only for an intentional board test. The test erases APP Sector 5
 * and therefore destroys the currently installed application image.
 */
#ifndef FLASH_IF_SELF_TEST_ENABLE
#define FLASH_IF_SELF_TEST_ENABLE 0
#endif

flash_if_status_t flash_if_self_test_run(void);

#endif /* FLASH_IF_SELF_TEST_H */
