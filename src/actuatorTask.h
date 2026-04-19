#pragma once

#include <stdbool.h>
#include <stdint.h>

void actuatorInitPins();
bool actuatorRequestSlot(uint8_t slot, bool on);
bool actuatorRequestAllOff();
void actuatorTask(void* pvParameters);
