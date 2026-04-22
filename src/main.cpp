#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_MAX31856.h>
#include "nvs_flash.h"
#include "esp_system.h"

#include "app_config.h"
#include "app_shared.h"
#include "fault.h"
#include "fault_restore.h"
#include "actuatorTask.h"
#include "stateMachineTask.h"
#include "safetyTask.h"
#include "logger.h"

static Adafruit_ADS1115 adsACS;
static Adafruit_ADS1115 adsPRES;
static Adafruit_MAX31856 *thermos[SLOT_COUNT] = {nullptr, nullptr, nullptr, nullptr};
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);
static Preferences prefs;

static const char *PREF_NAMESPACE = "calib";
static const float OFFSET_MV_LIMIT = 1000.0f;
static float calibOffset_mV[SLOT_COUNT] = {0, 0, 0, 0};

static bool g_adsACSReady = false;
static bool g_adsPRESReady = false;
static bool g_tcReady[SLOT_COUNT] = {false, false, false, false};
static uint8_t g_tcZeroStreak[SLOT_COUNT] = {0, 0, 0, 0};
static uint8_t g_cjZeroStreak[SLOT_COUNT] = {0, 0, 0, 0};
static uint8_t g_stuckStreak[SLOT_COUNT] = {0, 0, 0, 0};
static float g_lastTcTemp[SLOT_COUNT] = {NAN, NAN, NAN, NAN};
static float g_lastCjTemp[SLOT_COUNT] = {NAN, NAN, NAN, NAN};

// Simulation values (used in simulation mode or as fallback if sensors are missing)
static float g_simTemp[SLOT_COUNT] = {25.0f, 25.0f, 25.0f, 25.0f};
static float g_simCurrent[SLOT_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f};
static float g_simPressure[SLOT_COUNT] = {1.0f, 1.0f, 1.0f, 1.0f};

static void updateSimulatedSensors()
{
    MachineSnapshot snap{};
    bool haveSnap = appGetSnapshot(&snap, pdMS_TO_TICKS(20));

    for (uint8_t i = 0; i < SLOT_COUNT; ++i)
    {
        bool heaterOn = false;
        if (haveSnap)
        {
            heaterOn = snap.actuator.actualSSR[i] || snap.actuator.desiredSSR[i];
        }

        if (heaterOn)
        {
            g_simTemp[i] += 0.15f;
            if (g_simTemp[i] > 180.0f)
                g_simTemp[i] = 180.0f;
            g_simCurrent[i] = 1.2f;
        }
        else
        {
            g_simTemp[i] -= 0.05f;
            if (g_simTemp[i] < 26.0f)
                g_simTemp[i] = 26.0f;
            g_simCurrent[i] = 0.05f;
        }

        g_simPressure[i] = 1.0f;

        appMarkTemp(i, g_simTemp[i], true, 0);
        appMarkCurrent(i, g_simCurrent[i], true, 0);
        appMarkPressure(i, g_simPressure[i], true, 0);
    }
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

static float adsToVoltage(int16_t raw)
{
    return raw * ADS_LSB_V;
}

static float adsToPressure(int16_t raw)
{
    float v = adsToVoltage(raw);
    v = constrain(v, PRESS_V_MIN, PRESS_V_MAX);
    return ((v - PRESS_V_MIN) / (PRESS_V_MAX - PRESS_V_MIN)) * PRESS_RANGE_BAR;
}

static float adsToCurrentCalibrated(int16_t raw, uint8_t ch)
{
    float v = adsToVoltage(raw);
    float vzero = ADC_VREF / 2.0f;
    float corrected = v - vzero - (calibOffset_mV[ch] / 1000.0f);
    return corrected / ACS_SENSITIVITY_V_PER_A;
}

static String prefKeyForCurr(uint8_t ch)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "curr_%u", static_cast<unsigned>(ch));
    return String(buf);
}

static const char *wifiStatusToStr(wl_status_t s)
{
    switch (s)
    {
    case WL_IDLE_STATUS:
        return "IDLE";
    case WL_NO_SSID_AVAIL:
        return "NO_SSID";
    case WL_SCAN_COMPLETED:
        return "SCAN_DONE";
    case WL_CONNECTED:
        return "CONNECTED";
    case WL_CONNECT_FAILED:
        return "CONNECT_FAILED";
    case WL_CONNECTION_LOST:
        return "CONNECTION_LOST";
    case WL_DISCONNECTED:
        return "DISCONNECTED";
    default:
        return "UNKNOWN";
    }
}

