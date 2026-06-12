#!/usr/bin/env python3
"""
CRSF 输出诊断工具
用于验证从站 USART2 是否正确输出 CRSF 数据
"""

import serial
import sys
import time

def parse_crsf_frame(data):
    """解析 CRSF 帧"""
    if len(data) < 4:
        return None

    if data[0] != 0xC8:
        return None

    frame_size = data[1]
    if len(data) < frame_size + 2:
        return None

    frame_type = data[2]

    if frame_type == 0x16:  # RC_CHANNELS_PACKED
        return {
            'type': 'RC_CHANNELS_PACKED',
            'size': frame_size,
            'raw': data[:frame_size+2].hex()
        }

    return {
        'type': f'Unknown (0x{frame_type:02X})',
        'size': frame_size,
        'raw': data[:frame_size+2].hex()
    }

def monitor_crsf(port, baudrate=420000, duration=5):
    """监控 CRSF 输出"""
    print(f"CRSF 输出监控工具")
    print(f"=" * 60)
    print(f"串口: {port}")
    print(f"波特率: {baudrate}")
    print(f"监控时长: {duration} 秒")
    print(f"=" * 60)
    print()

    try:
        ser = serial.Serial(port, baudrate, timeout=0.1)
        print(f"✓ 串口打开成功")
        print(f"等待 CRSF 数据...\n")

        start_time = time.time()
        frame_count = 0
        byte_count = 0
        buffer = bytearray()

        while (time.time() - start_time) < duration:
            if ser.in_waiting > 0:
                data = ser.read(ser.in_waiting)
                byte_count += len(data)
                buffer.extend(data)

                # 尝试解析帧
                while len(buffer) >= 4:
                    if buffer[0] == 0xC8:
                        frame_size = buffer[1]
                        if len(buffer) >= frame_size + 2:
                            frame_data = buffer[:frame_size+2]
                            frame = parse_crsf_frame(frame_data)

                            if frame:
                                frame_count += 1
                                elapsed = time.time() - start_time
                                print(f"[{elapsed:.2f}s] 帧 #{frame_count}: {frame['type']}")
                                print(f"  大小: {frame['size']} 字节")
                                print(f"  数据: {frame['raw']}")
                                print()

                            buffer = buffer[frame_size+2:]
                        else:
                            break
                    else:
                        buffer.pop(0)

            time.sleep(0.001)

        ser.close()

        print(f"=" * 60)
        print(f"监控完成")
        print(f"=" * 60)
        print(f"总字节数: {byte_count}")
        print(f"总帧数: {frame_count}")
        if frame_count > 0:
            print(f"平均帧率: {frame_count / duration:.1f} Hz")
            print(f"\n✓ CRSF 输出正常！")
        else:
            print(f"\n✗ 未检测到 CRSF 帧")
            print(f"  可能原因：")
            print(f"  1. 接线错误（TX 未连接）")
            print(f"  2. 波特率不匹配")
            print(f"  3. 从站未运行测试固件")
            print(f"  4. 串口号错误")

    except serial.SerialException as e:
        print(f"✗ 串口错误: {e}")
        print(f"\n可用串口:")
        import serial.tools.list_ports
        for port in serial.tools.list_ports.comports():
            print(f"  - {port.device}: {port.description}")
        return False
    except Exception as e:
        print(f"✗ 错误: {e}")
        return False

    return frame_count > 0

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python crsf_monitor.py <串口号> [波特率]")
        print()
        print("示例:")
        print("  Windows: python crsf_monitor.py COM11 420000")
        print("  Linux:   python crsf_monitor.py /dev/ttyUSB0 420000")
        print()
        print("可用串口:")
        import serial.tools.list_ports
        for port in serial.tools.list_ports.comports():
            print(f"  - {port.device}: {port.description}")
        sys.exit(1)

    port = sys.argv[1]
    baudrate = int(sys.argv[2]) if len(sys.argv) > 2 else 420000

    success = monitor_crsf(port, baudrate)
    sys.exit(0 if success else 1)
