#pragma once

#include <Arduino.h>

// ================= I2C =================
static constexpr uint8_t I2C_SDA = 21;
static constexpr uint8_t I2C_SCL = 22;

// ================= SPI =================
static constexpr uint8_t SPI_SCK = 18;
static constexpr uint8_t SPI_MISO = 19;
static constexpr uint8_t SPI_MOSI = 23;

// ================= MAX31856 CS =================
static constexpr uint8_t MAX31856_CS[4] = {5, 17, 16, 4};

// ================= ADS1115 =================
static constexpr uint8_t ADS_ADDR_ACS = 0x48;
static constexpr uint8_t ADS_ADDR_PRES = 0x49;

static constexpr uint8_t SLOT_COUNT = 4;

struct AppConfig {
    const char* wifiSsid;
    const char* wifiPass;
    const char* mqttHost;
    uint16_t mqttPort;
    const char* mqttClientId;
    const char* mqttUser;
    const char* mqttPass;

    const char* topicTelemetry;
    const char* topicControl;
    const char* topicStatus;
    const char* topicEvent;

    float maxTempC;
    float maxCurrentA;
    float maxPressureBar;
    uint32_t sensorStaleTimeoutMs;

    float maxTotalCurrentA;
    float startMarginA;
    float slotNominalCurrentA[SLOT_COUNT];

    uint32_t telemetryIntervalRunningMs;
    uint32_t heartbeatIntervalIdleMs;
};

extern const AppConfig g_cfg;

extern const uint8_t SSR_PINS[SLOT_COUNT];
extern const uint8_t MAX31856_CS[SLOT_COUNT];
extern const uint8_t ADS_ADDR_ACS;
extern const uint8_t ADS_ADDR_PRES;

extern const float ADS_LSB_V;
extern const float ADC_VREF;
extern const float ACS_SENSITIVITY_V_PER_A;
extern const float PRESS_V_MIN;
extern const float PRESS_V_MAX;
extern const float PRESS_RANGE_BAR;
