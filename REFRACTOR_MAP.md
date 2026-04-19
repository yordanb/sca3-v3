# Delta refactor map

## Existing files kept and rewritten
- platformio.ini
- src/main.cpp
- src/app_shared.h
- src/fault.h
- src/fault.cpp
- src/fault_persist.cpp
- src/fault_restore.h
- src/fault_restore.cpp
- src/safetyTask.h
- src/safetyTask.cpp
- src/stateMachineTask.cpp

## New files added
- src/app_config.h
- src/app_config.cpp
- src/app_state.cpp
- src/fault_persist.h
- src/actuatorTask.h
- src/actuatorTask.cpp
- src/stateMachineTask.h

## Main architecture changes
- single-writer actuator task for SSR
- thread-safe fault manager with persistence wired into raise/reset
- snapshot-based shared state and sensor contract
- machine state separated from job state
- MQTT callback reduced to command ingress and ack publishing
- safer boot order and boot fault restore