static bool g_wifiStarted = false;

static const char *resetReasonToStr(esp_reset_reason_t r)
{
    switch (r)
    {
    case ESP_RST_UNKNOWN:
        return "UNKNOWN";
    case ESP_RST_POWERON:
        return "POWERON";
    case ESP_RST_EXT:
        return "EXTERNAL";
    case ESP_RST_SW:
        return "SOFTWARE";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT_WDT";
    case ESP_RST_TASK_WDT:
        return "TASK_WDT";
    case ESP_RST_WDT:
        return "OTHER_WDT";
    case ESP_RST_DEEPSLEEP:
        return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
    default:
        return "UNMAPPED";
    }
}

static void formatMax31856Fault(uint16_t fault, char *out, size_t len)
{
    if (!out || len == 0)
        return;

    if (fault == 0)
    {
        snprintf(out, len, "NONE");
        return;
    }

    out[0] = '\0';

    if (fault & MAX31856_FAULT_OPEN)
        strncat(out, "OPEN ", len - strlen(out) - 1);
    if (fault & MAX31856_FAULT_OVUV)
        strncat(out, "OVUV ", len - strlen(out) - 1);
    if (fault & MAX31856_FAULT_TCLOW)
        strncat(out, "TCLOW ", len - strlen(out) - 1);
    if (fault & MAX31856_FAULT_TCHIGH)
        strncat(out, "TCHIGH ", len - strlen(out) - 1);
    if (fault & MAX31856_FAULT_CJLOW)
        strncat(out, "CJLOW ", len - strlen(out) - 1);
    if (fault & MAX31856_FAULT_CJHIGH)
        strncat(out, "CJHIGH ", len - strlen(out) - 1);
    if (fault & MAX31856_FAULT_TCRANGE)
        strncat(out, "TCRANGE ", len - strlen(out) - 1);
    if (fault & MAX31856_FAULT_CJRANGE)
        strncat(out, "CJRANGE ", len - strlen(out) - 1);
}

static void scanI2C()
{
    LOGI("BOOT", "I2C scan start");
    uint8_t found = 0;

    for (uint8_t addr = 1; addr < 127; ++addr)
    {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0)
        {
            LOGI("BOOT", "I2C device found addr=0x%02X", addr);
            found++;
        }
    }

    LOGI("BOOT", "I2C scan done found=%u", found);
}

static void logSensorSummary()
{
    uint8_t tcReadyCount = 0;
    for (uint8_t i = 0; i < SLOT_COUNT; ++i)
    {
        if (g_tcReady[i])
        {
            tcReadyCount++;
        }
    }

    LOGI("BOOT",
         "sensor summary adsACS=%s adsPRES=%s tcInit=%u/%u",
         g_adsACSReady ? "ready" : "missing",
         g_adsPRESReady ? "ready" : "missing",
         static_cast<unsigned>(tcReadyCount),
         static_cast<unsigned>(SLOT_COUNT));
}

static void logRuntimeSensorSummary()
{
    MachineSnapshot snap{};
    if (!appGetSnapshot(&snap, pdMS_TO_TICKS(20)))
        return;

    uint8_t tempValidCount = 0;
    uint8_t currentValidCount = 0;
    uint8_t pressureValidCount = 0;

    for (uint8_t i = 0; i < SLOT_COUNT; ++i)
    {
        if (snap.sensors.tempC[i].valid)
            tempValidCount++;
        if (snap.sensors.currentA[i].valid)
            currentValidCount++;
        if (snap.sensors.pressureBar[i].valid)
            pressureValidCount++;
    }

    LOGI("SENSOR",
         "runtime summary tempValid=%u/%u currentValid=%u/%u pressureValid=%u/%u",
         static_cast<unsigned>(tempValidCount),
         static_cast<unsigned>(SLOT_COUNT),
         static_cast<unsigned>(currentValidCount),
         static_cast<unsigned>(SLOT_COUNT),
         static_cast<unsigned>(pressureValidCount),
         static_cast<unsigned>(SLOT_COUNT));
}

