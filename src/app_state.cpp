#include <math.h>
#include <string.h>
#include "app_shared.h"

SemaphoreHandle_t sharedMutex = nullptr;
SemaphoreHandle_t i2cMutex = nullptr;
QueueHandle_t eventQueue = nullptr;
QueueHandle_t actuatorQueue = nullptr;
MachineRuntime g_app = {};

static void initSensorPoint(SensorPoint& p) {
    p.value = NAN;
    p.valid = false;
    p.stale = true;
    p.lastUpdateMs = 0;
    p.faultFlags = 0;
}

bool appInitSharedObjects() {
    sharedMutex = xSemaphoreCreateMutex();
    i2cMutex = xSemaphoreCreateMutex();
    eventQueue = xQueueCreate(16, sizeof(event_t));
    actuatorQueue = xQueueCreate(16, sizeof(actuator_cmd_t));
    return sharedMutex && i2cMutex && eventQueue && actuatorQueue;
}

void appBootSafeDefaults() {
    if (!sharedMutex) return;
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    memset(&g_app, 0, sizeof(g_app));
    for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
        g_app.jobs[i].state = JOB_IDLE;
        g_app.jobs[i].remainingMs = 0;
        g_app.actuator.desiredSSR[i] = false;
        g_app.actuator.actualSSR[i] = false;
        g_app.actuator.lastChangeMs[i] = 0;
        initSensorPoint(g_app.sensors.tempC[i]);
        initSensorPoint(g_app.sensors.currentA[i]);
        initSensorPoint(g_app.sensors.pressureBar[i]);
    }
    g_app.machineState = MACH_BOOT;
    g_app.systemMode = MODE_COMMISSIONING;
    g_app.anyRunning = false;
    xSemaphoreGive(sharedMutex);
}

bool appGetSnapshot(MachineSnapshot* out, TickType_t timeout) {
    if (!out) return false;
    if (!xSemaphoreTake(sharedMutex, timeout)) return false;
    memcpy(out, &g_app, sizeof(MachineSnapshot));
    xSemaphoreGive(sharedMutex);
    return true;
}

bool appSetSystemMode(system_mode_t mode, TickType_t timeout) {
    if (!xSemaphoreTake(sharedMutex, timeout)) return false;
    g_app.systemMode = mode;
    xSemaphoreGive(sharedMutex);
    return true;
}

bool appIsAnyJobRunning(TickType_t timeout) {
    bool any = false;
    if (!xSemaphoreTake(sharedMutex, timeout)) return false;
    any = g_app.anyRunning;
    xSemaphoreGive(sharedMutex);
    return any;
}

void appRecomputeAnyRunningUnsafe() {
    g_app.anyRunning = false;
    for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
        if (g_app.jobs[i].state == JOB_RUNNING) {
            g_app.anyRunning = true;
            break;
        }
    }
}

void appUpdateMachineStateUnsafe() {
    if (g_app.machineState == MACH_FAULT_LATCHED) return;
    g_app.machineState = g_app.anyRunning ? MACH_RUNNING : MACH_IDLE;
}

void appSetJobStateUnsafe(uint8_t slot, job_state_t state, uint32_t remainingMs) {
    if (slot >= SLOT_COUNT) return;
    g_app.jobs[slot].state = state;
    g_app.jobs[slot].remainingMs = remainingMs;
    appRecomputeAnyRunningUnsafe();
    appUpdateMachineStateUnsafe();
}

void appSetActuatorDesiredUnsafe(uint8_t slot, bool on) {
    if (slot >= SLOT_COUNT) return;
    g_app.actuator.desiredSSR[slot] = on;
}

void appSetActuatorActualUnsafe(uint8_t slot, bool on) {
    if (slot >= SLOT_COUNT) return;
    g_app.actuator.actualSSR[slot] = on;
    g_app.actuator.lastChangeMs[slot] = millis();
}

static void markPoint(SensorPoint& p, float value, bool valid, uint16_t faultFlags) {
    p.value = value;
    p.valid = valid;
    p.stale = false;
    p.lastUpdateMs = millis();
    p.faultFlags = faultFlags;
}

void appMarkTemp(uint8_t slot, float value, bool valid, uint16_t faultFlags) {
    if (!xSemaphoreTake(sharedMutex, pdMS_TO_TICKS(20))) return;
    if (slot < SLOT_COUNT) markPoint(g_app.sensors.tempC[slot], value, valid, faultFlags);
    xSemaphoreGive(sharedMutex);
}

void appMarkCurrent(uint8_t slot, float value, bool valid, uint16_t faultFlags) {
    if (!xSemaphoreTake(sharedMutex, pdMS_TO_TICKS(20))) return;
    if (slot < SLOT_COUNT) markPoint(g_app.sensors.currentA[slot], value, valid, faultFlags);
    xSemaphoreGive(sharedMutex);
}

void appMarkPressure(uint8_t slot, float value, bool valid, uint16_t faultFlags) {
    if (!xSemaphoreTake(sharedMutex, pdMS_TO_TICKS(20))) return;
    if (slot < SLOT_COUNT) markPoint(g_app.sensors.pressureBar[slot], value, valid, faultFlags);
    xSemaphoreGive(sharedMutex);
}

void appInvalidateAllSensors() {
    if (!xSemaphoreTake(sharedMutex, pdMS_TO_TICKS(50))) return;
    for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
        initSensorPoint(g_app.sensors.tempC[i]);
        initSensorPoint(g_app.sensors.currentA[i]);
        initSensorPoint(g_app.sensors.pressureBar[i]);
    }
    xSemaphoreGive(sharedMutex);
}

void appForceBootFaultState(int slot, fault_scope_t scope) {
    if (!xSemaphoreTake(sharedMutex, pdMS_TO_TICKS(50))) return;
    g_app.machineState = MACH_FAULT_LATCHED;
    for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
        g_app.jobs[i].remainingMs = 0;
        g_app.actuator.desiredSSR[i] = false;
        g_app.actuator.actualSSR[i] = false;
        if (scope == FAULT_SCOPE_GLOBAL || i == slot) {
            g_app.jobs[i].state = JOB_FAULT;
        } else if (g_app.jobs[i].state != JOB_FAULT) {
            g_app.jobs[i].state = JOB_IDLE;
        }
    }
    g_app.anyRunning = false;
    xSemaphoreGive(sharedMutex);
}
