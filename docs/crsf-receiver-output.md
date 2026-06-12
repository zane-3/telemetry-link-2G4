# PRD: 2.4G 从站扩展为 CRSF 接收机输出

## 1. 目标

把 `telemetry-link-2G4` 的无人机端从站扩展成一个可以被飞控识别的 CRSF 接收机输出端，并解决当前联调中出现的 **飞控通道值偶尔跳动一次** 问题。

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

本 PR 只负责 **2.4G 从站收到无线 RC 通道后，转换成 CRSF 输出给飞控**，并在接收机端增加丢包、乱序、异常跳变和 failsafe 保护。

## 2. 当前问题判断

飞控 Receiver 页面偶尔出现某个通道值跳动一次，优先按以下原因处理：

1. 2.4G 链路偶发丢包、坏包、乱序或延迟。
2. 当前 A5 业务帧只使用 8-bit sum 校验，极少数错误帧可能碰巧通过。
3. 从站当前收到合法目标帧后会立即转发/输出，没有对 `SEQ` 连续性、通道突变或单帧异常做保护。
4. 如果手持端 ADC/IMU 本身有尖峰，接收端也不能直接把单帧尖峰输出给飞控。

因此本 PR 不能只做“无线转 CRSF”，还必须加入接收机端抗跳变处理。

## 3. 实现优先级

第一阶段先实现最小闭环：

1. 保持 E28 Radio UART 为 `115200 8N1`。
2. 新增 CRSF 输出 UART 为 `420000 8N1`。
3. 新增 `APP_FRAME_CMD_RC_CHANNELS = 0x10`。
4. 从站收到 A5 帧且目标 ID 匹配本机时，如果 `CMD=0x10`，解析 16 路 RC 通道。
5. 将 16 路 RC 通道转换成 CRSF `RC_CHANNELS_PACKED`。
6. 通过 Host UART 输出给飞控。
7. RC 模式下不要把 A5 原始业务帧直接透传给飞控。
8. 增加 RC failsafe，超过 300ms 未收到合法 RC 帧时输出安全通道值。
9. 增加 `SEQ` 连续性检测、异常帧计数、通道突变限幅和单帧尖峰抑制。

## 4. 串口要求

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

## 5. A5 RC_CHANNELS 业务帧

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

## 6. CRSF 输出要求

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

## 7. App_ProcessFrame 修改点

当前从无线收到匹配帧后，会把 A5 原始帧写到 `host_uart1`。CRSF 模式下应修改为：

```c
if (from_radio && cmd == APP_FRAME_CMD_RC_CHANNELS) {
    uint16_t raw_channels[16];
    uint16_t safe_channels[16];
    parse_rc_channels_from_payload(payload, payload_len, raw_channels);
    AppRcFilter_Update(seq, raw_channels, safe_channels);
    AppCrsf_SendRcChannels(g_app.hw.host_uart1, safe_channels);
    g_app.last_rc_ms = HAL_GetTick();
    return;
}
```

注意：

- `CMD=0x10` 时不要继续透传原始 A5 帧给飞控。
- 非 RC 帧可以保留原来的调试/透传逻辑。
- 仍然保留当前 `CFG ROLE/ID/REMOTE/CH/FILTER/ACK/SAVE` 配置命令。

## 8. 抗丢包、乱序和跳变要求

### 8.1 SEQ 连续性检测

从站必须记录上一帧 RC `SEQ`。

规则：

```text
expected_seq = last_seq + 1
```

如果收到的 `SEQ` 不连续：

- 不要立刻 failsafe。
- 记录 `rc_seq_gap_count`。
- 如果只是少量丢帧，继续使用当前帧，但必须经过通道限幅和突变限制。
- 如果连续超过 300ms 没有合法 RC 帧，进入 failsafe。

### 8.2 通道范围硬限幅

任何进入 CRSF 输出的通道值都必须先限幅：

```text
172 <= channel <= 1811
```

