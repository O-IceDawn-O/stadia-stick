# 编译与烧录

## Makefile 目标

```bash
make               # 编译 CH582F，生成 firmware/stick.bin
make CHIP=CH592    # 编译 CH592F
make clean         # 清理编译产物
make isp           # 打开 WCHISPTool 烧录工具
make test          # host 单元测试（需 gcc；无 gcc 时仅语法检查）
```

## 编译输出

编译成功后:

```
firmware/
└── stick.bin          # 烧录固件（目标文件）
```

## 烧录流程

CH582F / CH592F 均使用沁恒官方 **WCHISPTool** 烧录。

### 进入引导加载程序模式

使用 USB 引导加载程序烧录，操作步骤:

1. **按住 BOOT 按钮不放**
2. **上电插入电脑**
3. **看烧录工具是否识别**

### 烧录固件

在 WCHISPTool 中:

1. 确认已识别到目标芯片（CH582F 显示 CH583/CH582 系列，CH592F 显示 CH592）
2. 点击"目标程序文件" 选择 `firmware\stick.bin`
3. 点击"下载"按钮
4. 等待烧录完成（进度条走完，提示成功）

### 验证烧录

按 RESET 复位运行新固件，然后在设备管理器中确认:

1. "端口 (COM 和 LPT)" 下出现 **"Stick (SInput)"** 虚拟串口
2. "人体学输入设备 (Human Interface Devices)" 下出现 **"Stick (SInput)"** HID 游戏手柄

## 调试输出

本项目不使用硬件 UART 调试。所有日志通过 CDC 虚拟串口输出（USB 同一根数据线）。

日志系统设计:

- `printf()` 通过 `_write()` 重定向到 CDC 环形缓冲
- 日志**恒启用**，无 `#ifdef DEBUG` 条件编译
- 日志格式: `[T+秒.毫秒] [级别] [模块] 消息`
- 示例: `[T+12345] [INF] [BLE] Connected! handle=0`

### 查看日志

```bash
# COMx 是 CDC 虚拟串口的端口号（在设备管理器中查看）
python tools/serial_monitor.py 

# 示例:
python tools/serial_monitor.py 
# 输出:
# [T+12.345] [INF] [BLE] Scanning for Stadia...
# [T+56.789] [INF] [BLE] Connected!
# [T+57.012] [INF] [BAT] Battery: 75%
```

### 日志级别

日志级别由 `LOG_LEVEL` 宏控制（默认 `LOG_LEVEL_INFO`，输出 INFO 及以上）:

| 宏                  | 值 | 说明             |
| ------------------- | -- | ---------------- |
| `LOG_LEVEL_NONE`  | 0  | 不输出           |
| `LOG_LEVEL_ERROR` | 1  | 仅错误           |
| `LOG_LEVEL_WARN`  | 2  | 警告及以上       |
| `LOG_LEVEL_INFO`  | 3  | 信息及以上（默认） |
| `LOG_LEVEL_DEBUG` | 4  | 全部输出         |
