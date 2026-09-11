#ifndef __BUTTON_H__
#define __BUTTON_H__

#include <stdint.h>
#include <stdbool.h>

/* 软件消抖时间 (毫秒) */
#define DEBOUNCE_MS  20

/*
 * BOOT 按钮 (PB22) 模块
 * 参考: sdk/EVT/EXAM/BLE/HAL/include/KEY.h — WCH 按键例程风格
 *
 * PB22 在启动时用于进入引导加载程序（BOOT），
 * 启动后可用作普通 GPIO（输入上拉，按下为低电平）。
 *
 * 用法:
 *   Button_Init();          // 初始化 PB22 GPIO
 *   Button_Task();          // 在主循环中轮询
 *
 *   if (Button_IsPressed()) { ... }        // 实时读取电平
 *   if (Button_GetEvent()) { ... }         // 读取边缘事件 (按下时置位，读取后清零)
 */

void  Button_Init(void);
void  Button_Task(void);
bool  Button_IsPressed(void);
bool  Button_GetEvent(void);

#endif /* __BUTTON_H__ */
