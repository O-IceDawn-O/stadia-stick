/********************************************
 * Stick — CH592F Stadia USB Dongle
 * SInput 协议报文构建
 *
 * SInput 协议属公开标准, 由 Hand Held Legend 发布:
 *   https://github.com/HandHeldLegend/SInput-HID
 *   https://docs.handheldlegend.com/s/sinput
 *
 * 无硬件依赖，纯 C 算法，可在任何平台编译测试。
 ********************************************/

#include "sinput_builder.h"
#include <string.h>

/*********************************************************************
 * 按键映射表: JP_BUTTON_* → SINPUT_MASK_*
 * 参考: sinput_mode.c convert_buttons()
 *********************************************************************/
typedef struct {
    uint32_t jp_mask;
    uint32_t sinput_mask;
} btn_map_t;

static const btn_map_t s_btn_map[] = {
    /* 面键 (参考: sinput_mode.c convert_buttons) */
    { (1 << 0),  SINPUT_MASK_SOUTH },   /* JP_BUTTON_B1 (A)     → SINPUT_SOUTH   */
    { (1 << 1),  SINPUT_MASK_EAST  },   /* JP_BUTTON_B2 (B)     → SINPUT_EAST    */
    { (1 << 2),  SINPUT_MASK_WEST  },   /* JP_BUTTON_B3 (X)     → SINPUT_WEST    */
    { (1 << 3),  SINPUT_MASK_NORTH },   /* JP_BUTTON_B4 (Y)     → SINPUT_NORTH   */

    /* 肩键 */
    { (1 << 4),  SINPUT_MASK_L1    },   /* JP_BUTTON_L1 (LB)    → SINPUT_L1      */
    { (1 << 5),  SINPUT_MASK_R1    },   /* JP_BUTTON_R1 (RB)    → SINPUT_R1      */

    /* 扳机 (数字) */
    { (1 << 6),  SINPUT_MASK_L2    },   /* JP_BUTTON_L2 (LT)    → SINPUT_L2      */
    { (1 << 7),  SINPUT_MASK_R2    },   /* JP_BUTTON_R2 (RT)    → SINPUT_R2      */

    /* 系统键 */
    { (1 << 8),  SINPUT_MASK_BACK  },   /* JP_BUTTON_S1 (Back)  → SINPUT_BACK    */
    { (1 << 9),  SINPUT_MASK_START },   /* JP_BUTTON_S2 (Start) → SINPUT_START   */

    /* 摇杆按键 */
    { (1 << 10), SINPUT_MASK_L3    },   /* JP_BUTTON_L3 (LS)    → SINPUT_L3      */
    { (1 << 11), SINPUT_MASK_R3    },   /* JP_BUTTON_R3 (RS)    → SINPUT_R3      */

    /* 方向键 (D-pad) */
    { (1 << 12), SINPUT_MASK_DU    },   /* JP_BUTTON_DU         → SINPUT_DU      */
    { (1 << 13), SINPUT_MASK_DD    },   /* JP_BUTTON_DD         → SINPUT_DD      */
    { (1 << 14), SINPUT_MASK_DL    },   /* JP_BUTTON_DL         → SINPUT_DL      */
    { (1 << 15), SINPUT_MASK_DR    },   /* JP_BUTTON_DR         → SINPUT_DR      */

    /* 辅助键 */
    { (1 << 16), SINPUT_MASK_GUIDE  },  /* JP_BUTTON_A1 (Guide) → SINPUT_GUIDE   */
    { (1 << 17), SINPUT_MASK_CAPTURE},  /* JP_BUTTON_A2 (Cap)   → SINPUT_CAPTURE */
};

/*********************************************************************
 * 模拟值转换
 * 参考: sinput_mode.c convert_axis_to_s16(), convert_trigger_to_s16()
 *********************************************************************/

/**
 * @brief   摇杆: 8 位 (0-255, 中心 128) → 16 位有符号 (-32768~32767)
 */
static inline int16_t convert_axis_to_s16(uint8_t value)
{
    return ((int16_t)value - 128) * 256;
}

/**
 * @brief   扳机: 8 位 (0-255) → 16 位有符号 (0~32767)
 */
static inline int16_t convert_trigger_to_s16(uint8_t value)
{
    /* 参考: sinput_mode.c — (value * 32767) / 255, 范围 0~32767 */
    return (int16_t)(((uint16_t)value * 32767) / 255);
}

/*********************************************************************
 * 按键转换: JP_BUTTON_* 位图 → SINPUT_MASK_* 位图
 *********************************************************************/
static uint32_t convert_buttons(uint32_t jp_buttons)
{
    uint32_t sinput_buttons = 0;
    for (int i = 0; i < (int)(sizeof(s_btn_map) / sizeof(s_btn_map[0])); i++) {
        if (jp_buttons & s_btn_map[i].jp_mask)
            sinput_buttons |= s_btn_map[i].sinput_mask;
    }
    return sinput_buttons;
}

