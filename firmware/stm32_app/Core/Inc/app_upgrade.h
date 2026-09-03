#ifndef APP_UPGRADE_H
#define APP_UPGRADE_H

#include <stdbool.h>

/* Start the M11 independent watchdog before clock/peripheral initialization. */
void app_upgrade_watchdog_start(void);

/* APP service plus delayed PENDING_BOOT -> CONFIRMED transition. */
bool app_upgrade_init(void);
void app_upgrade_poll(void);

#endif /* APP_UPGRADE_H */
