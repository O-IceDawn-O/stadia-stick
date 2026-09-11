/********************************************
 * Stick — CH592F Stadia USB Dongle
 * 分级日志模块 (输出到 CDC 虚拟串口)
 *
 * 依赖: printf → _write() → CDC_PutChar → 环形缓冲 → CDC_Flush
 *       平台时间戳 g_sys_tick_ms (定义于 main.c)
 ********************************************/

#include "log.h"
#include "platform.h"   /* g_sys_tick_ms */
#include <stdarg.h>
#include <string.h>

/* 日志级别标签 */
static const char *s_level_tag[] = {
    "",        /* NONE  */
    "[ERR]",   /* ERROR */
    "[WRN]",   /* WARN  */
    "[INF]",   /* INFO  */
    "[DBG]"    /* DEBUG */
};

/* 临时格式化缓冲区 (LOG_BUF_SIZE 定义于 log.h) */
static char s_log_buf[LOG_BUF_SIZE];

void log_init(void)
{
    /* 预留: 未来可配置日志级别、注册回调等 */
}

void log_write(uint8_t level, const char *mod, const char *fmt, ...)
{
    if (level > LOG_LEVEL)
        return;

    uint16_t pos = 0;

    /* --- 时间戳 [T+秒.毫秒]  (秒数 uint32_t 可跑 136 年不溢出) --- */
    uint32_t ms = g_sys_tick_ms;
    pos += snprintf(s_log_buf + pos, LOG_BUF_SIZE - pos,
                    "[T+%u.%03u] ", (unsigned)(ms / 1000), (unsigned)(ms % 1000));
    if (pos >= LOG_BUF_SIZE) goto out;

    /* --- 级别标签 [ERR]/[WRN]/[INF]/[DBG] --- */
    if (level <= LOG_LEVEL_DEBUG) {
        pos += snprintf(s_log_buf + pos, LOG_BUF_SIZE - pos,
                        "%s ", s_level_tag[level]);
        if (pos >= LOG_BUF_SIZE) goto out;
    }

    /* --- 模块标签 [MOD] --- */
    if (mod && mod[0]) {
        pos += snprintf(s_log_buf + pos, LOG_BUF_SIZE - pos,
                        "[%s] ", mod);
        if (pos >= LOG_BUF_SIZE) goto out;
    }

    /* --- 用户消息 --- */
    {
        va_list args;
        va_start(args, fmt);
        pos += vsnprintf(s_log_buf + pos, LOG_BUF_SIZE - pos, fmt, args);
        va_end(args);
        if (pos >= LOG_BUF_SIZE) goto out;
    }

    /* --- 换行 --- */
    if (pos + 2 < LOG_BUF_SIZE) {
        s_log_buf[pos++] = '\n';
    }

out:
    if (pos >= LOG_BUF_SIZE) pos = LOG_BUF_SIZE - 1;  /* vsnprintf 截断时返回理想长度, pos 可能越界 */
    s_log_buf[pos] = '\0';
    printf("%s", s_log_buf);
}