/*********************************************************************
 * 构建 64 字节 SInput 输入报告
 *********************************************************************/
void sinput_build_report(uint32_t buttons, const uint8_t analog[6],
                          uint8_t battery, uint8_t out[64])
{
    sinput_report_t *rpt = (sinput_report_t *)out;

    memset(rpt, 0, sizeof(sinput_report_t));

    rpt->report_id    = SINPUT_REPORT_ID_INPUT;
    rpt->plug_status  = 4;     /* 4 = 电池供电 */
    rpt->charge_level = (battery <= 100) ? battery : 100;

    /* 按键转换 → 4 字节 LE */
    uint32_t sinput_btn = convert_buttons(buttons);
    rpt->buttons[0] = sinput_btn & 0xFF;
    rpt->buttons[1] = (sinput_btn >> 8) & 0xFF;
    rpt->buttons[2] = (sinput_btn >> 16) & 0xFF;
    rpt->buttons[3] = (sinput_btn >> 24) & 0xFF;

    /* 摇杆 */
    rpt->lx = convert_axis_to_s16(analog[0]);
    rpt->ly = convert_axis_to_s16(analog[1]);
    rpt->rx = convert_axis_to_s16(analog[2]);
    rpt->ry = convert_axis_to_s16(analog[3]);

    /* 扳机 */
    rpt->lt = convert_trigger_to_s16(analog[4]);
    rpt->rt = convert_trigger_to_s16(analog[5]);
}

/*********************************************************************
 * 构建 63 字节 SInput 功能响应
 * 参考: sinput_mode.c sinput_build_feature_response()
 *
 * 输出格式 (SDL SInput 驱动期望):
 *   out[0]     = 命令回显 (SINPUT_CMD_FEATURES = 0x02)
 *   out[1..24] = 24 字节能力结构体
 *   out[25..62]= 零填充
 *********************************************************************/
void sinput_build_feature_response(const uint8_t serial[6], uint8_t out[63])
{
    memset(out, 0, 63);

    /* Byte 0: 命令回显 */
    out[0] = SINPUT_CMD_FEATURES;

    /* 24 字节能力结构体起始于 out[1] */
    uint8_t *f = out + 1;

    /* 协议版本: v1.0 (uint16 LE) */
    f[0] = 0x00;
    f[1] = 0x01;

    /* 能力标志 1: 震动 + 玩家 LED + 摇杆 + 扳机 (无 IMU) */
    f[2] = 0xF3;   /* bit0=rumble(1),bit1=playerLED(1),bit2=accel(0),bit3=gyro(0),
                     * bit4=LX/LY(1),bit5=RX/RY(1),bit6=LT(1),bit7=RT(1) */

    /* 能力标志 2: RGB LED 默认开启, 无触摸板 */
    f[3] = 0x02;   /* bit1=RGB LED */

    /* 手柄类型: 标准 */
    f[4] = 1;

    /* 面键风格 (高 3 位): 1 = Xbox (ABXY), 子产品 (低 5 位): 0 */
    f[5] = (1 << 5);

    /* 轮询率: 1000Hz (uint16 LE = 0x03E8) */
    f[6] = 0xE8;
    f[7] = 0x03;

    /* 加速计/陀螺仪范围: 不支持, 全 0 */


    /* 按键使用掩码: SDL 用此判断哪些按钮活跃 */
    f[12] = 0xFF;  /* Byte 0: EAST|SOUTH|NORTH|WEST|DU|DD|DL|DR */
    f[13] = 0xFF;  /* Byte 1: L3|R3|L1|R1|L2|R2|(2 paddles) */
    f[14] = 0x0F;  /* Byte 2: START|BACK|GUIDE|CAPTURE */
    f[15] = 0x00;  /* Byte 3: 无额外按钮 */

    /* 触摸板: 不支持 */
    f[16] = 0;
    f[17] = 0;

    /* 序列号 (6 字节芯片 UID, 从索引 2 开始取 6 字节) */
    for (int i = 0; i < 6; i++) {
        f[18 + i] = serial[i];
    }

    /* 剩余字节已由 memset 填充为 0 */
}

/*********************************************************************
 * 解析震动命令
 *********************************************************************/
bool sinput_parse_haptic(const uint8_t *data, uint16_t len,
                          uint8_t *left, uint8_t *right)
{
    if (data == NULL || left == NULL || right == NULL)
        return false;

    if (len < 7)    /* report_id(1) + command(1) + data[5] */
        return false;

    if (data[0] != SINPUT_REPORT_ID_OUTPUT)
        return false;

    const sinput_output_t *out = (const sinput_output_t *)data;

    if (out->command != SINPUT_CMD_HAPTIC)
        return false;

    const sinput_haptic_t *haptic = (const sinput_haptic_t *)out->data;
    *left  = haptic->left_amplitude;
    *right = haptic->right_amplitude;

    return true;
}
