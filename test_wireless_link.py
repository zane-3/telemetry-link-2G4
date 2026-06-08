#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
E28 One-to-Many Wireless Link Diagnostic Tool
测试主从机之间的无线通信
"""

import serial
import serial.tools.list_ports
import time
import sys

# 配置串口参数
MASTER_PORT = 'COM7'  # 主机串口
SLAVE_PORT = 'COM6'   # 从机串口
BAUDRATE = 115200
TIMEOUT = 2

def list_serial_ports():
    """列出所有可用串口"""
    ports = serial.tools.list_ports.comports()
    print("\n可用串口:")
    for port in ports:
        print(f"  {port.device} - {port.description}")
    return ports

class E28Device:
    def __init__(self, port, name):
        self.port = port
        self.name = name
        self.ser = None

    def open(self):
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=BAUDRATE,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=TIMEOUT
            )
            print(f"✓ {self.name} ({self.port}) 已连接")
            time.sleep(0.1)
            # 清空缓冲区
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            return True
        except Exception as e:
            print(f"✗ {self.name} ({self.port}) 连接失败: {e}")
            return False

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()
            print(f"✓ {self.name} 已断开")

    def send_raw(self, data):
        """发送原始字节"""
        if self.ser and self.ser.is_open:
            self.ser.write(data)
            print(f"[{self.name} 发送] {data.hex(' ').upper()}")
            return True
        return False

    def send_string(self, text):
        """发送字符串"""
        if self.ser and self.ser.is_open:
            self.ser.write(text.encode())
            print(f"[{self.name} 发送] {text}")
            return True
        return False

    def receive(self, timeout=1.0):
        """接收数据"""
        if not self.ser or not self.ser.is_open:
            return None

        old_timeout = self.ser.timeout
        self.ser.timeout = timeout

        data = b''
        start_time = time.time()

        while (time.time() - start_time) < timeout:
            if self.ser.in_waiting > 0:
                chunk = self.ser.read(self.ser.in_waiting)
                data += chunk
                time.sleep(0.05)  # 等待更多数据

        self.ser.timeout = old_timeout

        if data:
            print(f"[{self.name} 接收] {data.hex(' ').upper()}")
            try:
                text = data.decode('utf-8', errors='ignore')
                if text.isprintable() or '\r' in text or '\n' in text:
                    print(f"           → {repr(text)}")
            except:
                pass

        return data if data else None

def test_config_query(device):
    """测试配置查询"""
    print(f"\n{'='*60}")
    print(f"测试 {device.name} 配置查询")
    print('='*60)

    # 进入配置模式
    device.send_string("+++")
    time.sleep(0.1)
    response = device.receive(0.5)

    if response and b"OK CFG MODE" in response:
        print(f"✓ {device.name} 进入配置模式成功")

        # 查询配置
        time.sleep(0.5)
        device.send_string("CFG?\r\n")
        response = device.receive(1.0)

        if response:
            print(f"✓ {device.name} 配置信息已获取")
            return True
        else:
            print(f"✗ {device.name} 未返回配置信息")
            return False
    else:
        print(f"✗ {device.name} 无法进入配置模式")
        return False

def test_loopback(device):
    """测试本地回环"""
    print(f"\n{'='*60}")
    print(f"测试 {device.name} 本地回环")
    print('='*60)

    test_data = bytes([0xA5, 0x04, 0xFF, 0x02, 0x10, 0x20, 0xD4])

    device.send_raw(test_data)
    time.sleep(0.5)
    response = device.receive(1.0)

    if response and test_data in response:
        print(f"✓ {device.name} 回环测试成功（数据返回）")
        return True
    else:
        print(f"✗ {device.name} 回环测试失败（无数据返回）")
        return False

def test_wireless_link(master, slave):
    """测试无线链路"""
    print(f"\n{'='*60}")
    print(f"测试无线通信: {master.name} → {slave.name}")
    print('='*60)

    # 清空接收缓冲
    master.receive(0.1)
    slave.receive(0.1)

    # 主机发送广播帧
    broadcast_frame = bytes([0xA5, 0x04, 0xFF, 0x02, 0x10, 0x20, 0xD4])
    print(f"\n1. 主机发送广播帧（目标0xFF）:")
    master.send_raw(broadcast_frame)
    time.sleep(0.5)

    slave_response = slave.receive(1.0)
    if slave_response and broadcast_frame in slave_response:
        print(f"✓ 从机收到广播帧")
    else:
        print(f"✗ 从机未收到广播帧")

    time.sleep(1)

    # 主机发送单播帧
    unicast_frame = bytes([0xA5, 0x04, 0x01, 0x02, 0x11, 0x21, 0xD9])
    print(f"\n2. 主机发送单播帧（目标0x01）:")
    master.send_raw(unicast_frame)
    time.sleep(0.5)

    slave_response = slave.receive(1.0)
    if slave_response and unicast_frame in slave_response:
        print(f"✓ 从机收到单播帧")

        # 等待ACK
        time.sleep(0.5)
        master_response = master.receive(1.0)
        if master_response and b'\xa5' in master_response:
            print(f"✓ 主机收到ACK应答")
            return True
        else:
            print(f"✗ 主机未收到ACK应答")
            return False
    else:
        print(f"✗ 从机未收到单播帧")
        return False

    time.sleep(1)

    # 从机发送到主机
    print(f"\n3. 从机发送单播帧（目标0x02）:")
    slave_to_master = bytes([0xA5, 0x04, 0x02, 0x01, 0x12, 0x22, 0xDC])
    slave.send_raw(slave_to_master)
    time.sleep(0.5)

    master_response = master.receive(1.0)
    if master_response and slave_to_master in master_response:
        print(f"✓ 主机收到从机数据")
        return True
    else:
        print(f"✗ 主机未收到从机数据")
        return False

def main():
    print("="*60)
    print("E28 One-to-Many 无线链路诊断工具")
    print("="*60)

    # 列出可用串口
    list_serial_ports()
    print()

    master = E28Device(MASTER_PORT, "主机")
    slave = E28Device(SLAVE_PORT, "从机")

    try:
        # 打开串口
        if not master.open():
            print("\n✗ 主机串口打开失败，退出测试")
            return

        if not slave.open():
            print("\n✗ 从机串口打开失败，退出测试")
            master.close()
            return

        time.sleep(1)

        # 测试1: 配置查询
        master_cfg_ok = test_config_query(master)
        time.sleep(1)
        slave_cfg_ok = test_config_query(slave)

        time.sleep(2)

        # 测试2: 本地回环（检查数据是否经过E28返回）
        # master_loop_ok = test_loopback(master)
        # time.sleep(1)
        # slave_loop_ok = test_loopback(slave)

        time.sleep(2)

        # 测试3: 无线链路
        wireless_ok = test_wireless_link(master, slave)

        # 汇总结果
        print(f"\n{'='*60}")
        print("测试结果汇总")
        print('='*60)
        print(f"主机配置查询: {'✓ 通过' if master_cfg_ok else '✗ 失败'}")
        print(f"从机配置查询: {'✓ 通过' if slave_cfg_ok else '✗ 失败'}")
        # print(f"主机回环测试: {'✓ 通过' if master_loop_ok else '✗ 失败'}")
        # print(f"从机回环测试: {'✓ 通过' if slave_loop_ok else '✗ 失败'}")
        print(f"无线通信测试: {'✓ 通过' if wireless_ok else '✗ 失败'}")

        if not wireless_ok:
            print("\n可能的问题:")
            print("1. E28模块物理连接问题（天线、供电）")
            print("2. E28模块配置未生效（重新配置并重启）")
            print("3. 距离太远或有障碍物")
            print("4. E28模块硬件故障")

    except KeyboardInterrupt:
        print("\n\n用户中断测试")
    except Exception as e:
        print(f"\n✗ 测试出错: {e}")
        import traceback
        traceback.print_exc()
    finally:
        master.close()
        slave.close()
        print("\n测试结束")

if __name__ == "__main__":
    main()
