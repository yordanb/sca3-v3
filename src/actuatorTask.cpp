#include <Arduino.h>
#include "actuatorTask.h"
#include "app_shared.h"
#include "app_config.h"

void actuatorInitPins() {
    for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
        pinMode(SSR_PINS[i], OUTPUT);
        digitalWrite(SSR_PINS[i], LOW);
    }
}

bool actuatorRequestSlot(uint8_t slot, bool on) {
    if (slot >= SLOT_COUNT || !actuatorQueue) return false;
    actuator_cmd_t cmd{};
    cmd.type = ACT_CMD_SET_SLOT;
    cmd.slot = slot;
    cmd.on = on;
    cmd.ts = millis();
    return xQueueSend(actuatorQueue, &cmd, pdMS_TO_TICKS(20)) == pdTRUE;
}

bool actuatorRequestAllOff() {
    if (!actuatorQueue) return false;
    actuator_cmd_t cmd{};
    cmd.type = ACT_CMD_FORCE_ALL_OFF;
    cmd.slot = -1;
    cmd.on = false;
    cmd.ts = millis();
    return xQueueSend(actuatorQueue, &cmd, pdMS_TO_TICKS(20)) == pdTRUE;
}

void actuatorTask(void* pvParameters) {
    (void)pvParameters;
    actuator_cmd_t cmd{};

    for (;;) {
        if (xQueueReceive(actuatorQueue, &cmd, portMAX_DELAY) != pdTRUE) continue;

        if (cmd.type == ACT_CMD_FORCE_ALL_OFF) {
            for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
                digitalWrite(SSR_PINS[i], LOW);
            }
            if (xSemaphoreTake(sharedMutex, pdMS_TO_TICKS(50))) {
                for (uint8_t i = 0; i < SLOT_COUNT; ++i) {
                    appSetActuatorDesiredUnsafe(i, false);
                    appSetActuatorActualUnsafe(i, false);
                }
                xSemaphoreGive(sharedMutex);
            }
            continue;
        }

        if (cmd.slot < 0 || cmd.slot >= SLOT_COUNT) continue;

        digitalWrite(SSR_PINS[cmd.slot], cmd.on ? HIGH : LOW);
        if (xSemaphoreTake(sharedMutex, pdMS_TO_TICKS(50))) {
            appSetActuatorActualUnsafe(cmd.slot, cmd.on);
            xSemaphoreGive(sharedMutex);
        }
    }
}
