#!/usr/bin/env python3
"""
Stick — CH582F/CH592F CDC 串口日志监视器
自动识别端口，断开后等待重连，不丢失启动日志。
"""

import argparse
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("需要 pyserial: pip install pyserial")
    sys.exit(1)


def detect_stick_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if "2E8A" in p.hwid.upper() or "2e8a" in p.hwid.lower():
            return p.device
        if "USB Serial" in p.description:
            return p.device
    for p in ports:
        if "USB" in p.description:
            return p.device
    return None


def colorize(text):
    """日志上色规则: 模块标签优先于级别标签, 按出现顺序匹配."""
    # 模块特有颜色 (匹配模块标签 [XXX])
    if "[BAT]" in text:  return "\033[92m%s\033[0m" % text  # 亮绿 — 电池
    if "[HID]" in text:  return "\033[95m%s\033[0m" % text  # 亮紫 — HID 输入/震动
    if "[BTN]" in text:  return "\033[93m%s\033[0m" % text  # 黄 — 按钮事件
    if "[USB]" in text:  return "\033[94m%s\033[0m" % text  # 蓝 — USB 枚举
    if "[GATT]" in text: return "\033[90m%s\033[0m" % text  # 灰 — GATT 协议 (最详细)
    if "[MAIN]" in text: return "%s" % text                 # 无色 — 启动信息
    # 级别颜色 (fallback)
    if "[ERR]" in text:  return "\033[91m%s\033[0m" % text  # 红
    if "[WRN]" in text:  return "\033[93m%s\033[0m" % text  # 黄
    if "[INF]" in text:  return "\033[96m%s\033[0m" % text  # 青
    if "[DBG]" in text:  return "\033[90m%s\033[0m" % text  # 灰
    return text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", help="COM port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--no-color", action="store_true", help="Disable ANSI color (default: enabled)")
    args = parser.parse_args()

    while True:
        port = args.port
        if not port:
            port = detect_stick_port()
            if not port:
                print("\n检测 Stick 端口...", end=" ", flush=True)
                time.sleep(0.5)
                continue

        ser = None
        try:
            ser = serial.Serial(port, args.baud, timeout=0.1)
        except serial.SerialException:
            if not args.port:
                args.port = None  # 端口失效，下次循环重新扫描
            print(f"\n等待 {port} 重新连接...", end=" ", flush=True)
            time.sleep(1)
            continue

        print(f"已连接 {port} @ {args.baud}")
        print("Ctrl+C 退出\n")

        try:
            buf = b""
            while True:
                data = ser.read(256)
                if data:
                    buf += data
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        text = line.decode("utf-8", errors="replace").strip("\r")
                        if text:
                            print(colorize(text) if not args.no_color else text)
                else:
                    time.sleep(0.01)
        except serial.SerialException:
            print(f"\n{port} 已断开, 搜索可用端口...", end=" ", flush=True)
            if not args.port:
                port = None  # 下一次循环重新扫描
        except KeyboardInterrupt:
            print("\n退出")
            break
        finally:
            if ser:
                ser.close()

        time.sleep(0.5)


if __name__ == "__main__":
    main()
