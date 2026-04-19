#include <Arduino.h>
#include <math.h>
#include "safetyTask.h"
#include "app_shared.h"
#include "app_config.h"
#include "fault.h"

static void raiseSlotFault(uint8_t slot, fault_code_t code) {
    if (fault_raise(code, FAULT_SCOPE_SLOT, slot, "SafetyTask", FAULT_SEV_TRIP_SLOT)) {
        event_t evt{};
        evt.type = EVT_FAULT;
        evt.slot = slot;
        evt.faultCode = static_cast<uint8_t>(code);
        evt.faultScope = FAULT_SCOPE_SLOT;
        evt.ts = millis();
        xQueueSend(eventQueue, &evt, 0);
    }
}

static void raiseGlobalFault(fault_code_t code) {
    if (fault_raise(code, FAULT_SCOPE_GLOBAL, -1, "SafetyTask", FAULT_SEV_LATCH_GLOBAL)) {
        event_t evt{};
        evt.type = EVT_FAULT;
        evt.slot = -1;
        evt.faultCode = static_cast<uint8_t>(code);
        evt.faultScope = FAULT_SCOPE_GLOBAL;
        evt.ts = millis();
        xQueueSend(eventQueue, &evt, 0);
    }
}

void safetyTask(void* pvParameters) {
    (void)pvParameters;
    MachineSnapshot snap{};

    for (;;) {
        if (!appGetSnapshot(&snap, pdMS_TO_TICKS(50))) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (snap.systemMode == MODE_COMMISSIONING || fault_is_active()) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
            if (snap.jobs[i].state != JOB_RUNNING) continue;

            const SensorPoint& t = snap.sensors.tempC[i];
            const SensorPoint& c = snap.sensors.currentA[i];
            const SensorPoint& p = snap.sensors.pressureBar[i];
            uint32_t now = millis();

            if (!t.valid || (now - t.lastUpdateMs > g_cfg.sensorStaleTimeoutMs)) {
                raiseSlotFault(i, FAULT_SENSOR_LOST);
                break;
            }
            if (t.value > g_cfg.maxTempC) {
                raiseSlotFault(i, FAULT_OVERTEMP);
                break;
            }
            if (c.valid && !c.stale && fabsf(c.value) > g_cfg.maxCurrentA) {
                raiseSlotFault(i, FAULT_OVERCURRENT);
                break;
            }
            if (!p.valid || (now - p.lastUpdateMs > g_cfg.sensorStaleTimeoutMs)) {
                raiseGlobalFault(FAULT_SENSOR_LOST);
                break;
            }
            if (p.value > g_cfg.maxPressureBar) {
                raiseGlobalFault(FAULT_PRESSURE_HIGH);
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
