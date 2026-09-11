# 开发指南

## 项目结构

```
CH592F/
├── src/
│   ├── main.c               # 入口 + 主循环 + 全局状态
│   ├── log.c                # CDC 分级日志模块
│   ├── log.h
│   ├── stadia_ble.c         # BLE Central（扫描/连接/Stadia 协议）
│   ├── stadia_ble.h
│   ├── sinput_usb.c         # USB 复合设备（CDC + HID SInput）
│   ├── sinput_usb.h
│   ├── stadia_protocol.c    # Stadia 报文解析（纯算法）
│   ├── stadia_protocol.h
│   ├── sinput_builder.c     # SInput 报文构建（纯算法）
│   ├── sinput_builder.h
│   ├── button.c             # PB22 按钮检测
│   ├── button.h
│   ├── platform.c           # 平台抽象层
│   └── platform.h
├── Makefile                 # 编译 + 烧录
├── firmware/                # 输出固件
├── sdk/                     # CH592 EVT SDK
├── tools/                   # 辅助脚本
│   └── serial_monitor.py    # CDC 串口监视器
└── docs/                    # 文档
```

## 模块依赖关系

```
main.c
  ├── log.c/h           → _write() → CDC 环形缓冲 → USB EP2 IN
  ├── stadia_ble.c/h    → WCH BLE 栈 (sdk/EVT/EXAM/BLE/LIB/)
  ├── sinput_usb.c/h    → WCH USB 寄存器操作
  ├── stadia_protocol.c/h → 无外部依赖（纯算法）
  ├── sinput_builder.c/h  → 无外部依赖（纯算法）
  ├── button.c/h        → GPIO 寄存器
  └── platform.c/h      → 芯片寄存器
```

日志管线: `printf`  `_write()`  `CDC_PutChar`  1024 字节环形缓冲  `CDC_Flush()`  USB EP2 IN。

## 键模块说明

### BLE Central (stadia_ble.c)

参考 WCH EVT Central 例程:

- `sdk/CH592EVT/EVT/EXAM/BLE/Central/APP/central.c`  (CH592F)
- `sdk/CH583EVT/EVT/EXAM/BLE/Central/APP/central.c`  (CH582F)
- SDK 路径由 Makefile 中 `CHIP` 变量自动选择

关键实现细节:

1. **设备匹配**: 按广播名前缀匹配 "Stadia"（`memcmp(name, "Stadia", 6)`），不依赖 MAC 地址
2. **服务发现**: 使用 `GATT_DiscPrimaryServiceByUUID()` 按 HID 服务 UUID (0x1812) 定向发现，然后在 HID 服务范围内调用 `GATT_DiscAllChars()` 发现所有特征，通过 properties 字节区分输入报告（0x10 = NOTIFY）和输出报告（0x0C = WRITE|WRITE_NO_RSP），这两个特征共享同一个 UUID (0x2A4D)
3. **通知接收**: 使能 CCCD 后接收 HID 输入报告通知
4. **震动写入**: **始终使用 `GATT_WriteNoRsp` (Write Command, `req.cmd=TRUE`, 无回执, 永不阻塞)，无视特征属性标志**。虽然特征可能仅声明 WRITE(0x08)，Write Command 仍可写入。
   Write Request 要求回执，回执一旦丢失 WCH 栈 ATT 过程标志会永久阻塞，导致震动永久失效，因此不用。
   写入数据不包含 Report ID 前缀（特征句柄已标识报告类型）。
5. **写回执处理**: 仅在 `s_disc_state == DISC_IDLE` 时非拦截式日志，不再提前 return，让消息走默认释放路径
6. **内存配置**: `BLE_MEMHEAP_SIZE=8192`（约 8KB，CH592F 占 30%，CH582F 占 25%）

### USB 复合设备 (sinput_usb.c)

参考 WCH EVT USB 设备例程:

- `sdk/EVT/EXAM/USB/Device/HID_CompliantDev/src/Main.c`  HID 设备参考
- `sdk/EVT/EXAM/USB/Device/COM/src/Main.c`  CDC 设备参考
- `sdk/EVT/EXAM/SRC/StdPeriphDriver/CH59x_usbdev.c`  USB 设备驱动

端点分配:

| 端点 | 类型 | 方向             | 用途                        |
| ---- | ---- | ---------------- | --------------------------- |
| EP1  | 中断 | IN               | CDC 通知                    |
| EP2  | 批量 | IN/OUT           | CDC 数据（日志输出 + 控制） |
| EP3  | 中断 | IN/OUT (64 字节) | HID SInput 报告             |

关键注意:

