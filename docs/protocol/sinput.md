# SInput USB HID 协议

> 本协议完整定义见 Hand Held Legend SInput 规范:
> https://github.com/HandHeldLegend/SInput-HID
> https://docs.handheldlegend.com/s/sinput

## 本项目的子集

本项目仅使用 SInput 协议的一个子集——不包含 IMU、触摸板、RGB LED 等功能。
USB 设备为 CDC+HID 复合设备（IAD），CDC 用于分级日志输出，HID 用于 SInput
游戏手柄协议。

### USB 标识

| 字段     | 值                                       |
| -------- | ---------------------------------------- |
| VID      | 0x2E8A                                   |
| PID      | 0x10C6                                   |
| 设备类   | 0xEF (IAD 复合设备)                      |
| 产品名   | "Stick (SInput)"                         |
| HID 端点 | EP3 IN/OUT (中断, 64 字节)               |
| CDC 端点 | EP1 IN (中断通知), EP2 IN/OUT (批量数据) |

### 报告 ID

HID 报告描述符定义了三个 Report ID:

| ID   | 方向   | 长度    | 用途                              |
| ---- | ------ | ------- | --------------------------------- |
| 0x01 | Input  | 64 字节 | 主输入报告（按键+摇杆+扳机+电量） |
| 0x02 | Input  | 63 字节 | 功能响应（SDL SInput 握手）       |
| 0x03 | Output | 48 字节 | 输出命令（震动/功能查询）         |

Report ID 0x02 是 Steam SDL SInput 驱动握手必需的。没有它，
`RetroactiveSDLFeatures()` 会失败，Steam 回退到通用白板手柄。

### 输入报告 (64 字节, Report ID 0x01)

| 偏移  | 大小 | 字段                  | 说明                                                         |
| ----- | ---- | --------------------- | ------------------------------------------------------------ |
| 0     | 1    | report_id             | 0x01                                                         |
| 1     | 1    | plug_status           | 供电状态 (4 = 电池供电)                                      |
| 2     | 1    | charge_level          | 电量百分比 (来自 g_stadia_battery, 0-100, 0xFF=未知时取 100) |
| 3-6   | 4    | buttons               | 32 个按键位图 (LE, SINPUT_MASK_*)                            |
| 7-8   | 2    | lx                    | 左摇杆 X (-32768~32767)                                      |
| 9-10  | 2    | ly                    | 左摇杆 Y                                                     |
| 11-12 | 2    | rx                    | 右摇杆 X                                                     |
| 13-14 | 2    | ry                    | 右摇杆 Y                                                     |
| 15-16 | 2    | lt                    | 左扳机 (0~32767)                                             |
| 17-18 | 2    | rt                    | 右扳机 (0~32767)                                             |
| 19-24 | 6    | imu_timestamp + accel | IMU 数据 (未用, 填 0)                                        |
| 25-36 | 12   | touchpad1 + touchpad2 | 触摸板 (未用, 填 0)                                          |
| 37-63 | 17   | reserved              | 填充 0                                                       |

### 功能响应报告 (63 字节, Report ID 0x02)

此报告响应 SDL 的 FEATURES 输出命令（USB OUT [0x03,0x02]），通过 EP3 IN 主动发送，用于 SDL SInput 驱动握手。

| 偏移  | 大小 | 字段         | 说明                                  |
| ----- | ---- | ------------ | ------------------------------------- |
| 0     | 1    | command_echo | 命令回显 (SINPUT_CMD_FEATURES = 0x02) |
| 1-24  | 24   | capabilities | 能力结构体                            |
| 25-62 | 38   | zero_padding | 填充 0                                |

能力结构体 (24 字节，偏移 1):

| 偏移  | 大小 | 字段              | 值         | 说明                                                                                               |
| ----- | ---- | ----------------- | ---------- | -------------------------------------------------------------------------------------------------- |
| 1     | 2    | protocol_version  | 0x0001     | v1.0 (uint16 LE)                                                                                   |
| 3     | 1    | capabilities_1    | 0xF3       | bit0=rumble, bit1=playerLED, bit2=accel(0), bit3=gyro(0), bit4=LX/LY, bit5=RX/RY, bit6=LT, bit7=RT |
| 4     | 1    | capabilities_2    | 0x02       | bit1=RGB LED                                                                                       |
| 5     | 1    | controller_type   | 1          | 标准手柄                                                                                           |
| 6     | 1    | face_style        | 0x20       | bit7-5=1 (Xbox ABXY 布局)                                                                          |
| 7-8   | 2    | polling_rate      | 0x03E8     | 1000Hz (uint16 LE)                                                                                 |
| 9-12  | 4    | accel_gyro_range  | 0x00000000 | 不支持, 全 0                                                                                       |
| 13-16 | 4    | button_usage_mask | 见下       | 活跃按钮掩码                                                                                       |

按钮使用掩码 (4 字节):

| 字节 | 值   | 覆盖的按钮                        |
| ---- | ---- | --------------------------------- |
| 0    | 0xFF | EAST/SOUTH/NORTH/WEST/DU/DD/DL/DR |
| 1    | 0xFF | L3/R3/L1/R1/L2/R2/预留 paddles    |
| 2    | 0x0F | START/BACK/GUIDE/CAPTURE          |
| 3    | 0x00 | 无额外按钮                        |

序列号 (6 字节, 偏移 19-24): 芯片唯一 ID (取自 `platform_get_unique_id()`)。

### 输出报告 (48 字节, Report ID 0x03)

