#include "reliability_fault.h"

#include <string.h>

#include "freertos/FreeRTOS.h"

static portMUX_TYPE s_fault_lock = portMUX_INITIALIZER_UNLOCKED;
static reliability_fault_snapshot_t s_faults;

void reliability_fault_clear(void)
{
    portENTER_CRITICAL(&s_fault_lock);
    memset(&s_faults, 0, sizeof(s_faults));
    portEXIT_CRITICAL(&s_fault_lock);
}

bool reliability_fault_arm(reliability_fault_kind_t kind)
{
    if ((kind < 0) || (kind >= REL_FAULT_COUNT)) {
        return false;
    }

    portENTER_CRITICAL(&s_fault_lock);
    s_faults.armed[kind] = true;
    portEXIT_CRITICAL(&s_fault_lock);
    return true;
}

bool reliability_fault_consume(reliability_fault_kind_t kind)
{
    bool armed;

    if ((kind < 0) || (kind >= REL_FAULT_COUNT)) {
        return false;
    }

    portENTER_CRITICAL(&s_fault_lock);
    armed = s_faults.armed[kind];
    s_faults.armed[kind] = false;
    portEXIT_CRITICAL(&s_fault_lock);
    return armed;
}

bool reliability_fault_set_timeout(uint8_t command, uint32_t count)
{
    if ((command == 0U) || (count == 0U)) {
        return false;
    }

    portENTER_CRITICAL(&s_fault_lock);
    s_faults.timeout_command = command;
    s_faults.timeout_remaining = count;
    portEXIT_CRITICAL(&s_fault_lock);
    return true;
}

bool reliability_fault_consume_timeout(uint8_t command)
{
    bool inject;

    portENTER_CRITICAL(&s_fault_lock);
    inject = (s_faults.timeout_command == command) &&
             (s_faults.timeout_remaining > 0U);
    if (inject) {
        s_faults.timeout_remaining--;
        if (s_faults.timeout_remaining == 0U) {
            s_faults.timeout_command = 0U;
        }
    }
    portEXIT_CRITICAL(&s_fault_lock);
    return inject;
}

void reliability_fault_get_snapshot(reliability_fault_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    portENTER_CRITICAL(&s_fault_lock);
    *snapshot = s_faults;
    portEXIT_CRITICAL(&s_fault_lock);
}

const char *reliability_fault_kind_name(reliability_fault_kind_t kind)
{
    switch (kind) {
        case REL_FAULT_DROP_DATA_ACK: return "drop_data_ack_once";
        case REL_FAULT_DUPLICATE_DATA: return "duplicate_data_once";
        case REL_FAULT_GAP_OFFSET: return "gap_offset_once";
        case REL_FAULT_BAD_MANIFEST_CRC: return "bad_manifest_crc_once";
        case REL_FAULT_DROP_ACTIVATE_ACK: return "drop_activate_ack_once";
        default: return "unknown";
    }
}
