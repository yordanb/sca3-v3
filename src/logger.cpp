#include "logger.h"
#include <stdarg.h>

static const char* level_to_string(log_level_t level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "E";
        case LOG_LEVEL_WARN:  return "W";
        case LOG_LEVEL_INFO:  return "I";
        case LOG_LEVEL_DEBUG: return "D";
        default: return "?";
    }
}

void log_init(unsigned long baud) {
#if LOG_ENABLE
    Serial.begin(baud);
    delay(50);
#endif
}

void log_print(log_level_t level, const char* tag, const char* fmt, ...) {
#if LOG_ENABLE
    if (level > LOG_LEVEL_CURRENT) return;

    char msg[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    const unsigned long now = millis();
    Serial.printf("[%lu][%s][%s] %s\n",
                  now,
                  level_to_string(level),
                  tag ? tag : "APP",
                  msg);
#endif
}
