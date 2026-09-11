########################################
# Stick — CH592F / CH582F Stadia USB Dongle
# USB 复合设备 (CDC + HID) + BLE Central
# 用法:
#   make              — 编译 CH582F (默认)
#   make CHIP=CH592   — 编译 CH592F
#   make clean        — 清理
#   make isp          — 打开 WCHISPTool
########################################

TARGET = stick

PREFIX = riscv-none-embed-
CC   = $(PREFIX)gcc
AS   = $(PREFIX)gcc -x assembler-with-cpp
OBJCOPY = $(PREFIX)objcopy
SIZE   = $(PREFIX)size
OBJDUMP = $(PREFIX)objdump

BUILD_DIR = build
FW_DIR    = firmware

# ---- 芯片选择: CH582 (默认) 或 CH592 ----
CHIP ?= CH582

ifeq ($(CHIP),CH582)
  SDK_BASE      = sdk/CH583EVT/EVT/EXAM
  SDK_LIB       = $(SDK_BASE)/SRC/StdPeriphDriver/libISP583.a
  BLE_LIB       = $(SDK_BASE)/BLE/LIB/libCH58xBLE.a
  STARTUP_S     = $(SDK_BASE)/SRC/Startup/startup_CH583.S
  OBJ_STARTUP   = startup_CH583.o

  # CH58x 外设驱动源文件
  SDK_C = \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH58x_clk.c \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH58x_gpio.c \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH58x_sys.c \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH58x_pwr.c \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH58x_usbdev.c \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH58x_adc.c \
    $(SDK_BASE)/BLE/HAL/MCU.c \
    $(SDK_BASE)/BLE/HAL/RTC.c

  CFLAGS_EXTRA  = -DISP582 -DCH58xBLE -DCFG_CH582
else
  SDK_BASE      = sdk/CH592EVT/EVT/EXAM
  SDK_LIB       = $(SDK_BASE)/SRC/StdPeriphDriver/libISP592.a
  BLE_LIB       = $(SDK_BASE)/BLE/LIB/libCH59xBLE.a
  STARTUP_S     = $(SDK_BASE)/SRC/Startup/startup_CH592.S
  OBJ_STARTUP   = startup_CH592.o

  # CH59x 外设驱动源文件
  SDK_C = \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH59x_clk.c \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH59x_gpio.c \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH59x_sys.c \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH59x_pwr.c \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH59x_usbdev.c \
    $(SDK_BASE)/SRC/StdPeriphDriver/CH59x_adc.c \
    $(SDK_BASE)/BLE/HAL/MCU.c \
    $(SDK_BASE)/BLE/HAL/RTC.c

  CFLAGS_EXTRA  = -DISP592 -DCH59xBLE -DCFG_CH592
endif

SDK_CORE_C    = $(SDK_BASE)/SRC/RVMSIS/core_riscv.c
LINKER_SCRIPT = $(SDK_BASE)/SRC/Ld/Link.ld

# CH58x BLE 库内置调度器；CH59x 需要单独汇编文件
ifeq ($(CHIP),CH592)
BLE_TASK_S    = $(SDK_BASE)/BLE/LIB/ble_task_scheduler.S
else
BLE_TASK_S    =
endif

CPU = -march=rv32imac -mabi=ilp32 -mcmodel=medany -msmall-data-limit=8 -mno-save-restore
OPT = -Os -ffunction-sections -fdata-sections

INCLUDES = \
	-I$(SDK_BASE)/SRC/RVMSIS \
	-I$(SDK_BASE)/SRC/StdPeriphDriver/inc \
	-I$(SDK_BASE)/BLE/HAL/include \
	-I$(SDK_BASE)/BLE/LIB \
	-Isrc

CFLAGS = $(CPU) $(OPT) $(INCLUDES) -Wall -Wextra -std=gnu99
CFLAGS += -Wno-unused-parameter -Wno-unused-function -Wno-attributes
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"
# INT_SOFT 保持未定义 = 0 (硬件堆栈模式, 与 startup 的 csrw 0x804,3 一致)
# 若定义 INT_SOFT=1 会使用软件中断属性, 与硬件模式冲突导致栈损坏
# BLE 内存/缓冲参数
CFLAGS += -DBLE_MEMHEAP_SIZE=8192
CFLAGS += -DBLE_BUFF_NUM=8
CFLAGS += -DBLE_TX_NUM_EVENT=3
# 芯片相关宏
CFLAGS += $(CFLAGS_EXTRA)
CFLAGS += -g -gdwarf-2

