# PRD: 2.4G 从站扩展为 CRSF 接收机输出

## 1. 目标

把 `telemetry-link-2G4` 的无人机端从站扩展成一个可以被飞控识别的 CRSF 接收机输出端。

最终链路：

```text
手持端 IMU/按键生成 RC 通道
    ↓ A5 RC_CHANNELS 帧
2.4G 主站 E28
    ↓ 无线透明传输
2.4G 从站 E28
    ↓ CRSF RC_CHANNELS_PACKED
飞控 UART RX
```

本 PR 只负责 **2.4G 从站收到无线 RC 通道后，转换成 CRSF 输出给飞控**。

## 2. 实现优先级

第一阶段先实现最小闭环：

1. 保持 E28 Radio UART 为 `115200 8N1`。
2. 新增 CRSF 输出 UART 为 `420000 8N1`。
3. 新增 `APP_FRAME_CMD_RC_CHANNELS = 0x10`。
4. 从站收到 A5 帧且目标 ID 匹配本机时，如果 `CMD=0x10`，解析 16 路 RC 通道。
5. 将 16 路 RC 通道转换成 CRSF `RC_CHANNELS_PACKED`。
6. 通过 Host UART 输出给飞控。
7. RC 模式下不要把 A5 原始业务帧直接透传给飞控。
8. 增加 RC failsafe，超过 300ms 未收到 RC 帧时输出安全通道值。

## 3. 串口要求

当前工程里所有 USART 可能共用 `APP_UART_BAUDRATE = 115200U`，不能直接改成 `420000`，否则 E28 通信会被破坏。

需要拆分波特率：

```c
#define APP_RADIO_UART_BAUDRATE 115200U
#define APP_CRSF_UART_BAUDRATE  420000U
#define APP_DEBUG_UART_BAUDRATE 115200U
```

推荐分配：

```text
USART1 / radio_uart  -> 115200，接 E28-2G4T12S
USART2 / host_uart1  -> 420000，接飞控 UART RX，输出 CRSF
USART3 / host_uart2  -> 115200，调试或保留
```

## 4. A5 RC_CHANNELS 业务帧

新增命令：

```c
#define APP_FRAME_CMD_RC_CHANNELS 0x10U
```

帧格式沿用当前 A5 业务帧：

```text
HEAD   = 0xA5
LEN    = TARGET 到 PAYLOAD 的长度
TARGET = 接收机 ID，例如 1
SRC    = 手持/主站 ID
SEQ    = 自增序号
CMD    = 0x10
PAYLOAD = 16 个 uint16_t 通道值，小端
CRC    = 8-bit sum
```

Payload：

```text
ch[0]  Roll
ch[1]  Pitch
ch[2]  Throttle
ch[3]  Yaw
ch[4]  Arm
ch[5]  Mode
ch[6]~ch[15] 保留
```

CRSF 通道范围：

```text
最小 172
中位 992
最大 1811
```

收到 payload 后必须限幅到 `172~1811`。

## 5. CRSF 输出要求

需要新增文件：

```text
application/App/Inc/app_crsf.h
application/App/Src/app_crsf.c
```

实现接口建议：

```c
void AppCrsf_SendRcChannels(UART_HandleTypeDef *uart, const uint16_t ch[16]);
```

CRSF RC frame：

```text
0xC8 0x18 0x16 [22 bytes packed 16ch 11-bit payload] CRC8
```

字段说明：

```text
0xC8 = flight controller address / sync byte
0x18 = frame size，type + payload + crc = 24
0x16 = RC_CHANNELS_PACKED
payload = 16 个 11-bit 通道，打包后 22 字节
CRC8 poly = 0xD5
CRC 计算范围 = type + payload
```

## 6. App_ProcessFrame 修改点

当前从无线收到匹配帧后，会把 A5 原始帧写到 `host_uart1`。CRSF 模式下应修改为：

```c
if (from_radio && cmd == APP_FRAME_CMD_RC_CHANNELS) {
    uint16_t channels[16];
    parse_rc_channels_from_payload(payload, payload_len, channels);
    AppCrsf_SendRcChannels(g_app.hw.host_uart1, channels);
    g_app.last_rc_ms = HAL_GetTick();
    return;
}
```

注意：

- `CMD=0x10` 时不要继续透传原始 A5 帧给飞控。
- 非 RC 帧可以保留原来的调试/透传逻辑。
- 仍然保留当前 `CFG ROLE/ID/REMOTE/CH/FILTER/ACK/SAVE` 配置命令。

## 7. Failsafe 要求

新增 RC failsafe：

```text
如果超过 300ms 未收到合法 RC_CHANNELS 帧：
CH1 Roll     = 992
CH2 Pitch    = 992
CH3 Throttle = 172
CH4 Yaw      = 992
CH5 Arm      = 172 / disarm
其他通道      = 992
```

建议周期性输出 failsafe CRSF 帧，避免飞控保持上一次油门。

## 8. 测试步骤

### 8.1 固定 CRSF 中位测试

先不接手持端，让 2.4G 从站自己周期性输出固定 CRSF：

```text
CH1 = 992
CH2 = 992
CH3 = 172
CH4 = 992
CH5 = 172
```

Betaflight Receiver 页面应能看到通道。

### 8.2 无线 RC 帧测试

从主站发送 `CMD=0x10` A5 RC frame，从站转换为 CRSF。

预期：Betaflight Receiver 页面 CH1~CH4 跟随变化。

### 8.3 Failsafe 测试

停止发送 RC 帧超过 300ms：

```text
油门变最低
ARM 通道关闭
Roll/Pitch/Yaw 回中
```

## 9. Betaflight 配置

接线：

```text
2.4G 从站 USART2_TX -> 飞控某个 UART_RX
GND -> GND
供电按板卡要求接 5V 或 3.3V
```

配置：

```text
Ports 页面：对应 UART 打开 Serial RX
Receiver 页面：
Receiver Mode = Serial-based receiver
Serial Receiver Provider = CRSF
```

## 10. 不在本 PR 做的事情

- 不实现手持端 IMU 驱动。
- 不实现姿态解算。
- 不把油门交给 IMU 控制。
- 不改变 E28 配置模式和现有 CFG 命令语义。

## 11. 验收标准

- E28 Radio UART 仍然可以正常收发 A5 帧。
- Host UART 可以以 420000 输出 CRSF。
- CRSF RC 输出周期为 10ms，目标 100Hz。
- Betaflight 能识别 CRSF 接收机。
- Receiver 页面能看到 CH1~CH5。
- 停止无线输入后 300ms 内进入安全通道输出。
- 油门 failsafe 始终为最低值。
