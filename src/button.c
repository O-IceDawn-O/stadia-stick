/********************************************
 * Stick — CH592F Stadia USB Dongle
 * BOOT 按钮 (PB22) — 消抖 + 边缘事件
 *
 * 参考:
 *   sdk/EVT/EXAM/BLE/HAL/include/KEY.h — 按键宏定义风格
 ********************************************/

#include "button.h"
#ifdef CFG_CH582
#include "CH58x_common.h"
#include "CH58x_gpio.h"
#else
#include "CH59x_common.h"
#include "CH59x_gpio.h"
#endif

#define BOOT_PIN    GPIO_Pin_22

/* 消抖时间 (ms, 定义于 button.h) */

/* 外部 SysTick 毫秒计数器 (定义于 main.c) */
extern volatile uint32_t g_sys_tick_ms;

static bool s_last_raw = true;    /* 上一次原始电平 (true=高=松开) */
static bool s_debounced = true;   /* 消抖后电平 */
static bool s_event = false;      /* 按下事件 (边缘触发，读取后清零) */
static uint32_t s_last_change = 0;

void Button_Init(void)
{
    s_last_raw = true;
    s_debounced = true;
}

bool Button_IsPressed(void)
{
    return !s_debounced; /* 按下为低电平 */
}

bool Button_GetEvent(void)
{
    bool ev = s_event;
    s_event = false;
    return ev;
}

void Button_Task(void)
{
    /* 读取当前电平 (PB22 按下为低) */
    bool raw = (GPIOB_ReadPortPin(BOOT_PIN) != 0);

    /* 电平变化时记录时间 */
    if (raw != s_last_raw) {
        s_last_raw = raw;
        s_last_change = g_sys_tick_ms;
    }

    /* 稳定超过消抖时间 → 更新消抖后电平 */
    if ((g_sys_tick_ms - s_last_change) >= DEBOUNCE_MS) {
        if (s_debounced != s_last_raw) {
            s_debounced = s_last_raw;
            if (!s_debounced) {
                /* 检测到按下 (高→低边缘) */
                s_event = true;
            }
        }
    }
}
