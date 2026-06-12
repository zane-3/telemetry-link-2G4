@echo off
echo ========================================
echo CRSF 快速测试工具
echo ========================================
echo.

REM 检查 Python
where python >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未找到 Python
    echo.
    echo 请安装 Python 3.x: https://www.python.org/downloads/
    echo.
    pause
    exit /b 1
)

echo Python 已安装:
python --version
echo.

REM 安装依赖
echo 安装 pyserial...
python -m pip install pyserial --quiet --disable-pip-version-check
echo.

echo ========================================
echo 启动 CRSF 监控工具...
echo ========================================
echo.
echo 连接到 COM6 @ 420000 bps
echo 监控 5 秒钟...
echo.

cd /d "%~dp0"

REM 运行简单测试
python -c "
import serial
import time
import sys

def crc8(data):
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

print('正在打开 COM6...')
try:
    ser = serial.Serial('COM6', 420000, timeout=0.1)
    print('✓ 串口已打开')
    print('等待 CRSF 数据...')
    print()

    buffer = bytearray()
    start_time = time.time()
    frame_count = 0

    while time.time() - start_time < 5:
        if ser.in_waiting > 0:
            data = ser.read(ser.in_waiting)
            buffer.extend(data)

            while len(buffer) >= 4:
                if buffer[0] == 0xC8:
                    frame_size = buffer[1]
                    if len(buffer) >= frame_size + 2:
                        frame_data = buffer[:frame_size+2]
                        frame_type = frame_data[2]
                        received_crc = frame_data[frame_size+1]
                        calculated_crc = crc8(frame_data[2:frame_size+1])

                        if received_crc == calculated_crc:
                            frame_count += 1
                            if frame_type == 0x16:
                                print(f'[{time.time()-start_time:.2f}s] ✓ CRSF RC_CHANNELS 帧 #{frame_count}')

                                # 解析第一个通道
                                payload = frame_data[3:frame_size+1]
                                ch1 = payload[0] | ((payload[1] & 0x07) << 8)
                                print(f'  CH1 值: {ch1} (应该接近 992)')
                            else:
                                print(f'[{time.time()-start_time:.2f}s] 帧类型: 0x{frame_type:02X}')
                        else:
                            print(f'[{time.time()-start_time:.2f}s] ✗ CRC 错误')

                        buffer = buffer[frame_size+2:]
                    else:
                        break
                else:
                    buffer.pop(0)

        time.sleep(0.01)

    ser.close()

    print()
    print('========================================')
    print(f'测试完成！')
    print(f'总帧数: {frame_count}')
    if frame_count > 0:
        print(f'平均帧率: {frame_count / 5:.1f} Hz')
        print()
        print('✓ CRSF 输出正常！从站工作正常。')
    else:
        print()
        print('✗ 未收到 CRSF 数据')
        print('可能原因:')
        print('  1. COM6 不是正确的端口')
        print('  2. 从站未连接或未运行')
        print('  3. 波特率不匹配')
    print('========================================')

except serial.SerialException as e:
    print(f'✗ 串口错误: {e}')
    print()
    print('可用串口:')
    import serial.tools.list_ports
    for port in serial.tools.list_ports.comports():
        print(f'  - {port.device}: {port.description}')
    sys.exit(1)
except Exception as e:
    print(f'✗ 错误: {e}')
    import traceback
    traceback.print_exc()
    sys.exit(1)
"

echo.
pause
