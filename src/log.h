#ifndef __LOG_H__
#define __LOG_H__

#include <stdint.h>
#include <stdio.h>

/* 日志格式化缓冲区大小 */
#define LOG_BUF_SIZE  256

/*
 * 分级日志模块
 * 输出到 CDC 虚拟串口（COM 口），通过 printf → _write → CDC 管线
 *
 * 用法:
 *   LOG_ERROR("BLE",  "Connection failed");
 *   LOG_INFO("MAIN", "USB ready, tick=%lu", now);
 *   LOG_DEBUG("HID", "Report: %02x %02x ...", data[0], data[1]);
 *
 * 日志格式: [T+ms] [LEVEL] [MODULE] 消息
 */

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

#ifndef LOG_LEVEL
#define LOG_LEVEL  LOG_LEVEL_INFO
#endif

#define LOG_ERROR(mod, fmt, ...) \
    log_write(LOG_LEVEL_ERROR, mod, fmt, ##__VA_ARGS__)

#define LOG_WARN(mod, fmt, ...) \
    log_write(LOG_LEVEL_WARN, mod, fmt, ##__VA_ARGS__)

#define LOG_INFO(mod, fmt, ...) \
    log_write(LOG_LEVEL_INFO, mod, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(mod, fmt, ...) \
    log_write(LOG_LEVEL_DEBUG, mod, fmt, ##__VA_ARGS__)

/**
 * @brief   初始化日志系统（目前仅设置全局时间引用）
 */
void log_init(void);

/**
 * @brief   写入一条日志（通过 level 过滤，低于当前级别不输出）
 * @param   level   LOG_LEVEL_*
 * @param   mod     模块名（如 "BLE", "USB", "HID", "MAIN"）
 * @param   fmt     printf 风格格式串
 */
void log_write(uint8_t level, const char *mod, const char *fmt, ...);

#endif /* __LOG_H__ */
