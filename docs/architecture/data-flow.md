# 数据流

## 主流水线

Stadia 手柄的数据从 BLE 接收到 USB 发出，经过三个处理阶段：

```
BLE 通知到达
     │
     ▼
┌──────────────────────┐
│ 阶段 1: BLE 接收     │
│                      │
│ TMOS 事件循环捕获     │
│ GATT 通知回调触发     │
│ 原始数据存入           │
│ g_stadia_raw_data[]  │
│ 设置 g_stadia_report │
│ _ready = true        │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│ 阶段 2: Stadia 解析   │
│                      │
│ stadia_parse_report()│
│ 输入: 原始报告        │
│ (可变长度, 可选0x03   │
│  报告 ID 前缀)        │
│ 输出: uint32_t        │
│ buttons (JP_BUTTON)  │
│ uint8_t analog[6]    │
│ (LX, LY, RX, RY,    │
│  L2, R2)             │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│ 阶段 3: SInput 构建   │
│                      │
│ sinput_build_report   │
│ (buttons, analog,    │
│  battery, report)    │
│ 输出: 64 字节         │
│ sinput_report_t      │
│ (report_id=0x01,     │
│  plug_status,        │
│  charge_level,       │
│  buttons[4],         │
│  lx/ly/rx/ry,        │
│  lt/rt)              │
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│ 阶段 4: USB 发送     │
│                      │
│ SInputUSB_SendReport │
│ ()                   │
│ 加载 EP3 IN 缓冲区   │
│ (64 字节)            │
│ 触发 IN 传输         │
│ 主机在 1ms 内轮询    │
│ 读取                 │
└──────────────────────┘
```

## 主循环步骤

每次主循环迭代依次执行：

```
 1. update_tick()              SysTick 毫秒计数
 2. platform_wdog_feed()       喂狗（防 BLE 死锁）
 3. CDC_Flush()                刷新 CDC 日志发送
 4. SInputUSB_ProcessOutput()  处理 USB OUT 报告（震动/Feature）
 5. Button_Task()              按键消抖 + 边缘检测
 6. Button 事件处理            扫描/停止/断开控制
 7. Feature 响应               构建并发送 Report ID 0x02
 8. LED 更新                   INIT=常亮/IDLE=慢闪/SCAN=快闪/CON=灭
 9. TMOS_SystemProcess()       BLE 事件处理
10. Discovery 超时             3s 强制完成
11. 输入报告                   解析 Stadia → 构建 SInput → USB 发送
12. 震动发送                   GATT Write + 指数退避 10ms→500ms
13. 电池轮询                   每 60s GATT_ReadCharValue
```

## 延时预算

| 阶段 | 最大耗时 | 说明 |
|------|---------|------|
| BLE 通知 → 主循环 | ~5ms | TMOS 事件调度 |
| Stadia 解析 | <0.1ms | 纯位运算 |
| SInput 构建 | <0.1ms | 纯位运算+移位 |
| USB IN 发送（EP3 64 字节） | <1ms | 等待主机轮询 |
| **端到端** | **~6-7ms** | 远低于游戏手柄 15ms 要求 |

## 反方向：震动

USB HID OUT 报告由中断服务程序缓存到 `g_hid_out_data`，主循环中处理：

```
主机 → USB OUT (EP3)
     ↓
ISR 缓存数据到 g_hid_out_data,
    设置 g_hid_out_pending = true
     ↓
主循环: SInputUSB_ProcessOutput() 触发回调
     ↓
on_hid_output() 回调:
  ├─ 命令 0x03,0x02 (FEATURES) → 设 g_feature_req_pending
  └─ sinput_parse_haptic(data, len, &left, &right)
       → 缓存振幅到 g_rumble_left/right
       → 设 g_rumble_pending = true
     ↓
主循环检测到 g_rumble_pending:
  if (now >= s_rumble_retry_at):
    stadia_build_rumble_report(left, right, rumble)  // 4 字节
    StadiaBLE_SendOutput(rumble, 4)                  // GATT Write Command (WriteNoRsp)
     ├─ 成功 → 清 pending, 重置退避
     └─ 失败 → 指数退避: 10ms → 20ms → 40ms → ... → 500ms cap
             新数据到达时立即重置退避
     ↓
BLE 栈发送到 Stadia 手柄输出报告特征句柄
```

GATT 写入**始终使用 Write Command** (`GATT_WriteNoRsp`，`req.cmd=TRUE`)，无视特征属性标志。

> 原因: Stadia 手柄输出报告特征只声明 WRITE（无 WRITE_NO_RSP 标志），但 Write Request 要求回执，
> 回执一旦丢失，WCH 栈 ATT 过程标志会永久阻塞，后续所有 GATT 操作返回 bleTimeout，震动永久失效。
> Write Command 无回执、不阻塞 ATT，实测 Stadia 手柄接受 Write Command 写入。

写入数据不含 Report ID 前缀（特征句柄已标识报告类型）。

震动振幅转换公式：`Stadia 值 = SInput 值 × 257`（0→0, 255→65535）。

## 反方向：Feature 响应

当 SDL（Steam 输入层）通过 USB OUT 发送 `[0x03, 0x02]` 请求设备能力时：

```
SDL → USB OUT [0x03, 0x02]
     ↓
on_hid_output() 设 g_feature_req_pending = true
     ↓
主循环检测 → platform_get_unique_id(uid)
     → sinput_build_feature_response(uid, resp)  // 63 字节
     → 封装 report[0]=0x02 + resp
     → SInputUSB_SendReport(report, 64)
     ↓
SDL 收到 Report ID 0x02 功能响应，完成握手
```
