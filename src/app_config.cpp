#include "app_config.h"
#include "secrets.h"

const AppConfig g_cfg = {
    .wifiSsid = WIFI_SSID,
    .wifiPass = WIFI_PASS,

    .mqttHost = MQTT_HOST,
    .mqttPort = MQTT_PORT,
    .mqttClientId = MQTT_CLIENT_ID,
    .mqttUser = MQTT_USER,
    .mqttPass = MQTT_PASS,

    // private
    /*
    .topicTelemetry = "/esp32/heater/telemetry",
    .topicControl = "/esp32/heater/control",
    .topicStatus = "/esp32/heater/status",
    .topicEvent = "/esp32/heater/event",
    */

    // public
    
    .topicTelemetry = "/aira/sca3/esp32_001/telemetry",
    .topicControl = "/aira/sca3/esp32_001/control",
    .topicStatus = "/aira/sca3/esp32_001/status",
    .topicEvent = "/aira/sca3/esp32_001/event",
    

    .maxTempC = 250.0f,
    .maxCurrentA = 18.0f,
    .maxPressureBar = 10.0f,
    .sensorStaleTimeoutMs = 2000UL,

    .maxTotalCurrentA = 35.0f,
    .startMarginA = 1.5f,
    .slotNominalCurrentA = {6.0f, 6.0f, 6.0f, 6.0f},

    .telemetryIntervalRunningMs = 1000UL,
    .heartbeatIntervalIdleMs = 30000UL,
};

const uint8_t SSR_PINS[SLOT_COUNT] = {33, 25, 26, 27};

const float ADS_LSB_V = 0.1875e-3f;
const float ADC_VREF = 5.0f;
const float ACS_SENSITIVITY_V_PER_A = 0.100f;
const float PRESS_V_MIN = 0.5f;
const float PRESS_V_MAX = 4.5f;
const float PRESS_RANGE_BAR = 10.0f;