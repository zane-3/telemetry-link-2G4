#!/usr/bin/env python3
"""
STM32F103 Application Firmware Flash Tool
使用 OpenOCD 烧录固件到 0x08004000 (Application 区域)
"""

import subprocess
import sys
import time
import os

OPENOCD = "openocd"
INTERFACE = "interface/stlink.cfg"
TARGET = "target/stm32f1x.cfg"
FIRMWARE_BIN = "build/application/application.bin"
FLASH_ADDRESS = "0x08004000"

def run_openocd_command(commands):
    """执行 OpenOCD 命令"""
    cmd = [
        OPENOCD,
        "-f", INTERFACE,
        "-f", TARGET
    ]

    for c in commands:
        cmd.extend(["-c", c])

    print(f"执行命令: {' '.join(cmd)}")

    try:
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=30
        )

        print(result.stdout)
        if result.stderr:
            print(result.stderr, file=sys.stderr)

        return result.returncode == 0
    except subprocess.TimeoutExpired:
        print("错误: 命令超时", file=sys.stderr)
        return False
    except Exception as e:
        print(f"错误: {e}", file=sys.stderr)
        return False

def main():
    print("=" * 60)
    print("STM32F103 固件烧录工具")
    print("=" * 60)

    # 检查固件文件
    if not os.path.exists(FIRMWARE_BIN):
        print(f"错误: 固件文件不存在: {FIRMWARE_BIN}")
        return 1

    file_size = os.path.getsize(FIRMWARE_BIN)
    print(f"固件文件: {FIRMWARE_BIN}")
    print(f"固件大小: {file_size} 字节 ({file_size / 1024:.2f} KB)")
    print(f"烧录地址: {FLASH_ADDRESS}")
    print()

    # 步骤 1: 连接目标
    print("[1/4] 连接目标芯片...")
    if not run_openocd_command([
        "init",
        "halt",
        "exit"
    ]):
        print("错误: 无法连接目标")
        return 1

    print("✓ 连接成功\n")
    time.sleep(0.5)

    # 步骤 2: 擦除 Flash
    print("[2/4] 擦除 Flash 区域...")
    if not run_openocd_command([
        "init",
        "halt",
        f"flash erase_address {FLASH_ADDRESS} 0x1C000",  # 擦除 112KB (128KB - 16KB bootloader)
        "exit"
    ]):
        print("错误: Flash 擦除失败")
        return 1

    print("✓ 擦除完成\n")
    time.sleep(0.5)

    # 步骤 3: 写入固件
    print("[3/4] 写入固件...")
    if not run_openocd_command([
        "init",
        "halt",
        f"flash write_bank 0 {FIRMWARE_BIN} {int(FLASH_ADDRESS, 16) - 0x08000000}",
        "exit"
    ]):
        print("警告: write_bank 失败，尝试 write_image...")
        if not run_openocd_command([
            "init",
            "halt",
            f"program {FIRMWARE_BIN} {FLASH_ADDRESS} verify",
            "exit"
        ]):
            print("错误: 固件写入失败")
            return 1

    print("✓ 写入完成\n")
    time.sleep(0.5)

    # 步骤 4: 复位运行
    print("[4/4] 复位并运行...")
    if not run_openocd_command([
        "init",
        "reset run",
        "exit"
    ]):
        print("警告: 复位命令失败（但固件可能已成功烧录）")

    print("✓ 复位完成\n")

    print("=" * 60)
    print("✓ 固件烧录完成！")
    print("=" * 60)
    print("\n设备应该已重启并运行新固件。")
    print("请检查串口输出验证启动 banner。")

    return 0

if __name__ == "__main__":
    sys.exit(main())
