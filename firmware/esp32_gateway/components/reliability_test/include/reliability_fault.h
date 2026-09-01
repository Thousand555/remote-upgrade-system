#ifndef RELIABILITY_FAULT_H
#define RELIABILITY_FAULT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    REL_FAULT_DROP_DATA_ACK = 0,
    REL_FAULT_DUPLICATE_DATA,
    REL_FAULT_GAP_OFFSET,
    REL_FAULT_BAD_MANIFEST_CRC,
    REL_FAULT_DROP_ACTIVATE_ACK,
    REL_FAULT_COUNT
} reliability_fault_kind_t;

typedef struct
{
    bool armed[REL_FAULT_COUNT];
    uint8_t timeout_command;
    uint32_t timeout_remaining;
} reliability_fault_snapshot_t;

void reliability_fault_clear(void);
bool reliability_fault_arm(reliability_fault_kind_t kind);
bool reliability_fault_consume(reliability_fault_kind_t kind);
bool reliability_fault_set_timeout(uint8_t command, uint32_t count);
bool reliability_fault_consume_timeout(uint8_t command);
void reliability_fault_get_snapshot(reliability_fault_snapshot_t *snapshot);
const char *reliability_fault_kind_name(reliability_fault_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* RELIABILITY_FAULT_H */
