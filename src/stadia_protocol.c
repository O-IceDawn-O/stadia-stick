/********************************************
 * Stick — CH592F Stadia USB Dongle
 * Stadia 手柄 BLE 报文解析
 * 无硬件依赖，纯 C 算法，可在任何平台编译测试。
 ********************************************/

#include "stadia_protocol.h"
#include <stddef.h>  /* NULL */

/* 按钮列映射表: (Stadia bits, JP_BUTTON bit)
 * 参考 stadia_bt.c 的按键映射 */
#define PAIR(stadia_bit, jp_bit)  { (stadia_bit), (jp_bit) }

typedef struct {
    uint8_t stadia_mask;
    uint32_t jp_mask;
} btn_map_t;

/* buttons1 映射 (byte 2) */
static const btn_map_t btn1_map[] = {
    PAIR(STADIA_BTN1_A3,  1 << 18),  /* A3 → JP_BUTTON_A3 (Assistant/Mute) */
    PAIR(STADIA_BTN1_A2,  1 << 17),  /* A2 → JP_BUTTON_A2 (Capture)        */
    PAIR(STADIA_BTN1_L2,  1 << 6),   /* L2 → JP_BUTTON_L2                  */
    PAIR(STADIA_BTN1_R2,  1 << 7),   /* R2 → JP_BUTTON_R2                  */
    PAIR(STADIA_BTN1_A1,  1 << 16),  /* A1 → JP_BUTTON_A1 (Stadia/Guide)   */
    PAIR(STADIA_BTN1_S2,  1 << 9),   /* S2 → JP_BUTTON_S2 (Menu/Start)    */
    PAIR(STADIA_BTN1_S1,  1 << 8),   /* S1 → JP_BUTTON_S1 (Options/Back)   */
    PAIR(STADIA_BTN1_R3,  1 << 11),  /* R3 → JP_BUTTON_R3                  */
};

/* buttons2 映射 (byte 3) */
static const btn_map_t btn2_map[] = {
    PAIR(STADIA_BTN2_L3,  1 << 10),  /* L3 → JP_BUTTON_L3                */
    PAIR(STADIA_BTN2_R1,  1 << 5),   /* R1 → JP_BUTTON_R1                */
    PAIR(STADIA_BTN2_L1,  1 << 4),   /* L1 → JP_BUTTON_L1                */
    PAIR(STADIA_BTN2_B4,  1 << 3),   /* B4 → JP_BUTTON_B4 (Y)            */
    PAIR(STADIA_BTN2_B3,  1 << 2),   /* B3 → JP_BUTTON_B3 (X)            */
    PAIR(STADIA_BTN2_B2,  1 << 1),   /* B2 → JP_BUTTON_B2 (B)            */
    PAIR(STADIA_BTN2_B1,  1 << 0),   /* B1 → JP_BUTTON_B1 (A)            */
};

/*********************************************************************
 * 解析 Stadia HID 输入报告
 *********************************************************************/
bool stadia_parse_report(const uint8_t *data, uint16_t len,
                          uint32_t *buttons_out, uint8_t analog_out[6])
{
    uint32_t buttons = 0;
    uint8_t offset = 0;

    /* 参数检查 */
    if (data == NULL || buttons_out == NULL || analog_out == NULL)
        return false;

    /* 如果有报告 ID 字节 (0x03) 则跳过 */
    if (len >= 11 && data[0] == 0x03) {
        offset = 1;
    }

    /* 至少需要 10 字节有效数据 */
    if (len - offset < 10)
        return false;

    const uint8_t *rpt = data + offset;

    /* ---- 解析按键 ---- */
    uint8_t b1 = rpt[1];  /* buttons1 */
    uint8_t b2 = rpt[2];  /* buttons2 */

    for (int i = 0; i < (int)(sizeof(btn1_map) / sizeof(btn1_map[0])); i++) {
        if (b1 & btn1_map[i].stadia_mask)
            buttons |= btn1_map[i].jp_mask;
    }
    for (int i = 0; i < (int)(sizeof(btn2_map) / sizeof(btn2_map[0])); i++) {
        if (b2 & btn2_map[i].stadia_mask)
            buttons |= btn2_map[i].jp_mask;
    }

    /* ---- D-pad 帽型开关 ---- */
    uint8_t dpad = rpt[0];
    if (dpad <= 7) {
        /* 映射到 4 个方向位 */
        if (dpad == STADIA_DPAD_UP || dpad == STADIA_DPAD_UP_RIGHT || dpad == STADIA_DPAD_UP_LEFT)
            buttons |= (1 << 12);  /* JP_BUTTON_DU */
        if (dpad == STADIA_DPAD_DOWN || dpad == STADIA_DPAD_DOWN_RIGHT || dpad == STADIA_DPAD_DOWN_LEFT)
            buttons |= (1 << 13);  /* JP_BUTTON_DD */
        if (dpad == STADIA_DPAD_LEFT || dpad == STADIA_DPAD_UP_LEFT || dpad == STADIA_DPAD_DOWN_LEFT)
            buttons |= (1 << 14);  /* JP_BUTTON_DL */
        if (dpad == STADIA_DPAD_RIGHT || dpad == STADIA_DPAD_UP_RIGHT || dpad == STADIA_DPAD_DOWN_RIGHT)
            buttons |= (1 << 15);  /* JP_BUTTON_DR */
    }

    /* ---- 模拟值 ---- */
    analog_out[0] = rpt[3];  /* left_x  */
    analog_out[1] = rpt[4];  /* left_y  */
    analog_out[2] = rpt[5];  /* right_x */
    analog_out[3] = rpt[6];  /* right_y */
    analog_out[4] = rpt[7];  /* l2      */
    analog_out[5] = rpt[8];  /* r2      */

    *buttons_out = buttons;
    return true;
}

/*********************************************************************
 * 构建 Stadia 震动报告
 *
 * Stadia 震动格式 (参考 stadia_bt.c §206-213):
 *   4 字节, 无报告 ID, 两个 16 位小端马达值 (0-255 → 0-65535)
 *   buf[0..1] = 左马达 LE, buf[2..3] = 右马达 LE
 *
 * GATT 写输出报告特征时不需报告 ID 前缀(特征句柄已标识),
 * 与 USB HID 输出报告的格式不同。
 *********************************************************************/
void stadia_build_rumble_report(uint8_t left, uint8_t right, uint8_t buf[4])
{
    /* SInput 振幅 0-255 → Stadia 16 位: value * 257 */
    uint16_t l16 = (uint16_t)left * 257;
    uint16_t r16 = (uint16_t)right * 257;

    buf[0] = l16 & 0xFF;          /* 左马达低字节 */
    buf[1] = (l16 >> 8) & 0xFF;   /* 左马达高字节 */
    buf[2] = r16 & 0xFF;          /* 右马达低字节 */
    buf[3] = (r16 >> 8) & 0xFF;   /* 右马达高字节 */
}
