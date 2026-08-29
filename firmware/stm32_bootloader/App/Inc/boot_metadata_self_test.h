#ifndef BOOT_METADATA_SELF_TEST_H
#define BOOT_METADATA_SELF_TEST_H

#include "boot_metadata.h"

/* Setting this to 1 erases Metadata Sector 4. Keep it disabled normally. */
#ifndef BOOT_METADATA_SELF_TEST_ENABLE
#define BOOT_METADATA_SELF_TEST_ENABLE 0
#endif

boot_metadata_status_t boot_metadata_self_test_run(void);

#endif /* BOOT_METADATA_SELF_TEST_H */
