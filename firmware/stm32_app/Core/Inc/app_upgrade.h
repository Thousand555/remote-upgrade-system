#ifndef APP_UPGRADE_H
#define APP_UPGRADE_H

#include <stdbool.h>

/* Minimal APP-side service: HELLO, GET_INFO and ENTER_BOOT only. */
bool app_upgrade_init(void);
void app_upgrade_poll(void);

#endif /* APP_UPGRADE_H */
