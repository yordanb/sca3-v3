#include "app_config.h"

const AppConfig g_cfg = {
    .wifiSsid = "Aira alfa",
    .wifiPass = "bismillah",
    .mqttHost = "192.168.100.12",
    .mqttPort = 1883,
    .mqttClientId = "ESP32Heater01",
    .mqttUser = "edgeuser",
    .mqttPass = "Bismillah#2026",

    .topicTelemetry = "/esp32/heater/telemetry",
    .topicControl = "/esp32/heater/control",
    .topicStatus = "/esp32/heater/status",
    .topicEvent = "/esp32/heater/event",

    .maxTempC = 250.0f,
    .maxCurrentA = 18.0f,
    .maxPressureBar = 10.0f,
    .sensorStaleTimeoutMs = 2000,

    .maxTotalCurrentA = 35.0f,
    .startMarginA = 1.5f,
    .slotNominalCurrentA = {6.0f, 6.0f, 6.0f, 6.0f},

    .telemetryIntervalRunningMs = 1000UL,
    .heartbeatIntervalIdleMs = 30000UL,
};

const uint8_t SSR_PINS[SLOT_COUNT] = {18, 19, 23};

const float ADS_LSB_V = 0.1875e-3f;
const float ADC_VREF = 5.0f;
const float ACS_SENSITIVITY_V_PER_A = 0.100f;
const float PRESS_V_MIN = 0.5f;
const float PRESS_V_MAX = 4.5f;
const float PRESS_RANGE_BAR = 10.0f;
