/********************************************
 * Stick — CH592F Stadia USB Dongle
 * 复合 USB 设备: CDC (日志) + HID (游戏手柄)
 *
 * 参考:
 *   sdk/EVT/EXAM/USB/Device/HID_CompliantDev/src/Main.c  — HID 框架
 *   sdk/EVT/EXAM/USB/Device/COM/src/Main.c               — CDC 帧
 *   sdk/EVT/EXAM/SRC/StdPeriphDriver/CH59x_usbdev.c      — USB 驱动
 *   Hand Held Legend SInput 规范 — https://github.com/HandHeldLegend/SInput-HID
 ********************************************/

#include "sinput_usb.h"
#include "platform.h"
#include <stdio.h>
#ifdef CFG_CH582
#include "CH58x_common.h"
#else
#include "CH59x_common.h"
#endif
#include <string.h>
#include <stdarg.h>

/* ---- USB 标识 ---- */
#define USB_VID     0x2E8A
#define USB_PID     0x10C6

/* ---- 全局状态 ---- */
volatile bool g_usb_ready = false;
uint8_t       g_usb_config = 0;
static void (*g_output_cb)(uint8_t*, uint16_t) = NULL;

/* ---- HID 输出报告延迟处理 (ISR → 主循环) ---- */
volatile bool  g_hid_out_pending = false;
static uint8_t g_hid_out_data[64];
static uint16_t g_hid_out_len = 0;

/* ---- CDC 状态 ---- */
static uint8_t  cdc_line_coding[7] = { 0xC0, 0xC6, 0x2D, 0x00, 0x00, 0x00, 0x08 }; /* 115200 8N1 */
static bool     cdc_serial_state = false;
static bool     cdc_host_open = false;     /* 主机已打开串口 (收到 SET_CONTROL_LINE_STATE DTR=1) */

/* ---- 端点 DMA 缓冲区 ---- */
__attribute__((aligned(4))) uint8_t EP0_Databuf[64 + 64 + 64]; /* EP0+EP4 */
__attribute__((aligned(4))) uint8_t EP1_Databuf[64 + 64];      /* EP1 CDC Notify (IN+OUT) */
__attribute__((aligned(4))) uint8_t EP2_Databuf[64 + 64];      /* EP2 CDC Data */
__attribute__((aligned(4))) uint8_t EP3_Databuf[64 + 64];      /* EP3 HID */

/* ---- USB 请求处理状态 ---- */
static uint8_t       g_req_code;
static uint16_t      g_req_len;
static const uint8_t *g_p_descr;
static uint8_t       g_hid_idle = 0;
static uint8_t       g_hid_proto = 0;

/* ===================================================================
 * 描述符
 * 注意: hid_report_descr 必须定义在 cfg_descr 之前（因为 cfg_descr 用 sizeof）
 * =================================================================== */

/*
 * SInput HID 报告描述符
 *
 * 参考: Hand Held Legend SInput 规范 (https://github.com/HandHeldLegend/SInput-HID)
 *
 * 三个报告 ID:
 *   0x02 — 功能响应 (63 字节, 作为 Input 报告响应 SDL FEATURES 命令)
 *   0x01 — 输入报告 (64 字节: 插头状态+电量+32按钮+4轴+2扳机+填充)
 *   0x03 — 输出报告 (48 字节: 震动/LED 命令)
 *
 * 关键: Report ID 0x02 是 Steam/SDL SInput 驱动握手必需的,
 *       没有它 RetroactiveSDLFeatures() 握手失败,
 *       Steam 会回退到通用白板手柄.
 */
