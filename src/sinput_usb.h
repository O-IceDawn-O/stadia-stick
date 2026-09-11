#ifndef __SINPUT_USB_H__
#define __SINPUT_USB_H__

#include <stdint.h>
#include <stdbool.h>

#include <stdio.h>

/* 端点大小 */
#define EP0_SIZE     64
#define EP2_SIZE     64   /* CDC Data IN/OUT (批量) */
#define EP3_SIZE     64   /* HID IN/OUT (中断) */

/* CDC 发送环形缓冲大小 */
#define CDC_TX_BUF_SIZE  1024

/*
 * USB 复合设备: CDC (虚拟串口, 日志) + HID (SInput 游戏手柄)
 *
 * 端点分配:
 *   EP1 IN — CDC 通知 (中断)
 *   EP2 IN/OUT — CDC 数据 (批量)
 *   EP3 IN/OUT — HID SInput 报告 (中断)
 *
 * 参考:
 *   sdk/EVT/EXAM/USB/Device/HID_CompliantDev/src/Main.c  — USB 框架
 *   sdk/EVT/EXAM/USB/Device/COM/src/Main.c               — CDC 框架
 *   sdk/EVT/EXAM/SRC/StdPeriphDriver/CH59x_usbdev.c       — USB 驱动
 */

/**
 * @brief   初始化 USB 设备控制器 (CDC + HID 复合设备)
 *          配置端点、DMA 缓冲区、中断。初始化后 USB 开始枚举。
 */
void SInputUSB_Init(void);

/**
 * @brief   通过 EP3 IN 发送 SInput HID 报告 (64 字节)
 * @param   data    报告数据
 * @param   len     数据长度（最大 64）
 * @return  true    发送成功 (EP3 已置 ACK 等待主机轮询)
 *          false   EP3 忙 (前次数据主机未读取), 丢弃
 *                  输入报告调用方应保留 ready 标志在下一轮重试
 */
bool SInputUSB_SendReport(uint8_t *data, uint16_t len);

/**
 * @brief   注册 EP3 OUT 回调（收到主机输出报告时调用，如震动命令）
 * @param   cb      回调函数指针
 */
void SInputUSB_SetOutputCallback(void (*cb)(uint8_t *data, uint16_t len));

/**
 * @brief   刷新 CDC 发送缓冲（在主循环中调用）
 */
void CDC_Flush(void);

/**
 * @brief   USB 是否已枚举就绪
 */
bool SInputUSB_IsReady(void);

/**
 * @brief   处理缓存的 HID 输出报告（在主循环中调用）
 *          USB ISR 只存数据设标志，延迟到主循环处理，
 *          避免在中断中调用 BLE API 或 LOG。
 */
void SInputUSB_ProcessOutput(void);

#endif /* __SINPUT_USB_H__ */
