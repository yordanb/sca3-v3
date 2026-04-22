#include <Arduino.h>
#include <math.h>
#include "safetyTask.h"
#include "app_shared.h"
#include "app_config.h"
#include "fault.h"

static uint8_t g_tempInvalidStreak[SLOT_COUNT] = {0, 0, 0, 0};
static uint8_t g_pressInvalidStreak[SLOT_COUNT] = {0, 0, 0, 0};

static void raiseSlotFault(uint8_t slot, fault_code_t code)
{
    if (fault_raise(code, FAULT_SCOPE_SLOT, slot, "SafetyTask", FAULT_SEV_TRIP_SLOT))
    {
        event_t evt{};
        evt.type = EVT_FAULT;
        evt.slot = slot;
        evt.faultCode = static_cast<uint8_t>(code);
        evt.faultScope = FAULT_SCOPE_SLOT;
        evt.ts = millis();
        xQueueSend(eventQueue, &evt, 0);
    }
}

static void raiseGlobalFault(fault_code_t code)
{
    if (fault_raise(code, FAULT_SCOPE_GLOBAL, -1, "SafetyTask", FAULT_SEV_LATCH_GLOBAL))
    {
        event_t evt{};
        evt.type = EVT_FAULT;
        evt.slot = -1;
        evt.faultCode = static_cast<uint8_t>(code);
        evt.faultScope = FAULT_SCOPE_GLOBAL;
        evt.ts = millis();
        xQueueSend(eventQueue, &evt, 0);
    }
}

void safetyTask(void *pvParameters)
{
    (void)pvParameters;
    MachineSnapshot snap{};

    for (;;)
    {
        if (!appGetSnapshot(&snap, pdMS_TO_TICKS(50)))
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // Maintenance dan Simulation: skip safety trip
        // Production: safety aktif normal
        if (snap.systemMode == MODE_MAINTENANCE ||
            snap.systemMode == MODE_SIMULATION ||
            fault_is_active())
        {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        uint32_t now = millis();

        for (uint8_t i = 0; i < SLOT_COUNT; ++i)
        {
            if (snap.jobs[i].state != JOB_RUNNING)
            {
                g_tempInvalidStreak[i] = 0;
                g_pressInvalidStreak[i] = 0;
                continue;
            }

            const SensorPoint &t = snap.sensors.tempC[i];
            const SensorPoint &c = snap.sensors.currentA[i];
            const SensorPoint &p = snap.sensors.pressureBar[i];

            bool tempInvalid = (!t.valid || (now - t.lastUpdateMs > g_cfg.sensorStaleTimeoutMs));
            bool pressureInvalid = (!p.valid || (now - p.lastUpdateMs > g_cfg.sensorStaleTimeoutMs));

            if (tempInvalid)
            {
                if (g_tempInvalidStreak[i] < 255)
                    g_tempInvalidStreak[i]++;
            }
            else
            {
                g_tempInvalidStreak[i] = 0;
            }

            if (pressureInvalid)
            {
                if (g_pressInvalidStreak[i] < 255)
                    g_pressInvalidStreak[i]++;
            }
            else
            {
                g_pressInvalidStreak[i] = 0;
            }

            if (g_tempInvalidStreak[i] >= 3)
            {
                raiseSlotFault(i, FAULT_SENSOR_LOST);
                break;
            }

            if (t.valid && t.value > g_cfg.maxTempC)
            {
                raiseSlotFault(i, FAULT_OVERTEMP);
                break;
            }

            if (c.valid && !c.stale && fabsf(c.value) > g_cfg.maxCurrentA)
            {
                raiseSlotFault(i, FAULT_OVERCURRENT);
                break;
            }

            if (g_pressInvalidStreak[i] >= 3)
            {
                raiseGlobalFault(FAULT_SENSOR_LOST);
                break;
            }

            if (p.valid && p.value > g_cfg.maxPressureBar)
            {
                raiseGlobalFault(FAULT_PRESSURE_HIGH);
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}