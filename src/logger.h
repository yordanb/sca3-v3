#pragma once
#include <Arduino.h>

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
} log_level_t;

#ifndef LOG_ENABLE
#define LOG_ENABLE 1
#endif

#ifndef LOG_LEVEL_CURRENT
#define LOG_LEVEL_CURRENT LOG_LEVEL_DEBUG
#endif

void log_init(unsigned long baud = 115200);
void log_print(log_level_t level, const char* tag, const char* fmt, ...);

#if LOG_ENABLE
#define LOGE(tag, fmt, ...) log_print(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) log_print(LOG_LEVEL_WARN,  tag, fmt, ##__VA_ARGS__)
#define LOGI(tag, fmt, ...) log_print(LOG_LEVEL_INFO,  tag, fmt, ##__VA_ARGS__)
#define LOGD(tag, fmt, ...) log_print(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#else
#define LOGE(tag, fmt, ...)
#define LOGW(tag, fmt, ...)
#define LOGI(tag, fmt, ...)
#define LOGD(tag, fmt, ...)
#endif
