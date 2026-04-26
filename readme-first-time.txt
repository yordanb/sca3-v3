Readme yang digenerate lengkap pertama kali oleh ChatGPT.

# Smart Curing Controller (ESP32)

Firmware berbasis **ESP32** untuk sistem **multi-slot heating / curing control** dengan monitoring sensor real-time, fault handling, persistent fault storage, dan integrasi MQTT.

Project ini ditujukan untuk kebutuhan kontrol proses yang membutuhkan:
- kontrol heater per slot
- pembacaan temperatur berbasis thermocouple
- monitoring arus dan tekanan
- fault handling yang fail-safe
- integrasi ke HMI / dashboard / broker MQTT

---

## Highlights

- **4-slot heater control**
- **Thermocouple MAX31856** per slot
- **Current sensing** via ADS1115
- **Pressure sensing** via ADS1115
- **MQTT command / telemetry / event**
- **FreeRTOS task-based architecture**
- **Persistent fault storage (NVS)**
- **Structured logging (`LOGE`, `LOGW`, `LOGI`, `LOGD`)**
- **Runtime sensor validation**
- **Fail-safe behavior for invalid sensors**

---

## Project Status

Project ini sudah memiliki fondasi yang kuat untuk:
- prototyping
- pilot deployment
- integrasi dashboard / broker

Beberapa area yang masih penting untuk validasi lapangan:
- tuning sensor validation untuk semua channel thermocouple
- verifikasi wiring fisik per slot
- pengujian fault path pada kondisi proses nyata
- hardening command/API untuk deployment multi-device

---

## Architecture Overview

### Task Layout

```text
ESP32
├── Core 0
│   ├── Actuator Task
│   ├── State Machine Task
│   ├── Safety Task
│   ├── Timer Task
│   └── Telemetry & MQTT Task
│
└── Core 1
    └── Sensor Task
        ├── ADS1115 Current
        ├── ADS1115 Pressure
        └── MAX31856 Thermocouple
```

### Functional Blocks

```text
MQTT Command
    ↓
mqttCallback()
    ↓
eventQueue
    ↓
StateMachineTask
    ↓
ActuatorTask
    ↓
SSR Output

Sensors → Shared Snapshot → SafetyTask → Fault Manager → FSM / Actuator
```

---

## Wiring Diagram

### High-Level Wiring

```text
                         +----------------------+
                         |        ESP32         |
                         |                      |
                         |  GPIO21  ---- SDA ---+-------------------+
                         |  GPIO22  ---- SCL ---+-------------------+---- ADS1115 #1 (ACS)
                         |                                            \--- ADS1115 #2 (PRESS)
                         |
                         |  GPIO18  ---- SCK --------------------------+---- MAX31856 #0
                         |  GPIO19  ---- MISO -------------------------+---- MAX31856 #1
                         |  GPIO23  ---- MOSI -------------------------+---- MAX31856 #2
                         |                                              \--- MAX31856 #3
                         |
                         |  GPIO5   ---- CS0 ------------------------------- MAX31856 Slot 0
                         |  GPIO17  ---- CS1 ------------------------------- MAX31856 Slot 1
                         |  GPIO16  ---- CS2 ------------------------------- MAX31856 Slot 2
                         |  GPIO4   ---- CS3 ------------------------------- MAX31856 Slot 3
                         |
                         |  GPIO25  ---- SSR Slot 0
                         |  GPIO26  ---- SSR Slot 1
                         |  GPIO27  ---- SSR Slot 2
                         |  GPIO14  ---- SSR Slot 3
                         +----------------------+
```

### Pin Mapping

#### I2C Bus

| Function | ESP32 Pin |
|---|---:|
| SDA | GPIO21 |
| SCL | GPIO22 |

#### SPI Bus

| Function | ESP32 Pin |
|---|---:|
| SCK | GPIO18 |
| MISO | GPIO19 |
| MOSI | GPIO23 |

#### MAX31856 Chip Select

| Slot | CS Pin |
|---|---:|
| Slot 0 | GPIO5 |
| Slot 1 | GPIO17 |
| Slot 2 | GPIO16 |
| Slot 3 | GPIO4 |

#### SSR Output