超出范围的值不得直接输出到飞控，必须夹到边界，并记录 `rc_range_clip_count`。

### 8.3 单帧尖峰抑制

为避免“偶尔跳一下”，每个通道必须和上一帧已输出值比较。

建议第一阶段阈值：

```text
Roll/Pitch/Yaw：单帧最大变化 250
Throttle：单帧最大变化 120
Arm：只允许在明确开关逻辑下变化，不跟随异常中间值
```

如果某一帧变化超过阈值：

- 不要直接输出突变值。
- 可以限制为 `last_output ± max_step`。
- 或者要求连续 2 帧同方向异常后才接受。
- 记录 `rc_jump_suppress_count`。

### 8.4 Throttle 特殊保护

油门通道 `CH3` 必须最保守：

- failsafe 时永远输出 `172`。
- 未 ARM 时永远输出 `172`。
- 任何单帧油门突然升高不得直接输出。
- ARM 关闭或异常时，立即油门最低。

### 8.5 诊断计数

新增或复用诊断计数：

```text
rc_frame_rx_count
rc_frame_drop_count
rc_seq_gap_count
rc_range_clip_count
rc_jump_suppress_count
rc_failsafe_count
crsf_tx_count
```

调试串口或 `CFG?` 扩展输出中应能看到这些计数，便于判断问题是频率干扰、丢包还是输入端尖峰。

## 9. Failsafe 要求

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

## 10. 测试步骤

### 10.1 固定 CRSF 中位测试

先不接手持端，让 2.4G 从站自己周期性输出固定 CRSF：

```text
CH1 = 992
CH2 = 992
CH3 = 172
CH4 = 992
CH5 = 172
```

Betaflight Receiver 页面应能看到通道，并且通道值不跳动。

### 10.2 无线 RC 帧测试

从主站发送 `CMD=0x10` A5 RC frame，从站转换为 CRSF。

预期：Betaflight Receiver 页面 CH1~CH4 跟随变化。

### 10.3 固定输入抗跳变测试

让主站连续发送固定 RC 值：

```text
CH1 = 992
CH2 = 992
CH3 = 172
CH4 = 992
CH5 = 172
```

观察 2 分钟：

- 飞控 Receiver 页面不得偶尔跳动。
- `rc_jump_suppress_count` 应为 0。
- 如果 `rc_seq_gap_count` 增加，说明链路存在丢包或干扰。

### 10.4 注入异常帧测试

人为插入一帧异常通道值：

```text
CH1 从 992 瞬间变 1811，然后下一帧回 992
```

预期：

- 飞控页面不应直接跳到 1811。
- `rc_jump_suppress_count` 增加。
- 下一帧恢复正常后继续输出平稳值。

### 10.5 Failsafe 测试

停止发送 RC 帧超过 300ms：

```text
油门变最低
ARM 通道关闭
Roll/Pitch/Yaw 回中
```

## 11. Betaflight 配置

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

## 12. 不在本 PR 做的事情

- 不实现手持端 IMU 驱动。
- 不实现姿态解算。
- 不把油门交给 IMU 控制。
- 不改变 E28 配置模式和现有 CFG 命令语义。
- 不新增新的 PR；本需求覆盖到之前的 CRSF PR 中继续推进。

## 13. 验收标准

- E28 Radio UART 仍然可以正常收发 A5 帧。
- Host UART 可以以 420000 输出 CRSF。
- CRSF RC 输出周期为 10ms，目标 100Hz。
- Betaflight 能识别 CRSF 接收机。
- Receiver 页面能看到 CH1~CH5。
- 固定输入测试下 Receiver 页面不得偶发跳动。
- 停止无线输入后 300ms 内进入安全通道输出。
- 油门 failsafe 始终为最低值。
- `SEQ` 不连续能被计数。
- 单帧异常跳变不会直接输出给飞控。
- 通道值必须限制在 `172~1811`。