static void logNetworkSummary(const char *tag = "BOOT")
{
    wl_status_t st = static_cast<wl_status_t>(WiFi.status());

    LOGI(tag,
         "network summary wifi=%s mqttHost=%s mqttPort=%u",
         wifiStatusToStr(st),
         g_cfg.mqttHost,
         static_cast<unsigned>(g_cfg.mqttPort));
}

static void logMqttSummary()
{
    LOGI("MQTT",
         "summary connected=%s state=%d server=%s:%u",
         mqttClient.connected() ? "yes" : "no",
         mqttClient.state(),
         g_cfg.mqttHost,
         static_cast<unsigned>(g_cfg.mqttPort));
}

static void loadCalibrationOffsets()
{
    prefs.begin(PREF_NAMESPACE, true);
    for (uint8_t i = 0; i < SLOT_COUNT; ++i)
    {
        calibOffset_mV[i] = prefs.getFloat(prefKeyForCurr(i).c_str(), 0.0f);
    }
    prefs.end();
}

static bool saveCalibrationOffset(uint8_t ch, float offset_mV)
{
    if (ch >= SLOT_COUNT || fabsf(offset_mV) > OFFSET_MV_LIMIT)
        return false;

    prefs.begin(PREF_NAMESPACE, false);
    bool ok = prefs.putFloat(prefKeyForCurr(ch).c_str(), offset_mV) > 0;
    prefs.end();

    calibOffset_mV[ch] = offset_mV;
    return ok;
}

static bool resetCalibrationOffset(uint8_t ch)
{
    if (ch >= SLOT_COUNT)
        return false;

    prefs.begin(PREF_NAMESPACE, false);
    prefs.remove(prefKeyForCurr(ch).c_str());
    prefs.end();

    calibOffset_mV[ch] = 0.0f;
    return true;
}

static void setupWiFi()
{
    WiFi.mode(WIFI_STA);

    if (WiFi.status() == WL_CONNECTED)
    {
        return;
    }

    if (!g_wifiStarted)
    {
        WiFi.begin(g_cfg.wifiSsid, g_cfg.wifiPass);
        g_wifiStarted = true;
        LOGI("NET", "WiFi begin ssid=%s", g_cfg.wifiSsid);
    }

    for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; ++i)
    {
        delay(500);
        wl_status_t st = static_cast<wl_status_t>(WiFi.status());
        LOGD("NET", "waiting WiFi attempt=%d status=%d(%s)",
             i + 1,
             static_cast<int>(st),
             wifiStatusToStr(st));
    }

    wl_status_t st = static_cast<wl_status_t>(WiFi.status());
    if (st == WL_CONNECTED)
    {
        LOGI("NET", "WiFi connected ip=%s", WiFi.localIP().toString().c_str());
        logNetworkSummary("NET");
    }
    else
    {
        LOGW("NET", "WiFi not ready status=%d(%s)",
             static_cast<int>(st),
             wifiStatusToStr(st));
    }
}

static bool publishJson(const char *topic, JsonDocument &doc)
{
    if (!mqttClient.connected())
    {
        LOGW("MQTT", "publish skipped not connected topic=%s", topic);
        return false;
    }

    char buf[1024];
    size_t needed = measureJson(doc);

    if (needed >= sizeof(buf))
    {
        LOGW("MQTT", "json too large topic=%s needed=%u",
             topic,
             static_cast<unsigned>(needed));
        return false;
    }

    size_t n = serializeJson(doc, buf, sizeof(buf));
    bool ok = mqttClient.publish(topic, buf, n);

    LOGI("MQTT", "publish topic=%s len=%u ok=%u",
         topic,
         static_cast<unsigned>(n),
         ok ? 1 : 0);

    return ok;
}

static void publishHeartbeat()
{
    MachineSnapshot snap{};
    if (!appGetSnapshot(&snap, pdMS_TO_TICKS(50)))
        return;

    JsonDocument doc;
    doc["esp_id"] = g_cfg.mqttClientId;
    doc["machine_state"] = static_cast<uint8_t>(snap.machineState);
    doc["mode"] = modeToStr(snap.systemMode);
    doc["simulated"] = (snap.systemMode == MODE_SIMULATION);
    doc["anyRunning"] = snap.anyRunning;
    doc["uptime_ms"] = millis();
    doc["freeHeap"] = esp_get_free_heap_size();

    publishJson(g_cfg.topicStatus, doc);
}

