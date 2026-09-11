# Stick — CH582F / CH592F Stadia USB Dongle 项目知识库

**项目状态**: 代码完成，BLE 连接、HID 输入、震动、电池、Steam 识别均正常
**芯片**: CH582F (QFN28, RISC-V4A @60MHz, 32KB SRAM, 448KB Flash) / CH592F (RISC-V4C @60MHz, 26KB SRAM, 512KB Flash)
**协议**: BLE Central (Stadia) → SInput USB HID (PC)
**调试**: CDC 虚拟串口（USB COM 口），无硬件 UART；日志默认 INFO，调试时改 LOG_LEVEL 宏
**烧录**: WCHISPTool GUI（按住 BOOT → 插 USB → 松 BOOT）

---

## 架构

直通管道：BLE 通知 → 解析 Stadia 报文 → 构建 SInput 报告 → USB 发送。无中间路由层。

```
Stadia手柄 ──BLE──→ CH582F/CH592F ──USB CDC+HID──→ PC (Steam/SDL)
```

---

## 目录结构

```
src/
├── main.c             # 入口、主循环、全局状态、SysTick 轮询
├── stadia_ble.c/h     # BLE Central：扫描/连接/服务发现/通知/震动写入
├── sinput_usb.c/h     # USB 复合设备：CDC 日志 + HID SInput 手柄
├── stadia_protocol.c/h# Stadia 10 字节报文解析 + 震动命令构建（纯算法）
├── sinput_builder.c/h # SInput 64 字节报告构建 + 功能响应 + 震动解析（纯算法）
├── button.c/h         # PB22 BOOT 按键消抖 + 边缘事件
├── platform.c/h       # 平台抽象：LED、SysTick、UID、复位、延时
└── log.c/h            # CDC 分级日志
tools/
└── serial_monitor.py  # CDC 串口监视器（彩色日志）
```

---

## 模块职责

| 模块 | 文件 | 硬件依赖 | 一句话 |
|------|------|----------|--------|
| 主循环 | `main.c` | — | 初始化 + 流水线调度 + 状态机 |
| BLE Central | `stadia_ble.c` | WCH BLE 栈 | 扫描 Stadia → 连接 → 发现服务 → 收通知 → 写震动 |
| USB 复合设备 | `sinput_usb.c` | WCH USB 控制器 | CDC (日志) + HID SInput (64 字节报告) |
| Stadia 协议 | `stadia_protocol.c` | — | 解析 10 字节输入报告，构建 4 字节震动命令 |
| SInput 构建 | `sinput_builder.c` | — | 构建 64 字节报告、功能响应(0x02)、解析震动命令 |
| 按钮 | `button.c` | GPIO | PB22 20ms 消抖 + 按下边缘事件 |
| 平台抽象 | `platform.c` | 芯片寄存器 | LED(PA8)、SysTick 时间戳、芯片 UID、复位、延时 |
| CDC 日志 | `log.c` | USB 控制器 | printf → 环形缓冲 → EP2 IN，恒启用，分级输出 |

---

## 状态机

### BLE 连接

```
IDLE ──按BOOT──→ SCANNING ──发现Stadia──→ CONNECTING ──成功──→ CONNECTED
 ↑                    ↑                         │                     │
 │                    └──按BOOT─────────────────→│                     │
 └──────────────── 断开/失败 ──────────────────────────────────────────┘
```

### 服务发现

```
DISC_SVC ──→ DISC_CHAR_ALL ──→ DISC_CCCD ──→ DISC_BAT_SVC ──→ DISC_BAT_CHAR ──→ DISC_BAT_CCCD ──→ DISC_IDLE
                                                ↑ 电池不存在或超时(3s) → DISC_IDLE
```

### LED 指示（PA8）

| 状态 | LED | 条件 |
|------|-----|------|
| 初始化中 | 蓝色常亮 | `!g_ble_ready` |
| IDLE | 慢闪 500ms | 已就绪未连接 |
| SCANNING | 快闪 100ms | 正在扫描 |
| CONNECTED | 熄灭 | 已连接 |

---

## 主循环

```
1. update_tick()             SysTick 毫秒计数（含 32-bit 绕回补偿）
2. platform_wdog_feed()      喂狗（防 BLE 死锁）
3. CDC_Flush()               刷新 CDC 发送缓冲
4. SInputUSB_ProcessOutput() 缓存 USB OUT 报告 → 回调
5. Button_Task()             按键轮询 + 边缘事件
6. Feature 响应              状态 → 构建 → USB 发送
7. LED 更新                  常亮/慢闪/快闪/灭
8. TMOS_SystemProcess()      BLE 事件处理
9. Discovery 超时            3s 强制完成
10. 输入报告                  解析 Stadia → 构建 SInput → USB 发送
11. 震动写入                 GATT Write，带指数退避(10ms→500ms)
12. 电池轮询                 每 60s GATT_ReadCharValue
```