static const uint8_t hid_report_descr[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)

    /* === 功能响应报告 (Report ID 0x02, 63 字节) === */
    0x85, 0x02,        //   Report ID (2)
    0x06, 0x00,0xFF,   //   Usage Page (Vendor Defined)
    0x09, 0x05,        //   Usage (Vendor Usage 5) — Feature Response
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF,0x00,   //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x3F,        //   Report Count (63)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    /* === 输入报告 (Report ID 0x01, 64 字节) === */
    0x85, 0x01,        //   Report ID (1)

    /* 插头状态 + 电池电量 (2 字节, Data 而非 Const 让主机读到) */
    0x05, 0x06,        //   Usage Page (Generic Device Controls)
    0x09, 0x20,        //   Usage (Battery Strength)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF,0x00,   //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    /* 32 个按键 (4 字节) */
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x20,        //   Usage Maximum (Button 32)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x20,        //   Report Count (32)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    /* 左摇杆 X/Y (16 位有符号) */
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x30,        //   Usage (X) — 左 X
    0x09, 0x31,        //   Usage (Y) — 左 Y
    0x09, 0x32,        //   Usage (Z) — 右 X (SInput)
    0x09, 0x35,        //   Usage (Rz) — 右 Y (SInput)
    0x16, 0x00,0x80,   //   Logical Minimum (-32768)
    0x26, 0xFF,0x7F,   //   Logical Maximum (32767)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    /* 扳机 (16 位, 0~32767) */
    0x09, 0x33,        //   Usage (Rx) — 左扳机 (SInput)
    0x09, 0x34,        //   Usage (Ry) — 右扳机 (SInput)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF,0x7F,   //   Logical Maximum (32767)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    /* 45 字节填充 (对齐 64B, Const) */
    0x06, 0x00,0xFF,   //   Usage Page (Vendor Defined)
    0x09, 0x01,        //   Usage (Vendor 1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF,0x00,   //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x2D,        //   Report Count (45)
    0x81, 0x03,        //   Input (Const,Var,Abs)

    /* === 输出报告 (Report ID 0x03, 48 字节, 震动命令) === */
    0x85, 0x03,        //   Report ID (3)
    0x06, 0x00,0xFF,   //   Usage Page (Vendor Defined)
    0x09, 0x01,        //   Usage (Vendor 1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF,0x00,   //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x30,        //   Report Count (48)
    0x91, 0x02,        //   Output (Data,Var,Abs)

    0xC0               // End Collection
};
#define HID_RPT_DLEN  (sizeof(hid_report_descr))

/* 设备描述符: IAD 复合设备 (CDC+HID, 参考 WCH 例程)
 * 注意: Windows 要求 CDC 复合设备 bDeviceClass=0xEF,bDeviceSubClass=0x02,bDeviceProtocol=0x01
 *       否则无法正确加载 usbser.sys，枚举失败 Code 10 */
static const uint8_t dev_descr[] = {
    0x12,0x01, 0x00,0x02, 0xEF,0x02,0x01, EP0_SIZE,
     (USB_VID)&0xFF, (USB_VID)>>8, (USB_PID)&0xFF, (USB_PID)>>8,
     0x00,0x01,
    0x01, 0x02, 0x00, 0x01
};

/* CDC+HID 配置描述符 */
/* 端点分配: EP1=CDC通知, EP2=CDC数据, EP3=HID */
#define EPSIZE_CDC  64
#define EPSIZE_HID  64
static const uint8_t cfg_descr[] = {
    0x09,0x02, 0x6B,0x00, 0x03, 0x01,0x00, 0x80,0x32,
    0x08,0x0B, 0x00,0x02, 0x02,0x02,0x01, 0x00,
    0x09,0x04, 0x00,0x00, 0x01, 0x02,0x02,0x01, 0x00,
    0x05,0x24, 0x00,0x10,0x01,
    0x05,0x24, 0x01,0x00,0x01,
    0x04,0x24, 0x02,0x02,
    0x05,0x24, 0x06,0x00,0x01,
    0x07,0x05, 0x81,0x03, EPSIZE_CDC,0x00, 0x01,
    0x09,0x04, 0x01,0x00, 0x02, 0x0A,0x00,0x00, 0x00,
    0x07,0x05, 0x02,0x02, EPSIZE_CDC,0x00, 0x00,
    0x07,0x05, 0x82,0x02, EPSIZE_CDC,0x00, 0x00,
    0x09,0x04, 0x02,0x00, 0x02, 0x03,0x00,0x00, 0x02,
    0x09,0x21, 0x00,0x01, 0x00,0x01, 0x22,
    (HID_RPT_DLEN)&0xFF, (HID_RPT_DLEN)>>8,
    0x07,0x05, 0x83,0x03, EPSIZE_HID,0x00, 0x01,
    0x07,0x05, 0x03,0x03, EPSIZE_HID,0x00, 0x01,
};

/* 接口2 HID 描述符在 cfg_descr 中的偏移 (见上方布局注释) */
#define CFG_HID_DESCR_OFFSET 84

