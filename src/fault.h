#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "app_shared.h"

enum fault_code_t : uint8_t {
    FAULT_NONE = 0,
    FAULT_OVERTEMP,
    FAULT_OVERCURRENT,
    FAULT_SENSOR_LOST,
    FAULT_PRESSURE_HIGH,
    FAULT_DOOR_OPEN,
    FAULT_TIMER_STALL,
    FAULT_INTERNAL
};

enum fault_severity_t : uint8_t {
    FAULT_SEV_WARN = 0,
    FAULT_SEV_TRIP_SLOT,
    FAULT_SEV_TRIP_GLOBAL,
    FAULT_SEV_LATCH_GLOBAL
};

struct fault_latch_t {
    bool active;
    fault_code_t code;
    fault_scope_t scope;
    int8_t slot;
    fault_severity_t severity;
    uint32_t timestamp;
    char source[16];
};

void fault_init();
bool fault_raise(fault_code_t code, fault_scope_t scope, int slot, const char* source,
                 fault_severity_t severity = FAULT_SEV_TRIP_SLOT);
bool fault_reset();
bool fault_is_active();
fault_latch_t fault_get_snapshot();