| Slot | SSR Pin |
|---|---:|
| Slot 0 | GPIO25 |
| Slot 1 | GPIO26 |
| Slot 2 | GPIO27 |
| Slot 3 | GPIO14 |

#### ADS1115 Addresses

| Device | I2C Address |
|---|---|
| ADS ACS | `0x48` |
| ADS PRESS | `0x49` |

---

## Sensor Mapping

### Thermocouple

| Slot | MAX31856 |
|---|---|
| Slot 0 | TC0 |
| Slot 1 | TC1 |
| Slot 2 | TC2 |
| Slot 3 | TC3 |

### Current Sensor

Saat ini firmware membaca current per channel dari ADS ACS. Pastikan mapping channel fisik di hardware sesuai dengan desain panel/wiring.

### Pressure Sensor

Pressure dibaca dari ADS PRESS. Jika hanya satu sensor pressure fisik digunakan, pastikan pemetaan channel di firmware dan wiring panel konsisten.

---

## MQTT Interface

### Topics

| Purpose | Topic |
|---|---|
| Control | `/esp32/heater/control` |
| Telemetry | `/esp32/heater/telemetry` |
| Status | `/esp32/heater/status` |
| Event | `/esp32/heater/event` |

### Control Topic

ESP32 **subscribe** ke topic control:

```text
/esp32/heater/control
```

Semua command dikirim sebagai JSON ke topik ini.

### Telemetry Topic

ESP32 **publish** telemetry ke:

```text
/esp32/heater/telemetry
```

Telemetry dikirim periodik saat ada slot aktif.

### Status Topic

ESP32 **publish** heartbeat / status umum ke:

```text
/esp32/heater/status
```

### Event Topic

ESP32 **publish**:
- command acknowledgment
- fault event
- event penting lainnya

ke:

```text
/esp32/heater/event
```

---

## MQTT Commands

### 1. Start Slot

```json
{
  "cmd": "start",
  "slot": 3,
  "duration_s": 120
}
```

### 2. Stop Slot

```json
{
  "cmd": "stop",
  "slot": 3
}
```

### 3. Set Mode

```json
{
  "cmd": "set_mode",
  "mode": "production"
}
```

atau

```json
{
  "cmd": "set_mode",
  "mode": "commissioning"
}
```

### 4. Reset Fault

```json
{
  "cmd": "reset_fault"
}
```

### 5. Calibrate Zero

```json
{
  "cmd": "calibrate_zero",
  "slot": 1,
  "offset_mV": 12.5
}
```

### 6. Reset Calibration

```json
{
  "cmd": "reset_calibration",
  "slot": 1
}
```

---

## Command Acknowledgment Format

Setiap command akan dibalas ke topic event dengan format:

```json
{
  "evt": "cmd_ack",
  "cmd": "start",
  "accepted": true,
  "reason": "queued",
  "slot": 3,
  "esp_id": "heater01"
}
```

Jika command ditolak:

```json
{
  "evt": "cmd_ack",
  "cmd": "start",
  "accepted": false,
  "reason": "invalid_param",
  "slot": 3,
  "esp_id": "heater01"
}
```

---

## Telemetry Format

Saat ada slot yang running, firmware publish telemetry periodik. Struktur tipikal:

```json
{
  "esp_id": "heater01",
  "machine_state": 2,
  "tempC": [null, null, null, 29.16],
  "temp_valid": [false, false, false, true],
  "currentA": [0.12, 0.09, 0.11, 0.13],
  "current_valid": [true, true, true, true],
  "pressureBar": [2.10, 2.11, 2.10, 2.09],
  "pressure_valid": [true, true, true, true],
  "ssr_desired": [false, false, false, true],
  "ssr_actual": [false, false, false, true],
  "remaining_ms": [0, 0, 0, 85000]
}
```

### Status / Heartbeat Example

```json
{
  "esp_id": "heater01",
  "machine_state": 1,
  "mode": "production",
  "anyRunning": false,
  "uptime_ms": 123456,
  "freeHeap": 180000
}
```

### Fault Event Example

```json
{
  "evt": "fault",
  "esp_id": "heater01",
  "code": 2,
  "scope": 1,
  "slot": 3,
  "source": "SafetyTask",
  "timestamp_ms": 12345678
}
```

---

## Runtime Sensor Validation

### Thermocouple Validation

