#include <Arduino.h>
#include "fault_restore.h"
#include "fault_persist.h"
#include "fault.h"
#include "app_shared.h"
#include "actuatorTask.h"

bool faultRestoreFromNVS() {
    fault_persist_record_t rec{};
    if (!faultPersistLoad(&rec) || !rec.valid) {
        return false;
    }

    fault_raise(static_cast<fault_code_t>(rec.code),
                static_cast<fault_scope_t>(rec.scope),
                rec.slot,
                "BootRestore",
                FAULT_SEV_LATCH_GLOBAL);

    appForceBootFaultState(rec.slot, static_cast<fault_scope_t>(rec.scope));
    actuatorRequestAllOff();

    event_t evt{};
    evt.type = EVT_BOOT_RESTORE_DONE;
    evt.slot = rec.slot;
    evt.faultScope = static_cast<fault_scope_t>(rec.scope);
    evt.faultCode = rec.code;
    evt.ts = millis();
    xQueueSend(eventQueue, &evt, 0);
    return true;
}