| 偏移 | 大小 | 字段      | 说明         |
| ---- | ---- | --------- | ------------ |
| 0    | 1    | report_id | 0x03         |
| 1    | 1    | command   | SINPUT_CMD_* |
| 2-47 | 46   | data      | 命令参数     |

### 输出命令

| 命令                  | 值   | 处理方式                       | 说明                                       |
| --------------------- | ---- | ------------------------------ | ------------------------------------------ |
| SINPUT_CMD_HAPTIC     | 0x01 | 提取振幅 → 转发到 Stadia 震动 | 由`sinput_parse_haptic()` 解析           |
| SINPUT_CMD_FEATURES   | 0x02 | 返回功能响应                   | 由`sinput_build_feature_response()` 构建 |
| SINPUT_CMD_PLAYER_LED | 0x03 | 忽略                           | 本项目中未实现                             |
| SINPUT_CMD_RGB_LED    | 0x04 | 忽略                           | 本项目中未实现                             |

### 震动命令数据 (SINPUT_CMD_HAPTIC)

`data` 字段前 5 字节为 `sinput_haptic_t` 结构体:

| 偏移 | 大小 | 字段            | 说明               |
| ---- | ---- | --------------- | ------------------ |
| 2    | 1    | type            | 马达类型 (2 = ERM) |
| 3    | 1    | left_amplitude  | 左马达振幅 (0-255) |
| 4    | 1    | left_brake      | 保留               |
| 5    | 1    | right_amplitude | 右马达振幅 (0-255) |
| 6    | 1    | right_brake     | 保留               |

`HID_OUT_Deal()` 在 USB ISR 中将输出报告拷贝到缓冲，主循环调用
`SInputUSB_ProcessOutput()` 触发回调，回调中 `sinput_parse_haptic()` 检查
report_id=0x03 且 command=0x01 后提取振幅值，缓存到全局变量等待
主循环写入 Stadia BLE 输出特征。

### SINPUT_MASK 按键位图

定义于 `sinput_builder.h`:

| 宏                  | 位    | 名称              |
| ------------------- | ----- | ----------------- |
| SINPUT_MASK_EAST    | 0     | B (右侧面键)      |
| SINPUT_MASK_SOUTH   | 1     | A (下面键)        |
| SINPUT_MASK_NORTH   | 2     | Y (上面键)        |
| SINPUT_MASK_WEST    | 3     | X (左侧面键)      |
| SINPUT_MASK_DU      | 4     | D-pad 上          |
| SINPUT_MASK_DD      | 5     | D-pad 下          |
| SINPUT_MASK_DL      | 6     | D-pad 左          |
| SINPUT_MASK_DR      | 7     | D-pad 右          |
| SINPUT_MASK_L3      | 8     | 左摇杆按下        |
| SINPUT_MASK_R3      | 9     | 右摇杆按下        |
| SINPUT_MASK_L1      | 10    | 左肩键 (LB)       |
| SINPUT_MASK_R1      | 11    | 右肩键 (RB)       |
| SINPUT_MASK_L2      | 12    | 左扳机数字 (LT)   |
| SINPUT_MASK_R2      | 13    | 右扳机数字 (RT)   |
| (未使用)            | 14-15 | —                |
| SINPUT_MASK_START   | 16    | Menu/Start        |
| SINPUT_MASK_BACK    | 17    | Options/Back      |
| SINPUT_MASK_GUIDE   | 18    | Guide/Stadia 按钮 |
| SINPUT_MASK_CAPTURE | 19    | Capture/截图      |

### 按键映射 (JP_BUTTON → SINPUT_MASK)

| JP_BUTTON              | SINPUT_MASK         | 位 |
| ---------------------- | ------------------- | -- |
| JP_BUTTON_B1 (A)       | SINPUT_MASK_SOUTH   | 1  |
| JP_BUTTON_B2 (B)       | SINPUT_MASK_EAST    | 0  |
| JP_BUTTON_B3 (X)       | SINPUT_MASK_WEST    | 3  |
| JP_BUTTON_B4 (Y)       | SINPUT_MASK_NORTH   | 2  |
| JP_BUTTON_L1           | SINPUT_MASK_L1      | 10 |
| JP_BUTTON_R1           | SINPUT_MASK_R1      | 11 |
| JP_BUTTON_L2           | SINPUT_MASK_L2      | 12 |
| JP_BUTTON_R2           | SINPUT_MASK_R2      | 13 |
| JP_BUTTON_L3           | SINPUT_MASK_L3      | 8  |
| JP_BUTTON_R3           | SINPUT_MASK_R3      | 9  |
| JP_BUTTON_S1 (Options) | SINPUT_MASK_BACK    | 17 |
| JP_BUTTON_S2 (Menu)    | SINPUT_MASK_START   | 16 |
| JP_BUTTON_A1 (Guide)   | SINPUT_MASK_GUIDE   | 18 |
| JP_BUTTON_A2 (Capture) | SINPUT_MASK_CAPTURE | 19 |
| D-pad 上               | SINPUT_MASK_DU      | 4  |
| D-pad 下               | SINPUT_MASK_DD      | 5  |
| D-pad 左               | SINPUT_MASK_DL      | 6  |
| D-pad 右               | SINPUT_MASK_DR      | 7  |

JP_BUTTON_A3 (Assistant) 未映射到 SInput 按键。

### 模拟值转换

| 输入                          | 转换公式                  | 输出范围     |
| ----------------------------- | ------------------------- | ------------ |
| Stadia 摇杆 (0-255, 中心 128) | `(value - 128) * 256`   | -32768~32767 |
| Stadia 扳机 (0-255)           | `(value * 32767) / 255` | 0~32767      |


