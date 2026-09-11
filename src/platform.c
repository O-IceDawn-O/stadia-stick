/********************************************
 * Stick — CH592F / CH582F Stadia USB Dongle
 * 平台抽象层
 *
 * 参考:
 *   CH5x8_sys.c                          — mDelaymS, SYS_GetSysTickCnt
 *   CH5x8 数据手册                        — 芯片唯一 ID 地址
 *   sdk/.../BLE/HAL/include/LED.h        — LED 宏定义风格
 ********************************************/

#include "platform.h"

#ifdef CFG_CH582
#include "CH58x_common.h"
#include "CH58x_gpio.h"
#include "CH58x_sys.h"
#else
#include "CH59x_common.h"
#include "CH59x_gpio.h"
#include "CH59x_sys.h"
#endif

/*********************************************************************
 * LED 控制 (PA8, 低电平点亮)
 *********************************************************************/
#define LED_PIN     GPIO_Pin_8

void platform_led_on(void)
{
    GPIOA_ResetBits(LED_PIN);
}

void platform_led_off(void)
{
    GPIOA_SetBits(LED_PIN);
}

void platform_led_toggle(void)
{
    GPIOA_InverseBits(LED_PIN);
}

/*********************************************************************
 * 系统时间
 * 参考: MCU.c SYS_GetSysTickCnt()
 * 注意: 需要在主循环中调用 update_tick() 刷新 g_sys_tick_ms
 *********************************************************************/
uint32_t platform_time_ms(void)
{
    return g_sys_tick_ms;
}

/*********************************************************************
 * 芯片唯一 ID
 * CH582F: InfoFlash 首地址 (0x3FFFF800) 前 6 字节
 * CH592F: 芯片唯一 ID 寄存器 (0x3FFFF8A8)
 *********************************************************************/
void platform_get_unique_id(uint8_t buf[6])
{
#ifdef CFG_CH582
    const uint32_t uid_addr = 0x3FFFF800;
#else
    const uint32_t uid_addr = 0x3FFFF8A8;
#endif
    for (int i = 0; i < 6; i++) {
        buf[i] = *((volatile uint8_t *)(uid_addr + i));
    }
}

/*********************************************************************
 * 软件复位
 *********************************************************************/
void platform_reboot(void)
{
    sys_safe_access_enable();
    R8_RST_WDOG_CTRL |= RB_SOFTWARE_RESET;
    sys_safe_access_disable();
    while (1);
}

/*********************************************************************
 * 看门狗 (WWDG)
 * 溢出窗口 ~437ms (60MHz/131072≈457.8Hz, 计数器 200)
 * 使能溢出复位; 主循环每轮喂狗, 防 BLE ATT 死锁
 *********************************************************************/
void platform_wdog_init(void)
{
    sys_safe_access_enable();
    WWDG_SetCounter(200);
    R8_RST_WDOG_CTRL |= RB_WDOG_RST_EN;
    sys_safe_access_disable();
}

void platform_wdog_feed(void)
{
    WWDG_SetCounter(200);
}

/*********************************************************************
 * 阻塞延时
 *********************************************************************/
void platform_delay_ms(uint32_t ms)
{
    mDelaymS(ms);
}
