#ifndef __PLATFORM_H__
#define __PLATFORM_H__

#include <stdint.h>
#include <stdbool.h>

/* g_sys_tick_ms 定义在 main.c 中 */
extern volatile uint32_t g_sys_tick_ms;

/**
 * @brief   获取系统运行毫秒数（基于 SysTick 自由运行计数器）
 *          参考: MCU.c SYS_GetSysTickCnt()
 */
uint32_t platform_time_ms(void);

/**
 * @brief   获取芯片唯一 ID（6 字节）
 *          CH582F: InfoFlash 首地址 (0x3FFFF800) 前 6 字节
 *          CH592F: 芯片唯一 ID 寄存器 (0x3FFFF8A8)
 */
void platform_get_unique_id(uint8_t buf[6]);

/**
 * @brief   软件复位芯片
 */
void platform_reboot(void);

/**
 * 看门狗 (WWDG) — 溢出窗口 ~437ms (60MHz/131072≈457.8Hz, 计数器 200)
 * 使能溢出复位; 主循环每轮喂狗, 防 BLE ATT 死锁
 */
void platform_wdog_init(void);
void platform_wdog_feed(void);

/**
 * @brief   LED 控制（PA8，低电平点亮）
 */
void platform_led_on(void);
void platform_led_off(void);
void platform_led_toggle(void);

/**
 * @brief   阻塞延时（毫秒）
 *          参考: CH5x8_sys.c mDelaymS()
 */
void platform_delay_ms(uint32_t ms);

#endif /* __PLATFORM_H__ */
