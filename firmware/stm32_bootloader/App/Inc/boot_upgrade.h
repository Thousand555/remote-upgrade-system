#ifndef BOOT_UPGRADE_H
#define BOOT_UPGRADE_H

#include <stdbool.h>

/* Initialize USART1 RTU transport and restore the latest Metadata session. */
bool boot_upgrade_init(void);

/* Process at most one complete RTU request. Call continuously from main. */
void boot_upgrade_poll(void);

/* True after a valid request for this node is received. */
bool boot_upgrade_has_activity(void);

#endif /* BOOT_UPGRADE_H */