_Static_assert(sizeof(cfg_descr) == 0x6B, "cfg_descr total length mismatch");
_Static_assert(CFG_HID_DESCR_OFFSET == 84, "HID descriptor offset mismatch");

/* 字符串 */
static const uint8_t str_lang[]   = { 0x04,0x03, 0x09,0x04 };
static const uint8_t str_manu[]   = { 0x0C,0x03, 'S',0,'t',0,'i',0,'c',0,'k',0 };
static const uint8_t str_prod[]   = { 0x1E,0x03, 'S',0,'t',0,'i',0,'c',0,'k',0,' ',0,'(',0,'S',0,'I',0,'n',0,'p',0,'u',0,'t',0,')',0 };

/* ===================================================================
 * CDC 数据发送 (环形缓冲 + 批量发送)
 * =================================================================== */
static uint8_t  cdc_tx_buf[CDC_TX_BUF_SIZE];
static volatile uint16_t cdc_tx_head = 0;
static volatile uint16_t cdc_tx_tail = 0;

void CDC_PutChar(char c)
{
    uint16_t next = (cdc_tx_head + 1) % CDC_TX_BUF_SIZE;
    if (next != cdc_tx_tail) {
        cdc_tx_buf[cdc_tx_head] = c;
        cdc_tx_head = next;
    }
}

/* 刷新 CDC 发送缓冲 (在主循环中调用) */
void CDC_Flush(void)
{
    if (!g_usb_ready || !cdc_host_open || cdc_tx_head == cdc_tx_tail) return;

    /* 读取 EP2_IN 当前状态, 如果上一次发送未完成则跳过 */
    if ((R8_UEP2_CTRL & MASK_UEP_T_RES) == UEP_T_RES_ACK)
        return;

    uint16_t len = 0;
    while (cdc_tx_tail != cdc_tx_head && len < EP2_SIZE) {
        pEP2_IN_DataBuf[len++] = cdc_tx_buf[cdc_tx_tail];
        cdc_tx_tail = (cdc_tx_tail + 1) % CDC_TX_BUF_SIZE;
    }

    if (len > 0) {
        R8_UEP2_T_LEN = len;
        R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    }
}

/* 重定向 printf 输出 → CDC（无硬件 UART，始终启用） */
int _write(int fd, char *buf, int size)
{
    (void)fd;
    for (int i = 0; i < size; i++) {
        if (buf[i] == '\n') CDC_PutChar('\r');
        CDC_PutChar(buf[i]);
    }
    return size;
}

/* ===================================================================
 * HID API
 * =================================================================== */
