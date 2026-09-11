# CLAUDE.md — Stick (CH582F / CH592F Stadia USB Dongle)

## 项目身份

CH582F / CH592F RISC-V 固件：把 Stadia 手柄的 BLE 输入转成 USB SInput 游戏手柄。
代码已完成，功能全通。改代码前先读 AGENTS.md。
编译: `make` (CH582F 默认), `make CHIP=CH592` (CH592F), `make test` (host 单元测试, 需 gcc; 无 gcc 时仅语法检查)。

## 编码红线

- C99, `-std=gnu99`, `snake_case`, 全局 `g_` 前缀, 内部 static `s_` 前缀
- DMA 缓冲区 `__attribute__((aligned(4)))`，协议结构体 `__attribute__((packed))`
- 中断↔主循环共享的标志用 `volatile`
- 所有日志通过 CDC 虚拟串口输出（恒启用，默认 INFO 级别，调试时改 LOG_LEVEL 宏）
- 常量用宏定义，魔数加注释
- Makefile 中 `INT_SOFT` 保持未定义（硬件堆栈模式）
- 使用 WCHISPTool 烧录，非 wchisp
- 绝不包含 `tusb.h` 或 BTstack 头文件
- 绝不使用 malloc 或动态分配
- 绝不在中断中做耗时操作或调 BLE API
- 绝不复制 router/players/feedback 层代码
- 看门狗 WWDG：主循环每轮 `platform_wdog_feed()` 喂狗，防 BLE 死锁；`platform_wdog_init()` 在 BLE 初始化完成后调用

## 工作流程

1. 读 AGENTS.md + .h 文件理解接口
2. 改前 `make` 确认编译通过
3. 改后 `make clean && make` 确认 0 警告
4. 改 .h 接口要同步更新 AGENTS.md 的模块描述
5. 复杂改动先写 brief plan

## 文件所有权

| 文件 | 谁负责 | 备注 |
|------|--------|------|
| `main.c` | 集成 | 全局状态、主循环、状态机 |
| `stadia_ble.c/h` | BLE | 只用 WCH BLE API |
| `sinput_usb.c/h` | USB | 只用 WCH USB 寄存器操作 |
| `stadia_protocol.c/h` | 提取 | 纯算法，无硬件依赖 |
| `sinput_builder.c/h` | 提取 | 纯算法，无硬件依赖 |
| `button.c/h` | 按钮 | PB22 软件消抖 |
| `platform.c/h` | 平台 | 封装 CH582F/CH592F 硬件（条件编译） |
| `log.c/h` | 日志 | CDC 分级日志 |
| `AGENTS.md` | 文档 | 修改接口时同步更新 |
| `Makefile` | 构建 | 工具链 riscv-none-embed-gcc |
