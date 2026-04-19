#include <Arduino.h>
#include <string.h>
#include "fault.h"
#include "fault_persist.h"
#include "freertos/semphr.h"
#include "logger.h"

static fault_latch_t g_fault = {};
static SemaphoreHandle_t faultMutex = nullptr;

void fault_init() {
    faultMutex = xSemaphoreCreateMutex();
    memset(&g_fault, 0, sizeof(g_fault));
}

bool fault_raise(fault_code_t code, fault_scope_t scope, int slot, const char* source,
                 fault_severity_t severity) {
    if (!faultMutex) return false;
    xSemaphoreTake(faultMutex, portMAX_DELAY);
    bool latched = false;
    if (!g_fault.active) {
        g_fault.active = true;
        g_fault.code = code;
        g_fault.scope = scope;
        g_fault.slot = slot;
        g_fault.severity = severity;
        g_fault.timestamp = millis();
        strncpy(g_fault.source, source ? source : "unknown", sizeof(g_fault.source) - 1);
        g_fault.source[sizeof(g_fault.source) - 1] = '\0';

        fault_persist_record_t rec{};
        rec.valid = true;
        rec.code = static_cast<uint8_t>(code);
        rec.scope = static_cast<uint8_t>(scope);
        rec.slot = static_cast<int8_t>(slot);
        rec.timestamp = g_fault.timestamp;
        faultPersistSave(rec);
        latched = true;
        LOGE("FAULT", "latched code=%u scope=%u slot=%d source=%s",
             static_cast<unsigned>(code), static_cast<unsigned>(scope), slot, g_fault.source);
    }
    xSemaphoreGive(faultMutex);
    return latched;
}

bool fault_reset() {
    if (!faultMutex) return false;
    xSemaphoreTake(faultMutex, portMAX_DELAY);
    if (!g_fault.active) {
        xSemaphoreGive(faultMutex);
        return false;
    }
    memset(&g_fault, 0, sizeof(g_fault));
    faultPersistClear();
    xSemaphoreGive(faultMutex);
    LOGW("FAULT", "reset");
    return true;
}

bool fault_is_active() {
    if (!faultMutex) return false;
    xSemaphoreTake(faultMutex, portMAX_DELAY);
    bool active = g_fault.active;
    xSemaphoreGive(faultMutex);
    return active;
}

fault_latch_t fault_get_snapshot() {
    fault_latch_t snap{};
    if (!faultMutex) return snap;
    xSemaphoreTake(faultMutex, portMAX_DELAY);
    snap = g_fault;
    xSemaphoreGive(faultMutex);
    return snap;
}
