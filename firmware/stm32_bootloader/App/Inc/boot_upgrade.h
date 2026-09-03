#ifndef BOOT_UPGRADE_H
#define BOOT_UPGRADE_H

#include <stdbool.h>

/* Initialize USART1 RTU transport and restore the latest Metadata session. */
bool boot_upgrade_init(void);

/* True when the verified image is still waiting for APP confirmation. */
bool boot_upgrade_application_is_pending(void);

/*
 * Make the final boot decision immediately before jumping to the APP.
 * PENDING_BOOT attempts are persisted here; after the configured limit the
 * state becomes FAILED and false is returned so recovery service stays up.
 */
bool boot_upgrade_prepare_application_boot(void);

/* Process at most one complete RTU request. Call continuously from main. */
void boot_upgrade_poll(void);

/* True after a valid request for this node is received. */
bool boot_upgrade_has_activity(void);

#endif /* BOOT_UPGRADE_H */
