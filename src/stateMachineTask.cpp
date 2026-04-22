#include <Arduino.h>
#include <math.h>
#include "stateMachineTask.h"
#include "app_shared.h"
#include "app_config.h"
#include "fault.h"
#include "actuatorTask.h"
#include "logger.h"

static bool guardNoFault() {
    return !fault_is_active();
}

static bool guardSlotCanStartUnsafe(uint8_t slot, uint32_t durationMs) {
    if (slot >= SLOT_COUNT || durationMs == 0) return false;
    if (g_app.jobs[slot].state == JOB_RUNNING) return false;
    if (g_app.machineState == MACH_FAULT_LATCHED) return false;
    return true;
}

static bool guardPowerBudgetUnsafe(uint8_t newSlot) {
    float totalCurrent = 0.0f;
    bool anyRunning = false;

    for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
        if (g_app.jobs[i].state != JOB_RUNNING) continue;
        anyRunning = true;
        const SensorPoint& point = g_app.sensors.currentA[i];
        float ia = point.valid && !point.stale ? fabsf(point.value) : g_cfg.slotNominalCurrentA[i];
        if (g_app.systemMode == MODE_PRODUCTION && (!point.valid || point.stale)) {
            return false;
        }
        totalCurrent += ia;
    }

    if (!anyRunning) return true;
    float projected = totalCurrent + g_cfg.slotNominalCurrentA[newSlot] + g_cfg.startMarginA;
    return projected <= g_cfg.maxTotalCurrentA;
}

static void stopSlotUnsafe(uint8_t slot, job_state_t finalState) {
    appSetJobStateUnsafe(slot, finalState, 0);
    appSetActuatorDesiredUnsafe(slot, false);
    actuatorRequestSlot(slot, false);
}

static const char *modeToStr(system_mode_t mode)
{
    switch (mode)
    {
    case MODE_PRODUCTION:
        return "production";
    case MODE_MAINTENANCE:
        return "maintenance";
    case MODE_SIMULATION:
        return "simulation";
    default:
        return "unknown";
    }
}

void stateMachineTask(void* pvParameters) {
    (void)pvParameters;
    event_t evt{};

    for (;;) {
        if (xQueueReceive(eventQueue, &evt, portMAX_DELAY) != pdTRUE) continue;

        if (evt.type == EVT_BOOT_RESTORE_DONE) {
            LOGI("FSM", "boot restore applied");
            continue;
        }

        if (evt.slot >= SLOT_COUNT && evt.slot != -1) continue;
        if (!xSemaphoreTake(sharedMutex, pdMS_TO_TICKS(100))) continue;

        switch (evt.type) {
            case EVT_CMD_START:
                if (!guardNoFault() || !guardSlotCanStartUnsafe(evt.slot, evt.durationMs) || !guardPowerBudgetUnsafe(evt.slot)) {
                    break;
                }
                appSetJobStateUnsafe(evt.slot, JOB_RUNNING, evt.durationMs);
                appSetActuatorDesiredUnsafe(evt.slot, true);
                actuatorRequestSlot(evt.slot, true);
                LOGI("FSM", "start slot=%d durationMs=%lu", evt.slot, static_cast<unsigned long>(evt.durationMs));
                break;

            case EVT_CMD_STOP:
                if (evt.slot >= 0 && g_app.jobs[evt.slot].state == JOB_RUNNING) {
                    stopSlotUnsafe(evt.slot, JOB_IDLE);
                    LOGI("FSM", "stop slot=%d", evt.slot);
                }
                break;

            case EVT_TIMER_EXPIRED:
                if (evt.slot >= 0 && g_app.jobs[evt.slot].state == JOB_RUNNING) {
                    stopSlotUnsafe(evt.slot, JOB_COMPLETED);
                    LOGI("FSM", "complete slot=%d", evt.slot);
                }
                break;

            case EVT_CMD_SET_MODE:
                if (!g_app.anyRunning && !fault_is_active())
                {
                    g_app.systemMode = evt.requestedMode;
                    LOGI("FSM", "mode=%s", modeToStr(evt.requestedMode));

                    if (evt.requestedMode == MODE_SIMULATION)
                    {
                        LOGW("FSM", "simulation mode active");
                    }
                }
                else
                {
                    LOGW("FSM", "set_mode rejected anyRunning=%u fault=%u",
                         g_app.anyRunning ? 1 : 0,
                         fault_is_active() ? 1 : 0);
                }
                break;

            case EVT_CMD_RESET_FAULT:
                if (!g_app.anyRunning && fault_reset()) {
                    for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
                        g_app.jobs[i].state = JOB_IDLE;
                        g_app.jobs[i].remainingMs = 0;
                        g_app.actuator.desiredSSR[i] = false;
                    }
                    g_app.machineState = MACH_IDLE;
                    actuatorRequestAllOff();
                    LOGW("FSM", "fault reset -> machine idle");
                }
                break;

            case EVT_FAULT:
                g_app.machineState = MACH_FAULT_LATCHED;
                if (evt.faultScope == FAULT_SCOPE_GLOBAL) {
                    for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
                        stopSlotUnsafe(i, JOB_FAULT);
                    }
                } else if (evt.slot >= 0) {
                    stopSlotUnsafe(evt.slot, JOB_FAULT);
                }
                actuatorRequestAllOff();
                LOGE("FSM", "fault scope=%u slot=%d code=%u",
                     static_cast<unsigned>(evt.faultScope), evt.slot,
                     static_cast<unsigned>(evt.faultCode));
                break;

            default:
                break;
        }

        appRecomputeAnyRunningUnsafe();
        appUpdateMachineStateUnsafe();
        xSemaphoreGive(sharedMutex);
    }
}