static void publishTelemetry()
{
    MachineSnapshot snap{};
    if (!appGetSnapshot(&snap, pdMS_TO_TICKS(50)))
        return;

    JsonDocument doc;
    doc["esp_id"] = g_cfg.mqttClientId;
    doc["machine_state"] = static_cast<uint8_t>(snap.machineState);
    doc["mode"] = modeToStr(snap.systemMode);
    doc["simulated"] = (snap.systemMode == MODE_SIMULATION);
    doc["anyRunning"] = snap.anyRunning;

    JsonArray temps = doc["tempC"].to<JsonArray>();
    JsonArray tempValid = doc["temp_valid"].to<JsonArray>();
    JsonArray currents = doc["currentA"].to<JsonArray>();
    JsonArray currentValid = doc["current_valid"].to<JsonArray>();
    JsonArray pressures = doc["pressureBar"].to<JsonArray>();
    JsonArray pressureValid = doc["pressure_valid"].to<JsonArray>();
    JsonArray desired = doc["ssr_desired"].to<JsonArray>();
    JsonArray actual = doc["ssr_actual"].to<JsonArray>();
    JsonArray remaining = doc["remaining_ms"].to<JsonArray>();

    for (uint8_t i = 0; i < SLOT_COUNT; ++i)
    {
        if (snap.sensors.tempC[i].valid)
            temps.add(roundf(snap.sensors.tempC[i].value * 10.0f) / 10.0f);
        else
            temps.add(nullptr);
        tempValid.add(snap.sensors.tempC[i].valid);

        if (snap.sensors.currentA[i].valid)
            currents.add(roundf(snap.sensors.currentA[i].value * 100.0f) / 100.0f);
        else
            currents.add(nullptr);
        currentValid.add(snap.sensors.currentA[i].valid);

        if (snap.sensors.pressureBar[i].valid)
            pressures.add(roundf(snap.sensors.pressureBar[i].value * 100.0f) / 100.0f);
        else
            pressures.add(nullptr);
        pressureValid.add(snap.sensors.pressureBar[i].valid);

        desired.add(snap.actuator.desiredSSR[i]);
        actual.add(snap.actuator.actualSSR[i]);
        remaining.add(snap.jobs[i].remainingMs);
    }

    publishJson(g_cfg.topicTelemetry, doc);
}

static void publishFaultStatus()
{
    fault_latch_t f = fault_get_snapshot();
    if (!f.active)
        return;

    JsonDocument doc;
    doc["evt"] = "fault";
    doc["esp_id"] = g_cfg.mqttClientId;
    doc["code"] = static_cast<uint8_t>(f.code);
    doc["scope"] = static_cast<uint8_t>(f.scope);
    doc["slot"] = f.slot;
    doc["source"] = f.source;
    doc["timestamp_ms"] = f.timestamp;

    publishJson(g_cfg.topicEvent, doc);
}

static void mqttReconnectIfNeeded()
{
    wl_status_t st = static_cast<wl_status_t>(WiFi.status());

    if (!g_wifiStarted)
    {
        return;
    }

    if (st != WL_CONNECTED)
    {
        return;
    }

    if (mqttClient.connected())
        return;

    LOGI("MQTT", "connecting host=%s port=%u",
         g_cfg.mqttHost,
         static_cast<unsigned>(g_cfg.mqttPort));

    if (mqttClient.connect(g_cfg.mqttClientId, g_cfg.mqttUser, g_cfg.mqttPass))
    {
        LOGI("MQTT", "connected broker=%s topic=%s", g_cfg.mqttHost, g_cfg.topicControl);
        mqttClient.subscribe(g_cfg.topicControl);
        logMqttSummary();

        JsonDocument boot;
        boot["evt"] = "boot";
        boot["esp_id"] = g_cfg.mqttClientId;
        boot["ip"] = WiFi.localIP().toString();
        publishJson(g_cfg.topicStatus, boot);
    }
    else
    {
        LOGW("MQTT", "connect failed state=%d", mqttClient.state());
    }
}

static bool enqueueCommand(const event_t &evt)
{
    return xQueueSend(eventQueue, &evt, pdMS_TO_TICKS(20)) == pdTRUE;
}