---

## 关键约定

**编码**: C99 (`-std=gnu99`), `snake_case` 命名, 全局 `g_` 前缀, 内部 static `s_` 前缀

**内存**: 无 malloc, DMA 缓冲区 `__attribute__((aligned(4)))`, CDC 环形缓冲 1024 字节

**中断**: USB 中断用 `__INTERRUPT` + `__HIGH_CODE`, ISR 只设标志不做耗时操作

**SysTick**: 禁用中断, 主循环轮询 `SysTick->CNT`, `TICKS_PER_MS=60000`

---

## ANTI-PATTERNS（本项目禁止）

| 模式 | 原因 |
|------|------|
| `tusb.h` 包含 | WCH 芯片不用 TinyUSB |
| BTstack 头文件 | 用 WCH 原生 BLE 栈 |
| `router`/`players`/`feedback` | 放不进 SRAM |
| 多协议切换 / 多手柄 | 仅 SInput，最多 1 个 Stadia |
| malloc / 动态分配 | 嵌入式禁区 |
| 超长 ISR / ISR 中轮询 | 中断应快速返回 |
| 魔数硬编码 | 常量用宏定义 |

---

## RISKS / GOTCHAS

**BLE**
- `BLE_MEMHEAP_SIZE=8192` 占 ~8KB（CH592F 约 30%，CH582F 约 25%），SRAM 较紧张
- 输入/输出报告同 UUID (0x2A4D)，需 `GATT_DiscAllChars` 按 properties 区分
- HID 输出报告写入：始终使用 `GATT_WriteNoRsp` (Write Command)，无回执不阻塞 ATT
- 如果 Write Command 持续失败（s_rumble_timeout_cnt ≥ 50），自动断连，回到 IDLE 等待手动 BOOT 重扫

**电池**
- Stadia 电池特征 `props=0x02` (仅 READ)，无 CCCD，不支持通知
- 发现时读一次，之后主循环每 60s `GATT_ReadCharValue` 轮询

**SysTick**
- 32-bit 计数器每 ~71.6s 绕回，`update_tick()` 需做绕回补偿
- `CH59x_BLEInit()` / `CH58X_BLEInit()` 内部调用 `SysTick_Config()` 重置计数器，调用后需重读基准

**USB**
- DMA 缓冲区必须 4 字节对齐
- CDC 复合设备必须 IAD (`bDeviceClass=0xEF, bDeviceSubClass=0x02, bDeviceProtocol=0x01`)
- EP3 IN 防覆写：检查 TX 状态 ACK 跳过，保护 Feature Response
- CDC 日志发送受 DTR 控制：`cdc_host_open` 随主机 SET_CONTROL_LINE_STATE (DTR) 复位，串口监视器断开即停发，重开自动恢复

**其他**
- `INT_SOFT` 保持未定义（=0，硬件堆栈模式），定义会导致栈损坏
- PB22 双重用途：启动时进引导加载程序，启动后作 GPIO
- 烧录工具：WCHISPTool，`make isp` 打开；`ISP_TOOL` 变量可覆盖路径

**看门狗 (WWDG)**
- WWDG：溢出 ~437ms (60MHz/131072≈457.8Hz, 计数器 200)，主循环每轮喂狗，防 BLE 死锁（曾致震动失效手动复位）
- `platform_wdog_init()` 在 BLE 初始化完成后调用，避开 BLE 慢启动窗口

---

## 构建与烧录

```bash
make                    # 编译 CH582F → firmware/stick.bin
make CHIP=CH592         # 编译 CH592F
make clean              # 清理
make isp                # 打开 WCHISPTool（按住BOOT→插USB→松BOOT后下载）
make test               # host 单元测试（需 gcc；无 gcc 时仅语法检查）
python tools/serial_monitor.py COMx   # 查看日志（默认彩色）
```

详细环境搭建见 `docs/getting-started/installation.md`，编译说明见 `docs/getting-started/building.md`。

---

## 参考资料

| 协议 | 公开来源 |
|------|----------|
| Stadia HID 输入报告 | DJm00n/ControllersInfo, Chromium gamepad_standard_mappings, SDL Stadia driver |
| SInput 协议 | https://github.com/HandHeldLegend/SInput-HID |
| SInput SDL 驱动 | SDL_hidapi_sinput.c (SDL 官方仓库) |