1. 使用 CH592 原生 USB 寄存器操作（非 TinyUSB）
2. 复合设备必须使用 IAD（Interface Association Descriptor），`bDeviceClass=0xEF`, `bDeviceSubClass=0x02`, `bDeviceProtocol=0x01`
3. HID 报告描述符有三个 Report ID: 0x01（输入报告）、0x02（功能响应）、0x03（输出/震动报告）
4. 所有 DMA 缓冲区必须 4 字节对齐（`__attribute__((aligned(4)))`）
5. HID OUT 在 ISR 中仅拷贝数据并设标志，主线循环中通过 `SInputUSB_ProcessOutput()` 触发回调处理

### 按钮 (button.c)

PB22 BOOT 按钮的简洁逻辑:

```c
if (BOOT 按下(消抖后)) {
    switch (当前状态) {
        case IDLE:      开始扫描;        break;
        case SCANNING:  取消扫描;        break;
        case CONNECTED: 断开并开始扫描;  break;
    }
}
```

软件消抖: SysTick 轮询，20ms 间隔，检测高到低边缘触发事件。

## 主循环结构 (main.c)

```
while (1):
  1. update_tick()              刷新 SysTick 毫秒计数器（含 32-bit 绕回补偿）
  2. platform_wdog_feed()       喂狗（防 BLE 死锁）
  3. CDC_Flush()                刷新 CDC 发送缓冲
  4. SInputUSB_ProcessOutput()  处理缓存的 USB OUT 报告
  5. Button_Task()              按钮轮询
  6. Button_GetEvent            BLE 扫描控制（仅 g_ble_ready 时生效）
  7. 功能请求待处理             SInput 功能响应
  8. LED 状态更新               INIT=常亮/IDLE=慢闪/SCANNING=快闪/CONNECTED=灭
  9. TMOS_SystemProcess()       BLE 事件处理
  10. Discovery 超时检测         3s 超时强制完成
  11. 有新的 HID 报告           解析→构建→USB 发送
  12. 震动待发送                BLE 震动写入（带指数退避 10ms→500ms）
  13. 周期电池读取              每 60s GATT_ReadCharValue
```

## 调试技巧

1. **CDC 日志**: 用 USB 数据线连接电脑，在设备管理器中找到 "Stick (SInput)" 虚拟串口的 COM 端口号，然后运行:

   ```bash
   python tools/serial_monitor.py 
   ```

   日志格式: `[T+秒.毫秒] [级别] [模块] 消息`
   日志恒启用，无需 `make DEBUG=1`。默认级别为 INFO（每帧 GATT/HID/Rx 日志属 DEBUG 级，默认被过滤）；
   需要逐帧日志时，把 `src/log.h` 中 `LOG_LEVEL` 宏改为 `LOG_LEVEL_DEBUG` 重新编译。
2. **LED 指示**: PA8 蓝色 LED 显示当前状态:

   - 初始化中（`!g_ble_ready`）: 蓝色常亮
   - IDLE（空闲）: 慢闪（500ms 间隔）
   - SCANNING（扫描）: 快闪（100ms 间隔）
   - CONNECTED（已连接）: 熄灭
3. **USB 枚举检查**: 设备管理器中确认:

   - "端口 (COM 和 LPT)" 下出现 "Stick (SInput)" 虚拟串口
   - "人体学输入设备" 下出现 "Stick (SInput)" HID 设备
4. **烧录失败**: 确保正确进入引导加载程序模式，按住 BOOT 上电插入电脑，通电后松开，然后在 WCHISPTool 中确认芯片被识别。

## 常见问题

**Q: 编译报错找不到头文件**
A: 检查 SDK 是否正确下载到 `sdk/` 目录，关键文件路径是否匹配安装说明中的验证列表。

**Q: BLE 扫描不到 Stadia**
A: 确保 Stadia 手柄处于配对模式（指示灯闪烁）。确保按了 BOOT 按钮启动扫描。

**Q: USB 枚举失败**
A: 检查 D+ 上拉电阻是否启用 (`R16_PIN_ANALOG_IE |= RB_PIN_USB_DP_PU`)。检查端点 DMA 缓冲区是否 4 字节对齐。

**Q: 烧录后没反应**
A: 确认进入引导加载程序的操作时序: 按住 BOOT → 插入 USB → 松 BOOT。如果设备管理器中出现 "WCH USB Module" 则说明已进入 ISP 模式。

**Q: 日志输出乱码或没有日志**
A: 确认串口监视器的 COM 口选择正确（"Stick (SInput)" 虚拟串口，非硬件 UART）。CDC 串口波特率自动匹配，无需手动设置。
