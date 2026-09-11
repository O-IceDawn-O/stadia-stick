#ifndef __SINPUT_BUILDER_H__
#define __SINPUT_BUILDER_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * SInput 协议报文构建
 *
 * SInput 协议属公开标准, 由 Hand Held Legend 发布:
 *   https://github.com/HandHeldLegend/SInput-HID
 *   https://docs.handheldlegend.com/s/sinput
 * SDL 原生支持: SDL_hidapi_sinput.c
 *
 * 本项目使用 SInput 子集: 无 IMU、触摸板、RGB LED。
 */

/* SInput 按键掩码 */
#define SINPUT_MASK_EAST        (1U << 0)   /* B */
#define SINPUT_MASK_SOUTH       (1U << 1)   /* A */
#define SINPUT_MASK_NORTH       (1U << 2)   /* Y */
#define SINPUT_MASK_WEST        (1U << 3)   /* X */
#define SINPUT_MASK_DU          (1U << 4)   /* D-pad 上 */
#define SINPUT_MASK_DD          (1U << 5)   /* D-pad 下 */
#define SINPUT_MASK_DL          (1U << 6)   /* D-pad 左 */
#define SINPUT_MASK_DR          (1U << 7)   /* D-pad 右 */
#define SINPUT_MASK_L3          (1U << 8)
#define SINPUT_MASK_R3          (1U << 9)
#define SINPUT_MASK_L1          (1U << 10)
#define SINPUT_MASK_R1          (1U << 11)
#define SINPUT_MASK_L2          (1U << 12)
#define SINPUT_MASK_R2          (1U << 13)
#define SINPUT_MASK_START       (1U << 16)  /* Menu */
#define SINPUT_MASK_BACK        (1U << 17)  /* Options */
#define SINPUT_MASK_GUIDE       (1U << 18)  /* Stadia 按钮 */
#define SINPUT_MASK_CAPTURE     (1U << 19)  /* 截图 */

/* SInput 报告 ID */
#define SINPUT_REPORT_ID_INPUT      0x01    /* 64 字节输入报告 */
#define SINPUT_REPORT_ID_FEATURES   0x02    /* 63 字节功能响应 */
#define SINPUT_REPORT_ID_OUTPUT     0x03    /* 48 字节输出报告 */

/* SInput 输出命令 */
#define SINPUT_CMD_HAPTIC       0x01        /* 震动命令 */
#define SINPUT_CMD_FEATURES     0x02        /* 功能查询 */

/* SInput 输入报告结构体 (64 字节) */
typedef struct __attribute__((packed)) {
    uint8_t  report_id;         /* 0x01                         */
    uint8_t  plug_status;       /* 供电状态                     */
    uint8_t  charge_level;      /* 电量 %                       */
    uint8_t  buttons[4];        /* 32 个按键位图 (LE)           */
    int16_t  lx, ly;            /* 左摇杆: -32768~32767         */
    int16_t  rx, ry;            /* 右摇杆                       */
    int16_t  lt, rt;            /* 扳机: 0~32767                */
    uint32_t imu_timestamp;     /* IMU 时间戳                   */
    int16_t  accel_x, accel_y, accel_z;  /* IMU 加速度 (未用)  */
    int16_t  gyro_x, gyro_y, gyro_z;     /* IMU 陀螺仪 (未用)  */
    uint8_t  touchpad1[6];      /* 触摸板 1 (未用)              */
    uint8_t  touchpad2[6];      /* 触摸板 2 (未用)              */
    uint8_t  reserved[17];      /* 填充 0                       */
} sinput_report_t;

/* SInput 输出报告结构体 (48 字节) */
typedef struct __attribute__((packed)) {
    uint8_t  report_id;         /* 0x03            */
    uint8_t  command;           /* SINPUT_CMD_*    */
    uint8_t  data[46];          /* 命令参数        */
} sinput_output_t;

/* SInput 震动命令数据 */
typedef struct __attribute__((packed)) {
    uint8_t  type;              /* 2 = ERM         */
    uint8_t  left_amplitude;    /* 0-255           */
    uint8_t  left_brake;        /* 保留            */
    uint8_t  right_amplitude;   /* 0-255           */
    uint8_t  right_brake;       /* 保留            */
} sinput_haptic_t;

/**
 * @brief   构建 64 字节 SInput 输入报告
 *
 * @param   buttons     JP_BUTTON_* 位图
 * @param   analog      [LX, LY, RX, RY, L2, R2] (0-255, 轴中心 128)
 * @param   battery     电池电量 0-100, 0xFF=未知
 * @param   out         [out] 64 字节报告缓冲区
 */
void sinput_build_report(uint32_t buttons, const uint8_t analog[6],
                          uint8_t battery, uint8_t out[64]);

/**
 * @brief   构建 63 字节 SInput 功能响应
 *
 * @param   serial      6 字节芯片唯一 ID
 * @param   out         [out] 63 字节响应缓冲区
 */
void sinput_build_feature_response(const uint8_t serial[6], uint8_t out[63]);

/**
 * @brief   从 SInput 输出报告中提取震动振幅
 *
 * @param   data        USB OUT 报告数据 (48 字节)
 * @param   len         数据长度
 * @param   left        [out] 左马达振幅 (0-255)
 * @param   right       [out] 右马达振幅 (0-255)
 * @return  true 如果是有效的震动命令
 */
bool sinput_parse_haptic(const uint8_t *data, uint16_t len,
                          uint8_t *left, uint8_t *right);

#endif /* __SINPUT_BUILDER_H__ */
