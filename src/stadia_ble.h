#ifndef __STADIA_BLE_H__
#define __STADIA_BLE_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * Stadia 手柄 BLE Central 连接管理
 *
 * 参考:
 *   sdk/EVT/EXAM/BLE/Central/APP/central.c       — BLE Central 完整实现
 *   sdk/EVT/EXAM/BLE/Central/APP/central_main.c  — 初始化序列
 *   sdk/EVT/EXAM/BLE/HAL/MCU.c                   — CH59x_BLEInit(), SysTick 配置
 */

/* 全局状态（由 main.c 定义） */
extern bool     g_ble_ready;            /* BLE 协议栈初始化完成才为 true */
extern bool     g_stadia_connected;     /* 是否已连接 Stadia 手柄      */
extern volatile bool g_stadia_report_ready;  /* 有新的 HID 通知待处理  */
extern uint8_t  g_stadia_raw_data[16];  /* 原始通知数据                */
extern uint16_t g_stadia_raw_len;       /* 数据长度                    */
extern uint16_t g_conn_handle;          /* BLE 连接句柄                */
extern uint16_t g_hid_input_handle;     /* 输入报告特征句柄            */
extern uint16_t g_hid_output_handle;    /* 输出报告特征值句柄          */
extern uint8_t  g_hid_output_props;     /* 输出报告特征属性字节        */
extern uint16_t g_stadia_bat_handle;    /* 电池特征值句柄              */
extern uint8_t  g_stadia_battery;       /* 0-100, 0xFF=未知           */
extern bool     g_stadia_bat_read_pending;  /* 电池读请求已发出待回执    */

/**
 * @brief   初始化 BLE Central 并开始扫描 Stadia
 *          必须在 CH59x_BLEInit()/CH58X_BLEInit() 和 HAL_Init() 之后调用
 *          (芯片由 CFG_CH582/CFG_CH592 条件编译选择)
 */
void StadiaBLE_Init(void);

/**
 * @brief   开始 BLE 扫描（按设备名匹配 "Stadia"）
 *          仅在 IDLE 状态有效
 */
void StadiaBLE_StartScan(void);

/**
 * @brief   停止扫描
 *          仅在 SCANNING 状态有效
 */
void StadiaBLE_StopScan(void);

/**
 * @brief   查询是否正在扫描
 * @return  true 正在扫描中
 */
bool StadiaBLE_IsScanning(void);

/**
 * @brief   断开当前连接
 */
void StadiaBLE_Disconnect(void);

/**
 * @brief   通过 GATT 输出报告特征发送数据（震动命令）
 */
bool StadiaBLE_SendOutput(uint8_t *data, uint16_t len);

/**
 * @brief   强制完成 discovery（超时后调用, 避免 GATT 操作卡死）
 */
void StadiaBLE_ForceDiscoveryDone(void);

/**
 * @brief   发起电池电量读取 (GATT Read). 结果通过 ATT_READ_RSP 更新 g_stadia_battery.
 *          需要在主循环中周期性调用, 间隔建议 >= 30 秒.
 *          当 g_stadia_bat_read_pending 为 true 时跳过 (前次读取未完成).
 */
void StadiaBLE_ReadBattery(void);

/* ===================================================================
 * 回调函数 — 由上层 (main.c) 实现
 * =================================================================== */

/**
 * @brief   BLE 连接成功回调
 * @param   conn_handle  连接句柄
 */
void StadiaBLE_OnConnected(uint16_t conn_handle);

/**
 * @brief   BLE 断开连接回调
 * @param   conn_handle  连接句柄
 */
void StadiaBLE_OnDisconnected(uint16_t conn_handle);

#endif /* __STADIA_BLE_H__ */