static void publishCmdAck(const char *cmd, bool accepted, const char *reason, int slot = -1)
{
    JsonDocument doc;
    doc["evt"] = "cmd_ack";
    doc["cmd"] = cmd;
    doc["accepted"] = accepted;
    doc["reason"] = reason;
    doc["slot"] = slot;
    doc["esp_id"] = g_cfg.mqttClientId;

    publishJson(g_cfg.topicEvent, doc);
}

static void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    (void)topic;

    JsonDocument doc;
    if (deserializeJson(doc, payload, length))
    {
        publishCmdAck("unknown", false, "json_parse_error");
        return;
    }

    const char *cmd = doc["cmd"];
    if (!cmd)
    {
        publishCmdAck("unknown", false, "missing_cmd");
        return;
    }

    event_t evt{};
    evt.slot = doc["slot"] | -1;
    evt.ts = millis();

    if (!strcmp(cmd, "start"))
    {
        evt.type = EVT_CMD_START;
        evt.durationMs = (doc["duration_s"] | 0) * 1000UL;

        if (evt.slot < 0 || evt.slot >= SLOT_COUNT || evt.durationMs == 0)
        {
            publishCmdAck(cmd, false, "invalid_param", evt.slot);
            return;
        }

        bool ok = enqueueCommand(evt);
        publishCmdAck(cmd, ok, ok ? "queued" : "queue_full", evt.slot);
        return;
    }

    if (!strcmp(cmd, "stop"))
    {
        evt.type = EVT_CMD_STOP;

        if (evt.slot < 0 || evt.slot >= SLOT_COUNT)
        {
            publishCmdAck(cmd, false, "invalid_param", evt.slot);
            return;
        }

        bool ok = enqueueCommand(evt);
        publishCmdAck(cmd, ok, ok ? "queued" : "queue_full", evt.slot);
        return;
    }

    if (!strcmp(cmd, "set_mode"))
    {
        const char *mode = doc["mode"];
        if (!mode)
        {
            publishCmdAck(cmd, false, "missing_mode");
            return;
        }

        evt.type = EVT_CMD_SET_MODE;

        if (!strcmp(mode, "production"))
        {
            evt.requestedMode = MODE_PRODUCTION;
        }
        else if (!strcmp(mode, "maintenance"))
        {
            evt.requestedMode = MODE_MAINTENANCE;
        }
        else if (!strcmp(mode, "simulation"))
        {
            evt.requestedMode = MODE_SIMULATION;
        }
        else
        {
            publishCmdAck(cmd, false, "invalid_mode");
            return;
        }

        bool ok = enqueueCommand(evt);
        publishCmdAck(cmd, ok, ok ? "queued" : "queue_full");
        return;
    }

    if (!strcmp(cmd, "reset_fault"))
    {
        evt.type = EVT_CMD_RESET_FAULT;

        bool ok = enqueueCommand(evt);
        publishCmdAck(cmd, ok, ok ? "queued" : "queue_full");
        return;
    }

    if (!strcmp(cmd, "calibrate_zero"))
    {
        float offset = doc["offset_mV"] | NAN;

        if (evt.slot < 0 || evt.slot >= SLOT_COUNT || isnan(offset))
        {
            publishCmdAck(cmd, false, "invalid_param", evt.slot);
            return;
        }

        bool ok = saveCalibrationOffset(evt.slot, offset);
        publishCmdAck(cmd, ok, ok ? "applied" : "save_failed", evt.slot);
        return;
    }

    if (!strcmp(cmd, "reset_calibration"))
    {
        if (evt.slot < 0 || evt.slot >= SLOT_COUNT)
        {
            publishCmdAck(cmd, false, "invalid_param", evt.slot);
            return;
        }

        bool ok = resetCalibrationOffset(evt.slot);
        publishCmdAck(cmd, ok, ok ? "applied" : "reset_failed", evt.slot);
        return;
    }

    publishCmdAck(cmd, false, "unknown_cmd", evt.slot);
}

