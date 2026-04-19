#pragma once

#include <stdint.h>
#include <stdbool.h>

struct fault_persist_record_t {
    bool valid;
    uint8_t code;
    uint8_t scope;
    int8_t slot;
    uint32_t timestamp;
};

bool faultPersistSave(const fault_persist_record_t& rec);
bool faultPersistLoad(fault_persist_record_t* out);
bool faultPersistClear();
