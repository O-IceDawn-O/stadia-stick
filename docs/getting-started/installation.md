# 环境搭建

## 依赖清单

| # | 依赖              | 版本   | 用途                  | 安装方式                            |
| - | ----------------- | ------ | --------------------- | ----------------------------------- |
| 1 | RISC-V GCC 工具链 | 8.2.0+ | RISC-V 交叉编译器     | 安装 MounRiver Studio（捆绑 xPack） |
| 2 | CH592 EVT / CH583 EVT SDK | >=v1.4 | BLE 协议栈 + USB 例程 | 官网下载 ZIP            |
| 3 | GNU Make          | >=4.0  | 构建系统              | chocolatey                          |
| 4 | WCHISPTool        | 最新   | 官方 USB 烧录工具     | 沁恒官网安装包                      |
| 5 | Git               | 任意   | 版本管理              | 通常已装                            |

---

## 1. 安装 RISC-V GCC 工具链

Makefile 中 `PREFIX = riscv-none-embed-`，需要对应的 RISC-V GCC 工具链。

### 方式 A（推荐）: 安装 MounRiver Studio

MounRiver Studio 捆绑了 `riscv-none-embed-gcc 8.2.0 (xPack)`，这是经过 WCH 验证的工具链。

1. 下载 [MounRiver Studio](http://www.mounriver.com/download)
2. 安装（默认路径含 `MounRiver_Studio2`）
3. 工具链位于: `MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin\`
4. 将该路径添加到系统 PATH 环境变量

验证:

```powershell
riscv-none-embed-gcc --version
# 输出: riscv-none-embed-gcc (xPack GNU RISC-V Embedded GCC) 8.2.0
```

### 方式 B: 使用其他 `riscv-none-embed-` 工具链

只要命令前缀是 `riscv-none-embed-` 且支持 `-march=rv32imac` 的工具链均可。

> **注意**: xPack 新版工具链使用 `riscv-none-elf-` 前缀，与 WCH 预编译库不兼容，请勿用于本项目。

---

## 2. 下载 SDK

本项目支持 CH582F 和 CH592F，分别需要对应的 SDK：

### CH582F：CH583 EVT SDK

**WCH 官网**:

```
https://www.wch.cn/downloads/CH583EVT_ZIP.html
```

解压到 `sdk\CH583EVT\`，最终路径: `<PROJECT_ROOT>\sdk\CH583EVT\EVT\EXAM\...`

### CH592F：CH592 EVT SDK

**WCH 官网**:

```
https://www.wch.cn/downloads/CH592EVT_ZIP.html
```

解压到 `sdk\CH592EVT\`，最终路径: `<PROJECT_ROOT>\sdk\CH592EVT\EVT\EXAM\...`

**GitHub 镜像**:

```
https://github.com/openwch/ch592
```

### 解压后目录

SDK 目录结构如下（以 CH592 为例，CH583 类似）:

```
sdk\CH592EVT\EVT\EXAM\
├── BLE\                    ← BLE 协议栈
│   ├── Central\            ← BLE Central 例程（主要参考）
│   ├── HAL\                ← HAL 层 + TMOS 调度器
│   │   ├── include\        ← HAL 头文件（CONFIG.h 等）
│   │   ├── MCU.c           ← BLE 初始化
│   │   └── SLEEP.c
│   └── LIB\                ← BLE 预编译库
│       ├── CH59xBLE_LIB.h  ← BLE API 头文件
│       └── libCH59xBLE.a   ← 预编译 BLE 库（链接用）
├── SRC\                    ← 芯片驱动
│   ├── RVMSIS\             ← 芯片头文件
│   ├── StdPeriphDriver\    ← 外设驱动源码（CH5x8_*.c/h）
│   └── Startup\            ← 启动代码
├── USB\Device\             ← USB 设备例程
└── Ld\                     ← 链接脚本（Link.ld）
```

> **注意**: 两个 SDK 分别存放在 `sdk/CH592EVT/` 和 `sdk/CH583EVT/` 目录下，
> Makefile 会根据 `CHIP` 变量自动选择对应的 SDK 路径。

---

## 3. 安装 GNU Make

### 通过 chocolatey 安装（推荐）

先装 chocolatey（如果还没有）:

```powershell
# 管理员 PowerShell 执行:
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
```

然后装 make:

```powershell
choco install make -y
```

### 验证

```powershell
make --version
# 输出: GNU Make x.x.x
```

---

## 4. 安装 WCHISPTool

WCHISPTool 是沁恒官方 USB 烧录工具，用于将固件烧录到 CH592F。

从沁恒官网下载安装包并安装:

```
https://www.wch.cn/downloads/WCHISPTool_Setup_exe.html
```

安装后路径取决于安装时选择的目录，常见位置: `C:\WCH\WCHISPTool\WCHISPTool_CH57x-59x\WCHISPTool_CH57x-59x.exe`
可在 Makefile 中通过 `ISP_TOOL` 变量指定路径: `make isp ISP_TOOL="D:/path/to/WCHISPTool.exe"`

验证方式: 打开软件，确认能识别到 CH592 芯片即可。

---

## 5. 最终验证

全部通过表示环境就绪:

```powershell
# 1. GCC 工具链
riscv-none-embed-gcc --version
# OK: riscv-none-embed-gcc (xPack GNU RISC-V Embedded GCC) 8.2.0

# 2. Make
make --version
# OK: GNU Make x.x.x

# 3. SDK 关键文件（两个 SDK 至少有一个）
(Test-Path "sdk\CH592EVT\EVT\EXAM\BLE\LIB\libCH59xBLE.a") -or `
(Test-Path "sdk\CH583EVT\EVT\EXAM\BLE\LIB\libCH58xBLE.a")
# OK: True
```

---

## 常见问题

### Q: Make 报错 "不是内部或外部命令"

PATH 没生效。重启 PowerShell 或手动指定:

```powershell
& "C:\ProgramData\chocolatey\bin\make.exe" --version  # 替换为你的 make.exe 实际路径
```
