#ifndef __STADIA_PROTOCOL_H__
#define __STADIA_PROTOCOL_H__

#include <stdint.h>
#include <stdbool.h>

/*
 * Stadia 手柄 BLE HID 输入报告格式（10 字节）
 *
 * 协议来源: DJm00n/ControllersInfo, chromium gamepad_standard_mappings,
 *          Chrome WebHID blog post, SDL Stadia controller driver
 *
 * byte 0:     报告 ID (0x03)
 * byte 1:     dpad (帽型开关, 0=上, 8=居中)
 * byte 2:     buttons1 (A3/A2/L2/R2/A1/S2/S1/R3)
 * byte 3:     buttons2 (L3/R1/L1/B4/B3/B2/B1)
 * byte 4-5:   left_x, left_y (0-255, 中心 128)
 * byte 6-7:   right_x, right_y
 * byte 8:     l2_trigger (0-255)
 * byte 9:     r2_trigger (0-255)
 * byte 10:    consumer
 */

/* buttons1 位掩码 */
#define STADIA_BTN1_A3      0x01    /* Assistant     */
#define STADIA_BTN1_A2      0x02    /* Capture       */
#define STADIA_BTN1_L2      0x04    /* Left trigger  */
#define STADIA_BTN1_R2      0x08    /* Right trigger */
#define STADIA_BTN1_A1      0x10    /* Stadia 按钮   */
#define STADIA_BTN1_S2      0x20    /* Menu          */
#define STADIA_BTN1_S1      0x40    /* Options       */
#define STADIA_BTN1_R3      0x80    /* Right stick   */

/* buttons2 位掩码 */
#define STADIA_BTN2_L3      0x01    /* Left stick   */
#define STADIA_BTN2_R1      0x02    /* Right bumper */
#define STADIA_BTN2_L1      0x04    /* Left bumper  */
#define STADIA_BTN2_B4      0x08    /* Y            */
#define STADIA_BTN2_B3      0x10    /* X            */
#define STADIA_BTN2_B2      0x20    /* B            */
#define STADIA_BTN2_B1      0x40    /* A            */

/* D-pad 帽型开关值 (byte 1) */
#define STADIA_DPAD_UP      0
#define STADIA_DPAD_UP_RIGHT    1
#define STADIA_DPAD_RIGHT   2
#define STADIA_DPAD_DOWN_RIGHT  3
#define STADIA_DPAD_DOWN    4
#define STADIA_DPAD_DOWN_LEFT   5
#define STADIA_DPAD_LEFT    6
#define STADIA_DPAD_UP_LEFT 7
#define STADIA_DPAD_CENTER  8

/**
 * @brief   解析 Stadia 10 字节 HID 输入报告
 *
 * @param   data        原始报告数据
 * @param   len         数据长度（至少 10 字节）
 * @param   buttons_out [out] JP_BUTTON_* 位图
 * @param   analog_out  [out] 6 字节模拟值: [LX, LY, RX, RY, L2, R2]
 * @return  true 解析成功
 */
bool stadia_parse_report(const uint8_t *data, uint16_t len,
                          uint32_t *buttons_out, uint8_t analog_out[6]);

/**
 * @brief   构建 Stadia 震动报告（4 字节, 无报告 ID）
 *
 *          格式: [left_lo][left_hi][right_lo][right_hi]
 *          振幅 0-255 → 16 位: value * 257
 *
 * @param   left    左马达振幅 (0-255)
 * @param   right   右马达振幅 (0-255)
 * @param   buf     [out] 4 字节数据 (直接用于 GATT 写入)
 */
void stadia_build_rumble_report(uint8_t left, uint8_t right, uint8_t buf[4]);

#endif /* __STADIA_PROTOCOL_H__ */