static void core1SensorTask(void *pvParameters)
{
    (void)pvParameters;
    static uint8_t tcDbgDiv = 0;

    for (;;)
    {
        MachineSnapshot snap{};
        bool haveSnap = appGetSnapshot(&snap, pdMS_TO_TICKS(20));
        bool simMode = haveSnap && (snap.systemMode == MODE_SIMULATION);

        if (simMode)
        {
            updateSimulatedSensors();

            tcDbgDiv++;
            if (tcDbgDiv >= 10)
            {
                tcDbgDiv = 0;
                LOGD("SENSOR", "simulation sensors updated");
            }

            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)))
        {
            if (g_adsACSReady)
            {
                for (uint8_t ch = 0; ch < SLOT_COUNT; ++ch)
                {
                    int16_t raw = adsACS.readADC_SingleEnded(ch);
                    appMarkCurrent(ch, adsToCurrentCalibrated(raw, ch), true, 0);
                }
            }
            else
            {
                for (uint8_t ch = 0; ch < SLOT_COUNT; ++ch)
                {
                    appMarkCurrent(ch, 0.0f, false, 1);
                }
            }

            if (g_adsPRESReady)
            {
                for (uint8_t ch = 0; ch < SLOT_COUNT; ++ch)
                {
                    int16_t raw = adsPRES.readADC_SingleEnded(ch);
                    appMarkPressure(ch, adsToPressure(raw), true, 0);
                }
            }
            else
            {
                for (uint8_t ch = 0; ch < SLOT_COUNT; ++ch)
                {
                    appMarkPressure(ch, 0.0f, false, 1);
                }
            }

            xSemaphoreGive(i2cMutex);
        }
        else
        {
            LOGW("SENSOR", "i2c mutex timeout");
        }

        tcDbgDiv++;
        bool doTcDbg = (tcDbgDiv >= 10); // ~2 detik
        if (doTcDbg)
            tcDbgDiv = 0;

        for (uint8_t i = 0; i < SLOT_COUNT; ++i)
        {
            float tcTemp = NAN;
            float cjTemp = NAN;
            bool valid = false;
            uint16_t faultFlags = 0;

            if (thermos[i] && g_tcReady[i])
            {
                faultFlags = thermos[i]->readFault();
                cjTemp = thermos[i]->readCJTemperature();
                tcTemp = thermos[i]->readThermocoupleTemperature();

                bool tempsFinite = !isnan(tcTemp) && !isnan(cjTemp);
                bool tcRangeOk = (tcTemp >= -50.0f && tcTemp <= 500.0f);
                bool cjRangeOk = (cjTemp >= 5.0f && cjTemp <= 85.0f);

                bool tcLooksZero = fabsf(tcTemp) < 0.01f;
                bool cjLooksZero = fabsf(cjTemp) < 0.01f;

                if (tcLooksZero)
                {
                    if (g_tcZeroStreak[i] < 255)
                        g_tcZeroStreak[i]++;
                }
                else
                {
                    g_tcZeroStreak[i] = 0;
                }

                if (cjLooksZero)
                {
                    if (g_cjZeroStreak[i] < 255)
                        g_cjZeroStreak[i]++;
                }
                else
                {
                    g_cjZeroStreak[i] = 0;
                }

                bool stuckNow =
                    !isnan(g_lastTcTemp[i]) &&
                    !isnan(g_lastCjTemp[i]) &&
                    fabsf(tcTemp - g_lastTcTemp[i]) < 0.01f &&
                    fabsf(cjTemp - g_lastCjTemp[i]) < 0.01f;

                if (stuckNow)
                {
                    if (g_stuckStreak[i] < 255)
                        g_stuckStreak[i]++;
                }
                else
                {
                    g_stuckStreak[i] = 0;
                }

                g_lastTcTemp[i] = tcTemp;
                g_lastCjTemp[i] = cjTemp;

                bool zeroPatternPersistent = (g_tcZeroStreak[i] >= 5) || (g_cjZeroStreak[i] >= 5);
                bool stuckPersistent = (g_stuckStreak[i] >= 20);

                valid = (faultFlags == 0) &&
                        tempsFinite &&
                        tcRangeOk &&
                        cjRangeOk &&
                        !zeroPatternPersistent &&
                        !stuckPersistent;
            }
            else
            {
                faultFlags = 0xFFFF;
                g_tcZeroStreak[i] = 255;
                g_cjZeroStreak[i] = 255;
                g_stuckStreak[i] = 255;
                g_lastTcTemp[i] = NAN;
                g_lastCjTemp[i] = NAN;
            }

            if (doTcDbg)
            {
                char faultText[96];
                formatMax31856Fault(faultFlags, faultText, sizeof(faultText));

                LOGD("SENSOR",
                     "TC[%u] tc=%.2f cj=%.2f fault=0x%02X(%s) ztc=%u zcj=%u stuck=%u valid=%u",
                     i,
                     tcTemp,
                     cjTemp,
                     faultFlags,
                     faultText,
                     g_tcZeroStreak[i],
                     g_cjZeroStreak[i],
                     g_stuckStreak[i],
                     valid ? 1 : 0);
            }

            appMarkTemp(i, valid ? tcTemp : 0.0f, valid, faultFlags);
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void timerTickTask(void *pvParameters)
{
    (void)pvParameters;
    static bool expiredSent[SLOT_COUNT] = {false, false, false, false};

    for (;;)
    {
        if (xSemaphoreTake(sharedMutex, pdMS_TO_TICKS(20)))
        {
            for (uint8_t i = 0; i < SLOT_COUNT; ++i)
            {
                if (g_app.jobs[i].state != JOB_RUNNING)
                {
                    expiredSent[i] = false;
                    continue;
                }

                if (g_app.jobs[i].remainingMs > 200)
                {
                    g_app.jobs[i].remainingMs -= 200;
                }
                else
                {
                    g_app.jobs[i].remainingMs = 0;
                }

                if (g_app.jobs[i].remainingMs == 0 && !expiredSent[i])
                {
                    expiredSent[i] = true;

                    event_t evt{};
                    evt.type = EVT_TIMER_EXPIRED;
                    evt.slot = i;
                    evt.ts = millis();

                    xQueueSend(eventQueue, &evt, 0);
                }
            }
            xSemaphoreGive(sharedMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void telemetryAndControlTask(void *pvParameters)
{
    (void)pvParameters;

    uint32_t lastTelemetryMs = 0;
    uint32_t lastHeartbeatMs = 0;
    uint32_t lastWifiRetryMs = 0;
    uint32_t lastSensorSummaryMs = 0;
    uint32_t lastMqttRetryMs = 0;
    uint32_t lastGateLogMs = 0;
    bool lastFaultPublished = false;

    for (;;)
    {
        uint32_t now = millis();

        if (WiFi.status() != WL_CONNECTED && now - lastWifiRetryMs >= 5000)
        {
            lastWifiRetryMs = now;
            setupWiFi();
        }

        if (WiFi.status() == WL_CONNECTED && now - lastMqttRetryMs >= 5000)
        {
            lastMqttRetryMs = now;
            mqttReconnectIfNeeded();
        }

        mqttClient.loop();

        MachineSnapshot snap{};
        if (appGetSnapshot(&snap, pdMS_TO_TICKS(20)))
        {
            if (now - lastGateLogMs >= 2000)
            {
                lastGateLogMs = now;
                LOGI("MQTT", "telemetry gate anyRunning=%u mode=%s machineState=%u",
                     snap.anyRunning ? 1 : 0,
                     modeToStr(snap.systemMode),
                     static_cast<unsigned>(snap.machineState));
            }

            if (snap.anyRunning && now - lastTelemetryMs >= g_cfg.telemetryIntervalRunningMs)
            {
                LOGI("MQTT", "telemetry trigger anyRunning=%u mode=%s",
                     snap.anyRunning ? 1 : 0,
                     modeToStr(snap.systemMode));
                publishTelemetry();
                lastTelemetryMs = now;
            }

            if (!snap.anyRunning && now - lastHeartbeatMs >= g_cfg.heartbeatIntervalIdleMs)
            {
                LOGI("MQTT", "heartbeat trigger anyRunning=%u mode=%s",
                     snap.anyRunning ? 1 : 0,
                     modeToStr(snap.systemMode));
                publishHeartbeat();
                lastHeartbeatMs = now;
            }

            if (now - lastSensorSummaryMs >= 10000)
            {
                logRuntimeSensorSummary();
                lastSensorSummaryMs = now;
            }
        }

        bool faultActive = fault_is_active();
        if (faultActive && !lastFaultPublished)
        {
            publishFaultStatus();
        }
        lastFaultPublished = faultActive;

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void createTaskOrHalt(TaskFunction_t fn,
                             const char *taskName,
                             uint32_t stackWords,
                             UBaseType_t priority,
                             BaseType_t core)
{
    BaseType_t ok = xTaskCreatePinnedToCore(
        fn,
        taskName,
        stackWords,
        nullptr,
        priority,
        nullptr,
        core);

    if (ok == pdPASS)
    {
        LOGI("BOOT", "task started: %s", taskName);
    }
    else
    {
        LOGE("BOOT", "task create failed: %s", taskName);
        while (true)
        {
            delay(1000);
        }
    }
}

void setup()
{
    log_init(115200);

    esp_reset_reason_t rr = esp_reset_reason();
    LOGI("BOOT", "startup begin");
    LOGI("BOOT", "reset reason=%d(%s)", static_cast<int>(rr), resetReasonToStr(rr));
    delay(100);

    actuatorInitPins();

    if (!appInitSharedObjects())
    {
        LOGE("BOOT", "failed to create mutex/queue");
        while (true)
        {
            delay(1000);
        }
    }

    appBootSafeDefaults();

    if (nvs_flash_init() != ESP_OK)
    {
        LOGE("BOOT", "nvs init failed");
    }

    fault_init();
    LOGI("BOOT", "fault manager initialized");

    loadCalibrationOffsets();
    LOGI("BOOT", "calibration offsets loaded");

    LOGI("BOOT", "I2C init sda=%u scl=%u", I2C_SDA, I2C_SCL);
    Wire.begin(I2C_SDA, I2C_SCL);
    scanI2C();

    LOGI("BOOT", "ADS ACS begin addr=0x%02X", ADS_ADDR_ACS);
    g_adsACSReady = adsACS.begin(ADS_ADDR_ACS, &Wire);
    if (!g_adsACSReady)
    {
        LOGE("BOOT", "ADS ACS not found");
    }
    else
    {
        LOGI("BOOT", "ADS ACS ready addr=0x%02X", ADS_ADDR_ACS);
    }

    LOGI("BOOT", "ADS PRES begin addr=0x%02X", ADS_ADDR_PRES);
    g_adsPRESReady = adsPRES.begin(ADS_ADDR_PRES, &Wire);
    if (!g_adsPRESReady)
    {
        LOGE("BOOT", "ADS PRES not found");
    }
    else
    {
        LOGI("BOOT", "ADS PRES ready addr=0x%02X", ADS_ADDR_PRES);
    }

    for (uint8_t i = 0; i < SLOT_COUNT; ++i)
    {
        thermos[i] = new Adafruit_MAX31856(MAX31856_CS[i]);

        if (!thermos[i]->begin())
        {
            g_tcReady[i] = false;
            LOGE("BOOT", "MAX31856 #%u init failed cs=%u",
                 static_cast<unsigned>(i),
                 static_cast<unsigned>(MAX31856_CS[i]));
            continue;
        }

        thermos[i]->setThermocoupleType(MAX31856_TCTYPE_J);
        g_tcReady[i] = true;

        LOGI("BOOT", "MAX31856 #%u init ok cs=%u",
             static_cast<unsigned>(i),
             static_cast<unsigned>(MAX31856_CS[i]));
    }

    logSensorSummary();

    mqttClient.setServer(g_cfg.mqttHost, g_cfg.mqttPort);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setSocketTimeout(2);
    mqttClient.setBufferSize(1024);

    LOGI("BOOT", "mqtt configured host=%s port=%u",
         g_cfg.mqttHost,
         static_cast<unsigned>(g_cfg.mqttPort));
    LOGI("BOOT", "mqtt buffer size=%u", 1024U);
    
    LOGI("BOOT", "topics control=%s telemetry=%s status=%s event=%s",
         g_cfg.topicControl,
         g_cfg.topicTelemetry,
         g_cfg.topicStatus,
         g_cfg.topicEvent);

    logNetworkSummary("BOOT");

    createTaskOrHalt(actuatorTask, "Actuator", 4096, 4, 0);
    createTaskOrHalt(stateMachineTask, "StateMachine", 6144, 3, 0);
    createTaskOrHalt(safetyTask, "Safety", 4096, 2, 0);
    createTaskOrHalt(timerTickTask, "Timer", 4096, 2, 0);
    createTaskOrHalt(core1SensorTask, "Sensors", 8192, 1, 1);
    createTaskOrHalt(telemetryAndControlTask, "MQTT", 12288, 1, 0);

    faultRestoreFromNVS();
    LOGI("BOOT", "fault restore checked");

    // setupWiFi();
    // logNetworkSummary("BOOT");
    LOGI("BOOT", "startup complete");
}

void loop()
{
    vTaskDelay(portMAX_DELAY);
}