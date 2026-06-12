#!/usr/bin/env python3
"""
CRSF 接收机模拟器 - 带 UI 显示
模拟飞控接收 CRSF 数据并显示通道值
"""

import tkinter as tk
from tkinter import ttk, scrolledtext
import serial
import serial.tools.list_ports
import threading
import time
from datetime import datetime

class CRSFReceiver:
    def __init__(self):
        self.running = False
        self.serial_port = None
        self.frame_count = 0
        self.error_count = 0
        self.channels = [0] * 16
        self.last_update = None

    def crc8_dvb_s2(self, data):
        """CRSF CRC8 计算 (DVB-S2 多项式 0xD5)"""
        crc = 0
        for byte in data:
            crc ^= byte
            for _ in range(8):
                if crc & 0x80:
                    crc = (crc << 1) ^ 0xD5
                else:
                    crc = crc << 1
            crc &= 0xFF
        return crc

    def parse_channels(self, payload):
        """解析 CRSF 11-bit 打包通道数据"""
        if len(payload) < 22:
            return None

        channels = []
        bit_index = 0

        for i in range(16):
            value = 0
            for bit in range(11):
                byte_index = bit_index // 8
                bit_offset = bit_index % 8
                if byte_index < len(payload):
                    if payload[byte_index] & (1 << bit_offset):
                        value |= (1 << bit)
                bit_index += 1
            channels.append(value)

        return channels

    def parse_frame(self, data):
        """解析 CRSF 帧"""
        if len(data) < 4:
            return None

        # 检查帧头
        if data[0] != 0xC8:
            return None

        frame_size = data[1]
        if len(data) < frame_size + 2:
            return None

        frame_type = data[2]
        payload = data[3:frame_size+1]
        received_crc = data[frame_size+1]

        # 验证 CRC
        calculated_crc = self.crc8_dvb_s2(data[2:frame_size+1])
        if received_crc != calculated_crc:
            return {'error': 'CRC mismatch', 'type': frame_type}

        if frame_type == 0x16:  # RC_CHANNELS_PACKED
            channels = self.parse_channels(payload)
            if channels:
                return {
                    'type': 'RC_CHANNELS',
                    'channels': channels,
                    'raw': data[:frame_size+2].hex()
                }

        return {
            'type': f'Unknown (0x{frame_type:02X})',
            'raw': data[:frame_size+2].hex()
        }

    def read_serial(self, port, baudrate, callback):
        """读取串口数据"""
        try:
            self.serial_port = serial.Serial(port, baudrate, timeout=0.1)
            buffer = bytearray()

            while self.running:
                if self.serial_port.in_waiting > 0:
                    data = self.serial_port.read(self.serial_port.in_waiting)
                    buffer.extend(data)

                    # 尝试解析帧
                    while len(buffer) >= 4:
                        if buffer[0] == 0xC8:
                            frame_size = buffer[1]
                            if len(buffer) >= frame_size + 2:
                                frame_data = buffer[:frame_size+2]
                                result = self.parse_frame(frame_data)

                                if result:
                                    if 'error' in result:
                                        self.error_count += 1
                                        callback('error', result)
                                    else:
                                        self.frame_count += 1
                                        if result['type'] == 'RC_CHANNELS':
                                            self.channels = result['channels']
                                            self.last_update = datetime.now()
                                        callback('frame', result)

                                buffer = buffer[frame_size+2:]
                            else:
                                break
                        else:
                            buffer.pop(0)

                time.sleep(0.001)

        except serial.SerialException as e:
            callback('error', {'error': str(e)})
        finally:
            if self.serial_port and self.serial_port.is_open:
                self.serial_port.close()

