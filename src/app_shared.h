#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "app_config.h"

// ---- System / job states ----
enum job_state_t : uint8_t {
    JOB_IDLE = 0,
    JOB_RUNNING,
    JOB_COMPLETED,
    JOB_FAULT
};

enum machine_state_t : uint8_t {
    MACH_BOOT = 0,
    MACH_IDLE,
    MACH_RUNNING,
    MACH_FAULT_LATCHED
};

enum system_mode_t
{
    MODE_PRODUCTION = 0,
    MODE_MAINTENANCE = 1,
    MODE_SIMULATION = 2
};

enum fault_scope_t : uint8_t {
    FAULT_SCOPE_SLOT = 0,
    FAULT_SCOPE_GLOBAL = 1
};

enum event_type_t : uint8_t {
    EVT_NONE = 0,
    EVT_CMD_START,
    EVT_CMD_STOP,
    EVT_CMD_SET_MODE,
    EVT_CMD_RESET_FAULT,
    EVT_TIMER_EXPIRED,
    EVT_FAULT,
    EVT_BOOT_RESTORE_DONE
};

struct SensorPoint {
    float value;
    bool valid;
    bool stale;
    uint32_t lastUpdateMs;
    uint16_t faultFlags;
};

struct SensorSnapshot {
    SensorPoint tempC[SLOT_COUNT];
    SensorPoint currentA[SLOT_COUNT];
    SensorPoint pressureBar[SLOT_COUNT];
};

struct JobSlot {
    job_state_t state;
    uint32_t remainingMs;
};

struct ActuatorState {
    bool desiredSSR[SLOT_COUNT];
    bool actualSSR[SLOT_COUNT];
    uint32_t lastChangeMs[SLOT_COUNT];
};

struct MachineRuntime {
    JobSlot jobs[SLOT_COUNT];
    SensorSnapshot sensors;
    ActuatorState actuator;
    machine_state_t machineState;
    system_mode_t systemMode;
    bool anyRunning;
};

struct MachineSnapshot {
    JobSlot jobs[SLOT_COUNT];
    SensorSnapshot sensors;
    ActuatorState actuator;
    machine_state_t machineState;
    system_mode_t systemMode;
    bool anyRunning;
};

struct event_t {
    event_type_t type;
    int slot;
    uint32_t durationMs;
    system_mode_t requestedMode;
    uint8_t faultCode;
    fault_scope_t faultScope;
    uint32_t ts;
};

enum actuator_cmd_type_t : uint8_t {
    ACT_CMD_SET_SLOT = 0,
    ACT_CMD_FORCE_ALL_OFF
};

struct actuator_cmd_t {
    actuator_cmd_type_t type;
    int slot;
    bool on;
    uint32_t ts;
};

extern SemaphoreHandle_t sharedMutex;
extern SemaphoreHandle_t i2cMutex;
extern QueueHandle_t eventQueue;
extern QueueHandle_t actuatorQueue;
extern MachineRuntime g_app;

bool appInitSharedObjects();
void appBootSafeDefaults();
bool appGetSnapshot(MachineSnapshot* out, TickType_t timeout = pdMS_TO_TICKS(50));
bool appSetSystemMode(system_mode_t mode, TickType_t timeout = pdMS_TO_TICKS(50));
bool appIsAnyJobRunning(TickType_t timeout = pdMS_TO_TICKS(50));
void appRecomputeAnyRunningUnsafe();
void appUpdateMachineStateUnsafe();
void appSetJobStateUnsafe(uint8_t slot, job_state_t state, uint32_t remainingMs);
void appSetActuatorDesiredUnsafe(uint8_t slot, bool on);
void appSetActuatorActualUnsafe(uint8_t slot, bool on);
void appMarkTemp(uint8_t slot, float value, bool valid, uint16_t faultFlags);
void appMarkCurrent(uint8_t slot, float value, bool valid, uint16_t faultFlags);
void appMarkPressure(uint8_t slot, float value, bool valid, uint16_t faultFlags);
void appInvalidateAllSensors();
void appForceBootFaultState(int slot, fault_scope_t scope);
