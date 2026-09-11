# Stick — CH582F / CH592F Stadia USB 手柄适配器

**把 Google Stadia 手柄的 BLE 输入，通过 CH582F/CH592F（¥7 RISC-V BLE 芯片）转成有线 USB SInput 游戏手柄。**

---

## 快速开始

```bash
# 1. 编译（默认 CH582F，CH592F 加 CHIP=CH592）
make

# 2. 烧录（按住 BOOT 插入 USB 进入 USB ISP 模式）
#    运行 make isp 打开 WCHISPTool，选择 firmware/stick.bin → 下载

# 3. CDC 日志查看（通过 USB COM 口，无硬件 UART）
python tools/serial_monitor.py 
```

USB 枚举信息：VID=0x2E8A PID=0x10C6，产品名 "Stick (SInput)"。

## 文档目录

| 章节                                       | 内容                                 |
| ------------------------------------------ | ------------------------------------ |
| [架构概述](architecture/overview.md)        | 系统结构、模块划分、直通管道设计     |
| [数据流](architecture/data-flow.md)         | BLE → 解析 → 构建 → USB 完整管线  |
| [硬件说明](hardware/board.md)               | SuperMini CH592F 引脚分配、按钮位置  |
| [环境搭建](getting-started/installation.md) | SDK 下载、工具链安装、依赖配置       |
| [编译与烧录](getting-started/building.md)   | Makefile 目标、烧录流程、调试开关    |
| [Stadia 协议](protocol/stadia.md)           | BLE HID 报告格式、按键映射、震动命令 |
| [SInput 协议](protocol/sinput.md)           | USB HID 报告格式、输出命令           |
| [开发指南](development/guide.md)            | 模块说明、修改指引、调试技巧         |

## 项目状态

代码完成，功能验证通过：BLE 连接、HID 输入、震动反馈、Steam 识别均正常。

- 芯片: CH582F (RISC-V4A @60MHz, 32KB SRAM, 448KB Flash) / CH592F (RISC-V4C @60MHz, 26KB SRAM, 512KB Flash)
- 协议: Stadia BLE Central → SInput USB HID
- 调试: CDC 虚拟串口（USB COM 口）
- 辅助工具: serial_monitor.py（日志查看）
