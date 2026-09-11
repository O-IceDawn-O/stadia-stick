# Stadia BLE 手柄协议

> 协议格式参考:
> DJm00n/ControllersInfo (HID 报告描述符), Chromium gamepad_standard_mappings (按键映射),
> SDL Stadia controller driver

## 概述

Stadia 手柄通过 BLE GATT 连接，使用 HID-over-GATT (HOGP) 协议传输输入报告。

## 连接参数

| 参数                   | 值                              |
| ---------------------- | ------------------------------- |
| 传输方式               | BLE GATT (通知)                 |
| HID 服务 UUID          | 0x1812 (Human Interface Device) |
| 输入/输出报告特征 UUID | 0x2A4D (Report)                 |
| CCCD UUID              | 0x2902                          |
| 输入报告负载长度       | 10 字节                         |

输入和输出报告使用同一个 UUID (0x2A4D)，通过 GATT `DiscAllChars` 按特征 properties 字节区分:
0x10 = NOTIFY (输入报告)，0x0C = WRITE | WRITE_NO_RSP (输出报告)。

## 输入报告格式

Stadia 手柄发送的 BLE 通知数据中，有效负载为 10 字节，可选前缀 1 字节报告 ID (0x03)，
总长度 10 字节（无前缀）或 11 字节（有前缀）。`stadia_parse_report()` 自动处理两种情形。

有效负载字节布局（去掉可选 0x03 前缀后）:

```
byte 0:     dpad (帽型开关)
            0=上, 1=右上, 2=右, 3=右下, 4=下, 5=左下, 6=左, 7=左上, 8=居中
byte 1:     buttons1
            bit0=A3(Assistant), bit1=A2(Capture), bit2=L2, bit3=R2,
            bit4=A1(Stadia/Guide), bit5=S2(Menu), bit6=S1(Options), bit7=R3
byte 2:     buttons2
            bit0=L3, bit1=R1, bit2=L1, bit3=B4(Y),
            bit4=B3(X), bit5=B2(B), bit6=B1(A)
byte 3:     left_x (0-255, 中心 128)
byte 4:     left_y (0-255, 中心 128)
byte 5:     right_x (0-255, 中心 128)
byte 6:     right_y (0-255, 中心 128)
byte 7:     l2_trigger (0-255)
byte 8:     r2_trigger (0-255)
byte 9:     consumer (多媒体按键，当前未使用)
```

D-pad 帽型开关映射为 4 个方向位:

| D-pad 值 | 触发方向位 |
| -------- | ---------- |
| 0 (上)   | DU         |
| 1 (右上) | DU + DR    |
| 2 (右)   | DR         |
| 3 (右下) | DD + DR    |
| 4 (下)   | DD         |
| 5 (左下) | DD + DL    |
| 6 (左)   | DL         |
| 7 (左上) | DU + DL    |
| 8 (居中) | 无         |

## 按键映射 (Stadia 位 → JP_BUTTON 位)

| Stadia            | 所在字节      | 位掩码 | JP_BUTTON 位 |
| ----------------- | ------------- | ------ | ------------ |
| B1 (A)            | buttons2 bit6 | 0x40   | bit 0        |
| B2 (B)            | buttons2 bit5 | 0x20   | bit 1        |
| B3 (X)            | buttons2 bit4 | 0x10   | bit 2        |
| B4 (Y)            | buttons2 bit3 | 0x08   | bit 3        |
| L1                | buttons2 bit2 | 0x04   | bit 4        |
| R1                | buttons2 bit1 | 0x02   | bit 5        |
| L2                | buttons1 bit2 | 0x04   | bit 6        |
| R2                | buttons1 bit3 | 0x08   | bit 7        |
| S1 (Options)      | buttons1 bit6 | 0x40   | bit 8        |
| S2 (Menu)         | buttons1 bit5 | 0x20   | bit 9        |
| L3                | buttons2 bit0 | 0x01   | bit 10       |
| R3                | buttons1 bit7 | 0x80   | bit 11       |
| D-pad 上          | —            | —     | bit 12       |
| D-pad 下          | —            | —     | bit 13       |
| D-pad 左          | —            | —     | bit 14       |
| D-pad 右          | —            | —     | bit 15       |
| A1 (Stadia/Guide) | buttons1 bit4 | 0x10   | bit 16       |
| A2 (Capture)      | buttons1 bit1 | 0x02   | bit 17       |
| A3 (Assistant)    | buttons1 bit0 | 0x01   | bit 18       |

JP_BUTTON 位由 `stadia_protocol.c` 中 `btn1_map[]` 和 `btn2_map[]` 定义，D-pad 方向由
`stadia_parse_report()` 中第 80-89 行逻辑产生。

## 震动输出报告 (4 字节)

通过 GATT Write Command (`GATT_WriteNoRsp`) 写入输出报告特征，**始终使用 Write Command，
无视特征属性标志**（特征仅声明 WRITE 时同样适用）。**不含报告 ID 前缀**
(特征句柄已标识报告类型)。

> 为什么不用 Write Request: Write Request 要求回执，回执一旦丢失 WCH 栈 ATT 过程标志永久阻塞，
> 所有后续 GATT 操作返回 bleTimeout(23)，震动永久失效。Write Command 无回执、不阻塞 ATT，
> 实测 Stadia 手柄接受 Write Command 写入输出报告特征。

```
byte 0-1:  左马达 (uint16 LE, 0-65535)
byte 2-3:  右马达 (uint16 LE, 0-65535)
```

SInput 振幅 (0-255) 转换为 Stadia 16 位值:

```
stadia_value = sinput_value * 257    /* 0→0, 255→65535 */
```

`stadia_build_rumble_report()` 直接生成 4 字节输出，无需额外封装。

## 电池服务

Battery Service (0x180F) 存在于 Stadia 手柄中，已成功发现 Battery Level 特征 (0x2A19)。
该特征属性为 0x02（仅 READ），无 CCCD，不支持通知。
通过 `GATT_ReadCharValue` 轮询读取电量 (0-100)，主循环每 60 秒自动轮询一次。
`g_stadia_battery` 默认为 0xFF（未知），成功读取后更新为 0-100。