bool SInputUSB_SendReport(uint8_t *data, uint16_t len)
{
    if (len > EP3_SIZE) len = EP3_SIZE;
    /* EP3 IN 防覆写: 前次数据主机尚未轮询时 (ACK) 跳过.
     * 这对 Feature Response (0x02) 是必要的 — 它不能被后续的输入报告覆盖.
     * 但输入报告 (0x01) 被丢弃时调用方应保留 ready 标志在下一轮重试. */
    if ((R8_UEP3_CTRL & MASK_UEP_T_RES) == UEP_T_RES_ACK)
        return false;
    memcpy(pEP3_IN_DataBuf, data, len);
    R8_UEP3_T_LEN = len;
    R8_UEP3_CTRL = (R8_UEP3_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_ACK;
    return true;
}

void SInputUSB_SetOutputCallback(void (*cb)(uint8_t*, uint16_t))
{
    g_output_cb = cb;
}

bool SInputUSB_IsReady(void) { return g_usb_ready; }

/* ===================================================================
 * EP3 OUT 处理 — HID 输出报告（震动命令等）
 *
 * 只在 ISR 中拷贝数据+设标志，实际回调处理在主循环。
 * 原因: 回调可能调用 LOG(printf) 或 BLE API, 非中断安全。
 * =================================================================== */
static void HID_OUT_Deal(uint8_t len)
{
    if (len > sizeof(g_hid_out_data))
        len = sizeof(g_hid_out_data);
    memcpy(g_hid_out_data, pEP3_OUT_DataBuf, len);
    g_hid_out_len = len;
    g_hid_out_pending = true;
}

/**
 * @brief   在主循环中调用：处理缓存的 HID 输出报告
 */
void SInputUSB_ProcessOutput(void)
{
    if (!g_hid_out_pending)
        return;
    g_hid_out_pending = false;
    if (g_output_cb && g_hid_out_len > 0)
        g_output_cb(g_hid_out_data, g_hid_out_len);
}

/* ===================================================================
 * USB 中断处理
 * =================================================================== */
void USB_DevTransProcess(void)
{
    uint8_t len, chtype;
    uint8_t intflag, errflag = 0;

    intflag = R8_USB_INT_FG;

    if (intflag & RB_UIF_TRANSFER) {
        if ((R8_USB_INT_ST & MASK_UIS_TOKEN) != MASK_UIS_TOKEN) {
            switch (R8_USB_INT_ST & (MASK_UIS_TOKEN | MASK_UIS_ENDP)) {

            /* ---- EP0 IN ---- */
            case UIS_TOKEN_IN:
                switch (g_req_code) {
                case USB_GET_DESCRIPTOR:
                    len = g_req_len >= EP0_SIZE ? EP0_SIZE : g_req_len;
                    memcpy(pEP0_DataBuf, g_p_descr, len);
                    g_req_len -= len;
                    g_p_descr += len;
                    R8_UEP0_T_LEN = len;
                    R8_UEP0_CTRL ^= RB_UEP_T_TOG;
                    break;
                case USB_SET_ADDRESS:
                    R8_USB_DEV_AD = (R8_USB_DEV_AD & RB_UDA_GP_BIT) | g_req_len;
                    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                    break;
                default:
                    R8_UEP0_T_LEN = 0;
                    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
                    g_usb_ready = true;
                    break;
                }
                break;

            /* ---- EP0 OUT (SET_LINE_CODING 数据到达) ---- */
            case UIS_TOKEN_OUT:
                len = R8_USB_RX_LEN;
                if (g_req_code == 0x20 && len >= 7) {
                    memcpy(cdc_line_coding, pEP0_DataBuf, 7);
                }
                break;

            /* ---- EP1 IN (CDC Notify) ---- */
            case UIS_TOKEN_IN | 1:
                R8_UEP1_CTRL ^= RB_UEP_T_TOG;
                R8_UEP1_CTRL = (R8_UEP1_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                break;

            /* ---- EP2 OUT (CDC 数据入) ---- */
            case UIS_TOKEN_OUT | 2:
                if (R8_USB_INT_ST & RB_UIS_TOG_OK) {
                    R8_UEP2_CTRL ^= RB_UEP_R_TOG;
                    len = R8_USB_RX_LEN;
                }
                break;

            /* ---- EP2 IN (CDC 数据出) ---- */
            case UIS_TOKEN_IN | 2:
                R8_UEP2_CTRL ^= RB_UEP_T_TOG;
                R8_UEP2_CTRL = (R8_UEP2_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                break;

            /* ---- EP3 OUT (HID 输出) ---- */
            case UIS_TOKEN_OUT | 3:
                if (R8_USB_INT_ST & RB_UIS_TOG_OK) {
                    R8_UEP3_CTRL ^= RB_UEP_R_TOG;
                    len = R8_USB_RX_LEN;
                    HID_OUT_Deal(len);
                }
                break;

            /* ---- EP3 IN (HID 输入) ---- */
            case UIS_TOKEN_IN | 3:
                R8_UEP3_CTRL ^= RB_UEP_T_TOG;
                R8_UEP3_CTRL = (R8_UEP3_CTRL & ~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                break;
            }
            R8_USB_INT_FG = RB_UIF_TRANSFER;
        }

        /* ---- SETUP 包 ---- */
        if (R8_USB_INT_ST & RB_UIS_SETUP_ACT) {
            R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
            g_req_len = pSetupReqPak->wLength;
            g_req_code = pSetupReqPak->bRequest;
            chtype = pSetupReqPak->bRequestType;

            len = 0; errflag = 0;

            if ((chtype & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD) {
                if ((chtype & 0x20) && ((chtype & 0x0F) == 1)) {
                    /* 类请求发往接口 — 检查 wIndex 区分 HID(2) 和 CDC(0) */
                    uint8_t iface = pSetupReqPak->wIndex & 0xFF;
                    if (iface == 2) {
                        /* HID 类请求 */
                        switch (g_req_code) {
                        case 0x0A: g_hid_idle = EP0_Databuf[3]; break;
                        case 0x09: break;
                        case 0x0B: g_hid_proto = EP0_Databuf[2]; break;
                        case 0x02: EP0_Databuf[0] = g_hid_idle; len = 1; break;
                        case 0x03: EP0_Databuf[0] = g_hid_proto; len = 1; break;
default: errflag = 0xFF; break;
                        }
                    } else if (iface == 0) {
                        /* CDC 类请求 */
                        switch (g_req_code) {
                        case 0x20: /* SET_LINE_CODING — 数据在后续 OUT 阶段到达 */
                            break;
                        case 0x21: /* GET_LINE_CODING */
                            memcpy(pEP0_DataBuf, cdc_line_coding, 7);
                            len = 7;
                            break;
                        case 0x22: /* SET_CONTROL_LINE_STATE */
                            cdc_serial_state = (pSetupReqPak->wValue & 0x01) != 0;
                            cdc_host_open = cdc_serial_state;
                            break;
                        default: errflag = 0xFF; break;
                        }
                    } else {
                        errflag = 0xFF;
                    }
                } else {
                    errflag = 0xFF;
                }
            } else {
                /* 标准请求 */
                switch (g_req_code) {
                case USB_GET_DESCRIPTOR:
                    switch ((pSetupReqPak->wValue) >> 8) {
                    case USB_DESCR_TYP_DEVICE:
                        g_p_descr = dev_descr; len = dev_descr[0]; break;
                    case USB_DESCR_TYP_CONFIG:
                        g_p_descr = cfg_descr; len = cfg_descr[2]; break;
                    case USB_DESCR_TYP_HID:
                        g_p_descr = (uint8_t*)(&cfg_descr[CFG_HID_DESCR_OFFSET]); len = 9; break;
                    case USB_DESCR_TYP_REPORT:
                        g_p_descr = hid_report_descr; len = sizeof(hid_report_descr); break;
                    case USB_DESCR_TYP_STRING:
                        switch ((pSetupReqPak->wValue)&0xFF) {
                        case 0: g_p_descr=str_lang; len=str_lang[0]; break;
                        case 1: g_p_descr=str_manu; len=str_manu[0]; break;
                        case 2: g_p_descr=str_prod; len=str_prod[0]; break;
                        default: errflag=0xFF; break;
                        } break;
                    default: errflag=0xFF; break;
                    }
                    if (g_req_len > len) g_req_len = len;
                    len = (g_req_len >= EP0_SIZE) ? EP0_SIZE : g_req_len;
                    memcpy(pEP0_DataBuf, g_p_descr, len);
                    g_p_descr += len;
                    g_req_len -= len;
                    break;

                case USB_SET_ADDRESS:
                    g_req_len = (pSetupReqPak->wValue) & 0xFF;
                    break;

                case USB_GET_CONFIGURATION:
                    pEP0_DataBuf[0] = g_usb_config;
                    if (g_req_len > 1) g_req_len = 1;
                    break;

                case USB_SET_CONFIGURATION:
                    g_usb_config = (pSetupReqPak->wValue) & 0xFF;
                    break;

                case USB_CLEAR_FEATURE:
                    if ((chtype & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
                        switch ((pSetupReqPak->wIndex)&0xFF) {
                        case 0x81: R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_T_TOG|MASK_UEP_T_RES)) | UEP_T_RES_NAK; break;
                        case 0x01: R8_UEP1_CTRL = (R8_UEP1_CTRL & ~(RB_UEP_R_TOG|MASK_UEP_R_RES)) | UEP_R_RES_ACK; break;
                        case 0x82: R8_UEP2_CTRL = (R8_UEP2_CTRL & ~(RB_UEP_T_TOG|MASK_UEP_T_RES)) | UEP_T_RES_NAK; break;
                        case 0x02: R8_UEP2_CTRL = (R8_UEP2_CTRL & ~(RB_UEP_R_TOG|MASK_UEP_R_RES)) | UEP_R_RES_ACK; break;
                        case 0x83: R8_UEP3_CTRL = (R8_UEP3_CTRL & ~(RB_UEP_T_TOG|MASK_UEP_T_RES)) | UEP_T_RES_NAK; break;
                        case 0x03: R8_UEP3_CTRL = (R8_UEP3_CTRL & ~(RB_UEP_R_TOG|MASK_UEP_R_RES)) | UEP_R_RES_ACK; break;
                        default: errflag=0xFF; break;
                        }
                    } else errflag = 0xFF;
                    break;

                case USB_SET_FEATURE:
                    if ((chtype & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
                        switch (pSetupReqPak->wIndex) {
                        case 0x81: R8_UEP1_CTRL = (R8_UEP1_CTRL&~(RB_UEP_T_TOG|MASK_UEP_T_RES))|UEP_T_RES_STALL; break;
                        case 0x01: R8_UEP1_CTRL = (R8_UEP1_CTRL&~(RB_UEP_R_TOG|MASK_UEP_R_RES))|UEP_R_RES_STALL; break;
                        case 0x82: R8_UEP2_CTRL = (R8_UEP2_CTRL&~(RB_UEP_T_TOG|MASK_UEP_T_RES))|UEP_T_RES_STALL; break;
                        case 0x02: R8_UEP2_CTRL = (R8_UEP2_CTRL&~(RB_UEP_R_TOG|MASK_UEP_R_RES))|UEP_R_RES_STALL; break;
                        case 0x83: R8_UEP3_CTRL = (R8_UEP3_CTRL&~(RB_UEP_T_TOG|MASK_UEP_T_RES))|UEP_T_RES_STALL; break;
                        case 0x03: R8_UEP3_CTRL = (R8_UEP3_CTRL&~(RB_UEP_R_TOG|MASK_UEP_R_RES))|UEP_R_RES_STALL; break;
                        default: errflag=0xFF; break;
                        }
                    } else errflag = 0xFF;
                    break;

                case USB_GET_INTERFACE:
                    pEP0_DataBuf[0] = 0x00;
                    if (g_req_len > 1) g_req_len = 1;
                    break;

                case USB_SET_INTERFACE:
                    break;

                case USB_GET_STATUS:
                    pEP0_DataBuf[0] = 0x00; pEP0_DataBuf[1] = 0x00;
                    if (g_req_len >= 2) g_req_len = 2;
                    break;

                default: errflag = 0xFF; break;
                }
            }

            if (errflag == 0xFF) {
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
            } else {
                if (chtype & 0x80) {
                    if (g_req_code != USB_GET_DESCRIPTOR)
                        len = (g_req_len > EP0_SIZE) ? EP0_SIZE : g_req_len;
                } else {
                    len = 0;
                }
                R8_UEP0_T_LEN = len;
                R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
            }
            R8_USB_INT_FG = RB_UIF_TRANSFER;
        }
    } else if (intflag & RB_UIF_BUS_RST) {
        R8_USB_DEV_AD = 0;
        R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        g_usb_ready = false;
        cdc_host_open = false;
        R8_USB_INT_FG = RB_UIF_BUS_RST;
    } else if (intflag & RB_UIF_SUSPEND) {
        if (R8_USB_MIS_ST & RB_UMS_SUSPEND)
            g_usb_ready = false;
        R8_USB_INT_FG = RB_UIF_SUSPEND;
    } else
        R8_USB_INT_FG = intflag;
}

/* ===================================================================
 * USB 中断 ISR
 * =================================================================== */
__INTERRUPT
__HIGH_CODE
void USB_IRQHandler(void) { USB_DevTransProcess(); }

/* ===================================================================
 * 初始化
 * =================================================================== */
void SInputUSB_Init(void)
{
    if (cfg_descr[CFG_HID_DESCR_OFFSET] != 0x09 ||
        cfg_descr[CFG_HID_DESCR_OFFSET + 1] != 0x21) {
        printf("[HID] DESCRIPTOR LAYOUT DRIFT\n");
        platform_reboot();
    }

    pEP0_RAM_Addr = EP0_Databuf;
    pEP1_RAM_Addr = EP1_Databuf;  /* CDC Notify */
    pEP2_RAM_Addr = EP2_Databuf;  /* CDC Data */
    pEP3_RAM_Addr = EP3_Databuf;  /* HID */

    USB_DeviceInit();
    PFIC_EnableIRQ(USB_IRQn);

    printf("[USB] CDC+HID init\n");
}