ASFLAGS = $(CPU) $(OPT) $(INCLUDES) -Wall

LDFLAGS = $(CPU) -nostartfiles -T $(LINKER_SCRIPT)
LDFLAGS += -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(TARGET).map
LDFLAGS += --specs=nano.specs

LIBS = -lc -lm -lnosys
LIBS_TO_LINK = $(SDK_LIB) $(BLE_LIB) $(LIBS)

# ---- 应用源文件 (芯片无关) ----
SRC_C = \
	src/main.c \
	src/log.c \
	src/button.c \
	src/sinput_usb.c \
	src/sinput_builder.c \
	src/stadia_protocol.c \
	src/stadia_ble.c \
	src/platform.c

C_SRCS = $(SDK_C) $(SDK_CORE_C) $(SRC_C)
S_SRCS = $(STARTUP_S) $(BLE_TASK_S)

ALL_SRCS = $(C_SRCS)
OBJECTS = $(addprefix $(BUILD_DIR)/, $(notdir $(ALL_SRCS:.c=.o)))
OBJECTS += $(BUILD_DIR)/$(OBJ_STARTUP)
ifneq ($(BLE_TASK_S),)
  OBJECTS += $(BUILD_DIR)/ble_task_scheduler.o
endif
VPATH := $(dir $(C_SRCS)) $(dir $(S_SRCS)) $(dir $(BLE_TASK_S)) src

# ---- 目标 ----
.PHONY: all clean isp dirs info test

all: dirs $(FW_DIR)/$(TARGET).bin

dirs:
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"
	@if not exist "$(FW_DIR)" mkdir "$(FW_DIR)"

$(BUILD_DIR)/%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(OBJ_STARTUP): $(STARTUP_S) Makefile
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.o: %.S Makefile
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS)
	$(CC) $(OBJECTS) $(LIBS_TO_LINK) $(LDFLAGS) -o $@
	$(SIZE) $@

$(FW_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	$(OBJCOPY) -O binary -S $< $@
	@echo "Build: $(FW_DIR)/$(TARGET).bin  [CHIP=$(CHIP)]"

# WCHISPTool 路径（按实际安装位置修改）
ISP_TOOL ?= C:/WCH/WCHISPTool/WCHISPTool_CH57x-59x/WCHISPTool_CH57x-59x.exe

isp: $(FW_DIR)/$(TARGET).bin
	@start "" "$(ISP_TOOL)"
	@echo "WCHISPTool 已打开（配置路径: $(ISP_TOOL)）"

TEST_SRC = tests/test_stadia_protocol.c tests/test_sinput_builder.c
TEST_BIN1 = $(BUILD_DIR)/run_tests_protocol.exe
TEST_BIN2 = $(BUILD_DIR)/run_tests_builder.exe
HOST_CC ?= gcc

# 探测 host gcc（无 gcc 时优雅跳过，不掩盖测试失败）
HOST_CC_OK := $(strip $(shell $(HOST_CC) --version >NUL 2>&1 && echo yes))

test: dirs
	@if "$(HOST_CC_OK)" == "yes" ($(HOST_CC) -Isrc -std=gnu99 -Wall -Wextra tests/test_stadia_protocol.c src/stadia_protocol.c -o $(TEST_BIN1)) else (echo host gcc not found, syntax-check only & $(CC) -fsyntax-only -Isrc $(TEST_SRC) src/stadia_protocol.c src/sinput_builder.c)
	@if "$(HOST_CC_OK)" == "yes" ($(HOST_CC) -Isrc -std=gnu99 -Wall -Wextra tests/test_sinput_builder.c src/sinput_builder.c -o $(TEST_BIN2))
	@if "$(HOST_CC_OK)" == "yes" ($(subst /,\,$(TEST_BIN1)) && $(subst /,\,$(TEST_BIN2)) && echo ALL TESTS PASSED)

info:
	@echo "CHIP=$(CHIP)"
	@echo "SDK=$(SDK_BASE)"
	@echo "CFLAGS_EXTRA=$(CFLAGS_EXTRA)"

clean:
	@if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
	@if exist "$(FW_DIR)" rmdir /s /q "$(FW_DIR)"
	@echo "Cleaned."

-include $(wildcard $(BUILD_DIR)/*.d)