class CRSFMonitorUI:
    def __init__(self, root):
        self.root = root
        self.root.title("CRSF 接收机模拟器 - Betaflight 风格")
        self.root.geometry("900x700")
        self.root.resizable(False, False)

        self.receiver = CRSFReceiver()
        self.monitor_thread = None

        self.setup_ui()
        self.update_channel_display()

    def setup_ui(self):
        """设置 UI"""
        # 顶部控制面板
        control_frame = ttk.Frame(self.root, padding="10")
        control_frame.pack(fill=tk.X)

        # 串口选择
        ttk.Label(control_frame, text="串口:").pack(side=tk.LEFT, padx=5)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(control_frame, textvariable=self.port_var, width=15)
        self.port_combo['values'] = [p.device for p in serial.tools.list_ports.comports()]
        if self.port_combo['values']:
            self.port_combo.current(0)
        self.port_combo.pack(side=tk.LEFT, padx=5)

        # 波特率选择
        ttk.Label(control_frame, text="波特率:").pack(side=tk.LEFT, padx=5)
        self.baudrate_var = tk.StringVar(value="420000")
        baudrate_combo = ttk.Combobox(control_frame, textvariable=self.baudrate_var, width=10)
        baudrate_combo['values'] = ['115200', '420000', '460800']
        baudrate_combo.pack(side=tk.LEFT, padx=5)

        # 刷新按钮
        ttk.Button(control_frame, text="刷新串口", command=self.refresh_ports).pack(side=tk.LEFT, padx=5)

        # 启动/停止按钮
        self.start_button = ttk.Button(control_frame, text="启动监控", command=self.toggle_monitor)
        self.start_button.pack(side=tk.LEFT, padx=5)

        # 状态标签
        self.status_label = ttk.Label(control_frame, text="未连接", foreground="red")
        self.status_label.pack(side=tk.LEFT, padx=20)

        # 通道显示区域
        channel_frame = ttk.LabelFrame(self.root, text="遥控通道 (CRSF)", padding="10")
        channel_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        # 创建通道进度条
        self.channel_bars = []
        self.channel_labels = []
        channel_names = ['横滚[A]', '俯仰[E]', '油门[T]', '方向[R]',
                        'AUX1', 'AUX2', 'AUX3', 'AUX4',
                        'AUX5', 'AUX6', 'AUX7', 'AUX8',
                        'AUX9', 'AUX10', 'AUX11', 'AUX12']

        for i in range(16):
            row_frame = ttk.Frame(channel_frame)
            row_frame.pack(fill=tk.X, pady=2)

            # 通道名称
            name_label = ttk.Label(row_frame, text=channel_names[i], width=12, anchor=tk.W)
            name_label.pack(side=tk.LEFT, padx=5)

            # 进度条
            bar = ttk.Progressbar(row_frame, length=400, maximum=1811)
            bar.pack(side=tk.LEFT, padx=5)
            self.channel_bars.append(bar)

            # 数值标签
            value_label = ttk.Label(row_frame, text="0", width=6, anchor=tk.E)
            value_label.pack(side=tk.LEFT, padx=5)
            self.channel_labels.append(value_label)

        # 统计信息
        stats_frame = ttk.LabelFrame(self.root, text="统计信息", padding="10")
        stats_frame.pack(fill=tk.X, padx=10, pady=5)

        stats_grid = ttk.Frame(stats_frame)
        stats_grid.pack()

        ttk.Label(stats_grid, text="接收帧数:").grid(row=0, column=0, sticky=tk.W, padx=5)
        self.frame_count_label = ttk.Label(stats_grid, text="0", foreground="blue")
        self.frame_count_label.grid(row=0, column=1, sticky=tk.W, padx=5)

        ttk.Label(stats_grid, text="错误帧数:").grid(row=0, column=2, sticky=tk.W, padx=20)
        self.error_count_label = ttk.Label(stats_grid, text="0", foreground="red")
        self.error_count_label.grid(row=0, column=3, sticky=tk.W, padx=5)

        ttk.Label(stats_grid, text="更新时间:").grid(row=0, column=4, sticky=tk.W, padx=20)
        self.update_time_label = ttk.Label(stats_grid, text="--", foreground="green")
        self.update_time_label.grid(row=0, column=5, sticky=tk.W, padx=5)

        # 日志区域
        log_frame = ttk.LabelFrame(self.root, text="日志", padding="10")
        log_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        self.log_text = scrolledtext.ScrolledText(log_frame, height=8, state=tk.DISABLED)
        self.log_text.pack(fill=tk.BOTH, expand=True)

    def refresh_ports(self):
        """刷新串口列表"""
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo['values'] = ports
        if ports:
            self.port_combo.current(0)
        self.log("刷新串口列表: " + ", ".join(ports))

    def toggle_monitor(self):
        """启动/停止监控"""
        if not self.receiver.running:
            self.start_monitor()
        else:
            self.stop_monitor()

    def start_monitor(self):
        """启动监控"""
        port = self.port_var.get()
        baudrate = int(self.baudrate_var.get())

        if not port:
            self.log("错误: 请选择串口", "error")
            return

        self.receiver.running = True
        self.receiver.frame_count = 0
        self.receiver.error_count = 0

        self.monitor_thread = threading.Thread(
            target=self.receiver.read_serial,
            args=(port, baudrate, self.on_data_received),
            daemon=True
        )
        self.monitor_thread.start()

        self.start_button.config(text="停止监控")
        self.status_label.config(text="监控中", foreground="green")
        self.log(f"开始监控 {port} @ {baudrate} bps")

    def stop_monitor(self):
        """停止监控"""
        self.receiver.running = False
        if self.monitor_thread:
            self.monitor_thread.join(timeout=1)

        self.start_button.config(text="启动监控")
        self.status_label.config(text="已停止", foreground="red")
        self.log("监控已停止")

    def on_data_received(self, event_type, data):
        """串口数据回调"""
        if event_type == 'frame':
            if data['type'] == 'RC_CHANNELS':
                self.root.after(0, self.update_stats)
        elif event_type == 'error':
            self.root.after(0, lambda: self.log(f"错误: {data.get('error', 'Unknown')}", "error"))

    def update_channel_display(self):
        """更新通道显示"""
        for i, (bar, label) in enumerate(zip(self.channel_bars, self.channel_labels)):
            value = self.receiver.channels[i]
            bar['value'] = value

            # 转换为微秒 (CRSF 172-1811 → PWM 988-2012)
            pwm_value = int(988 + (value - 172) * (2012 - 988) / (1811 - 172))
            label.config(text=str(pwm_value))

            # 根据值设置颜色
            if i == 2:  # 油门
                if value < 300:
                    label.config(foreground="green")
                else:
                    label.config(foreground="red")
            elif i == 4:  # ARM
                if value < 300:
                    label.config(foreground="green")
                else:
                    label.config(foreground="orange")
            else:
                label.config(foreground="black")

        self.root.after(50, self.update_channel_display)

    def update_stats(self):
        """更新统计信息"""
        self.frame_count_label.config(text=str(self.receiver.frame_count))
        self.error_count_label.config(text=str(self.receiver.error_count))

        if self.receiver.last_update:
            time_str = self.receiver.last_update.strftime("%H:%M:%S.%f")[:-3]
            self.update_time_label.config(text=time_str)

        # 计算帧率
        if self.receiver.frame_count > 0:
            self.status_label.config(text=f"接收中 ({self.receiver.frame_count}帧)")

    def log(self, message, level="info"):
        """添加日志"""
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.log_text.config(state=tk.NORMAL)

        if level == "error":
            tag = "error"
            prefix = "[错误]"
        else:
            tag = "info"
            prefix = "[信息]"

        self.log_text.insert(tk.END, f"{timestamp} {prefix} {message}\n", tag)
        self.log_text.tag_config("error", foreground="red")
        self.log_text.tag_config("info", foreground="black")
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)

if __name__ == "__main__":
    root = tk.Tk()
    app = CRSFMonitorUI(root)
    root.mainloop()