Firmware tidak hanya mengandalkan `begin()` atau satu pembacaan temperatur. Validasi thermocouple mempertimbangkan:

- fault register MAX31856
- thermocouple temperature finite / not NaN
- cold junction temperature finite / not NaN
- temperature range sanity check
- zero pattern persistence
- stuck reading detection

Tujuan pendekatan ini adalah untuk menghindari false positive saat:
- chip tidak benar-benar hadir
- thermocouple open
- wiring salah
- bus membaca nilai default / floating

### ADS1115 Validation

ADS current dan pressure dianggap valid hanya jika:
- device I2C terdeteksi saat boot
- init device berhasil

Jika tidak:
- data ditandai invalid
- sistem tetap fail-safe

---

## Fault Handling

Sistem fault memiliki properti berikut:

- latched
- thread-safe
- disimpan ke NVS
- di-restore saat boot

### Fault Pipeline

```text
Sensor / Safety Condition
    ↓
fault_raise(...)
    ↓
Persist to NVS
    ↓
Publish Event
    ↓
FSM / Actuator Response
```

### Fault Reset

Fault tidak hilang sendiri. Reset harus dilakukan eksplisit dengan:

```json
{
  "cmd": "reset_fault"
}
```

---

## Logging

Sistem menggunakan logging terpusat dengan macro:

| Level | Macro |
|---|---|
| Error | `LOGE(...)` |
| Warning | `LOGW(...)` |
| Info | `LOGI(...)` |
| Debug | `LOGD(...)` |

### Example Log

```text
[I][BOOT] startup begin
[I][BOOT] I2C device found addr=0x48
[I][BOOT] ADS ACS ready addr=0x48
[D][SENSOR] TC[3] tc=29.16 cj=29.55 fault=0x00(NONE) valid=1
[I][MQTT] connected broker=192.168.100.12 topic=/esp32/heater/control
```

---

## Build & Flash

Menggunakan PlatformIO.

### Build

```bash
pio run
```

### Upload

```bash
pio run --target upload
```

### Serial Monitor

```bash
pio device monitor
```

---

## Software Dependencies

- ESP32 Arduino Framework
- Adafruit ADS1X15
- Adafruit MAX31856
- Adafruit BusIO
- PubSubClient
- ArduinoJson
- Preferences / NVS

---

## Repository Structure (Suggested)

```text
src/
├── main.cpp
├── app_config.cpp
├── app_config.h
├── app_shared.h
├── app_state.cpp
├── actuatorTask.cpp
├── actuatorTask.h
├── stateMachineTask.cpp
├── stateMachineTask.h
├── safetyTask.cpp
├── safetyTask.h
├── fault.cpp
├── fault.h
├── fault_persist.cpp
├── fault_persist.h
├── fault_restore.cpp
├── fault_restore.h
├── logger.cpp
└── logger.h
```

---

## Recommended Validation Tests

### Sensor Presence Test
- lepas semua thermocouple
- pastikan `tempValid=0/4`
- pasang satu thermocouple
- pastikan hanya channel itu yang valid

### Fault Path Test
- start slot tanpa sensor valid
- pastikan `FAULT_SENSOR_LOST` muncul
- pastikan output heater aman

### Telemetry Test
- start slot valid
- pastikan telemetry keluar ke `/esp32/heater/telemetry`
- cek `remaining_ms`, `ssr_desired`, `ssr_actual`

### Recovery Test
- paksa fault
- reboot device
- pastikan fault restore dari NVS bekerja
- reset fault
- pastikan status kembali normal

---

## Security / Production Notes

Untuk deployment publik / production, sangat disarankan menambahkan:

- MQTT authentication yang lebih kuat
- request ID pada command
- versioning payload command
- TLS jika broker mendukung
- provisioning credential yang lebih aman
- watchdog supervision
- per-channel diagnostic counters

---

## Known Limitations

- `MAX31856 begin()` tidak dapat dijadikan presence detector tunggal
- validasi thermocouple masih bergantung pada heuristik runtime
- payload command saat ini belum memiliki versioning / auth token
- mode command masih bisa diperketat validasinya

---

## License

Tentukan sesuai kebutuhan project, misalnya:

```text
MIT License
```

---

## Maintainer

Smart Curing Controller  
ESP32-based heating and curing platform
