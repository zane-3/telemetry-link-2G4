# TelemetryLink-2G4

STM32F103 + E28-2G4T12S 2.4G 数传固件。

当前分支实现了 E28-2G4T12S 的首次 HEX 配置、透明传输、一对多广播/监听、运行时主从角色配置、目标接收端过滤和 ACK 机制。

## 硬件和串口约定

| 功能 | 说明 |
| --- | --- |
| Radio UART | MCU 到 E28-2G4T12S 模块 |
| Host UART | 外部主机/飞控/调试串口 |
| E28 配置模式 | `M2=1, M1=1, M0=1`，固定 `9600 8N1`，发送 HEX 指令 |
| E28 传输模式 | `M2=1, M1=0, M0=0`，业务串口 `115200 8N1` |

E28-2G4T12S 配置使用 HEX 指令，不使用 `AT` / `ATSxxx` 文本命令。

## 首次配置流程

首次烧录后，固件会根据运行时配置生成 E28 参数帧：

```text
C0 ADDH ADDL SPED CHAN OPTION
```

默认参数：

```text
SPED   = 0x28    // 115200 UART + 空速配置
CHAN   = 0x18
OPTION = 0x04    // 透明传输、推挽输出、12dBm
```

主站默认 E28 地址：

```text
0xFFFF
```

从站 E28 地址：

```text
0x0001 ~ 0x00FE
```

配置成功后，固件会将运行时配置、CRC 和 E28 marker 写入 MCU Flash。后续上电时，如果配置没有变化，将直接进入传输模式，不会重复配置 E28。

## 一对多模式说明

本项目当前采用 **透明广播/监听 + MCU 业务帧过滤** 的方式实现一对多。

### 主站

主站 E28 地址配置为：

```text
0xFFFF
```

同信道从站都可以收到主站发送的数据；主站也可以监听同信道从站回传的数据。

### 从站

每个从站必须设置唯一 ID：

```text
从站 1: ID=1, E28 地址 0x0001
从站 2: ID=2, E28 地址 0x0002
从站 3: ID=3, E28 地址 0x0003
```

所有模块必须保持相同信道，例如：

```text
CH=0x18
```

## 运行时串口配置命令

配置命令从 Host UART 输入，命令以回车换行结束：

```text
\r\n
```

### 查询配置

```text
CFG?
```

返回内容会包含：

```text
ROLE
ID
REMOTE
CH
OPTION
FILTER
ACK
E28
```

### 配置主站

```text
CFG ROLE=MASTER
CFG ID=1
CFG REMOTE=1
CFG CH=0x18
CFG OPTION=0x04
CFG FILTER=1
CFG ACK=1
CFG SAVE
CFG APPLY
```

主站应用后，E28 应配置为：

```text
C0 FF FF 28 18 04
```

### 配置从站 1

```text
CFG ROLE=SLAVE
CFG ID=1
CFG REMOTE=1
CFG CH=0x18
CFG OPTION=0x04
CFG FILTER=1
CFG ACK=1
CFG SAVE
CFG APPLY
```

从站 1 应配置为：

```text
C0 00 01 28 18 04
```

### 配置从站 2

```text
CFG ROLE=SLAVE
CFG ID=2
CFG REMOTE=1
CFG CH=0x18
CFG OPTION=0x04
CFG FILTER=1
CFG ACK=1
CFG SAVE
CFG APPLY
```

从站 2 应配置为：

```text
C0 00 02 28 18 04
```

### 常用命令

| 命令 | 说明 |
| --- | --- |
| `CFG?` | 查询当前配置 |
| `CFG ROLE=MASTER` | 设置为主站 |
| `CFG ROLE=SLAVE` | 设置为从站 |
| `CFG ID=1` | 设置本机 ID |
| `CFG REMOTE=1` | 设置默认远端 ID |
| `CFG CH=0x18` | 设置 E28 信道，建议使用 `0x` 十六进制格式 |
| `CFG OPTION=0x04` | 设置 E28 OPTION，建议使用 `0x` 十六进制格式 |
| `CFG FILTER=1` | 启用目标 ID 帧过滤 |
| `CFG FILTER=0` | 关闭帧过滤，恢复透明透传 |
| `CFG ACK=1` | 启用 ACK |
| `CFG ACK=0` | 关闭 ACK |
| `CFG SAVE` | 保存当前运行时配置到 MCU Flash |
| `CFG APPLY` | 立即根据当前配置重新配置 E28 |
| `CFG DEFAULT` | 恢复默认配置并清除 E28 marker |

建议使用：

```text
CFG CH=0x18
CFG OPTION=0x04
```

不要依赖 `CFG CH=18` 这种写法，避免十进制/十六进制理解不一致。

## 业务帧格式

开启 `FILTER=1` 后，无线接收的数据必须符合业务帧格式，否则不会转发给 Host UART。

```text
+------+-----+--------+-----+-----+-----+---------+-----+
| HEAD | LEN | TARGET | SRC | SEQ | CMD | PAYLOAD | CRC |
+------+-----+--------+-----+-----+-----+---------+-----+
  1B     1B    1B       1B    1B    1B    0~N       1B
```

字段：

```text
HEAD   = 0xA5
LEN    = TARGET 到 PAYLOAD 的长度，不包含 HEAD/LEN/CRC
TARGET = 目标 ID，0xFF 表示广播
SRC    = 源 ID
SEQ    = 序号
CMD    = 命令
CRC    = 前面所有字节的 8-bit sum
```

示例：

```text
主站 1 发给从站 2:
A5 04 02 01 10 20 CRC

主站 1 广播:
A5 04 FF 01 11 20 CRC

从站 2 回复主站 1:
A5 05 01 02 10 A0 00 CRC
```

## ACK 规则

当 `ACK=1` 且主站发送非广播帧时，主站会记录等待 ACK。

从站收到目标为自己的非 ACK 帧后，会回复：

```text
TARGET = 主站 ID / REMOTE
SRC    = 本机 ID
SEQ    = 原请求 SEQ
CMD    = 原请求 CMD | 0x80
PAYLOAD[0] = status
```

广播帧默认不要求 ACK，避免多个从站同时回包造成碰撞。

## 多从站回传注意事项

E28 透明广播/监听不能解决多个从站同时回传的空口碰撞。实际使用时建议：

1. 主站轮询从站，从站只在被点名后回传。
2. 从站按 ID 分时隙回传。
3. 上层协议必须带 `SRC`，主站通过 `SRC` 区分不同从站。

## 构建

```powershell
powershell -ExecutionPolicy Bypass -File .\build_app.ps1 -Role master -LocalId 1
powershell -ExecutionPolicy Bypass -File .\build_app.ps1 -Role slave -LocalId 1
powershell -ExecutionPolicy Bypass -File .\build_app.ps1 -Role slave -LocalId 2
```

说明：构建参数只作为默认值/出厂值，实际角色、ID、信道、过滤和 ACK 可通过串口命令运行时修改并保存到 Flash。

## 建议验证步骤

1. 分别烧录 1 个主站、2 个从站。
2. 通过串口执行 `CFG?` 确认配置。
3. 主站执行 `CFG ROLE=MASTER`、`CFG SAVE`、`CFG APPLY`。
4. 从站分别执行 `CFG ROLE=SLAVE`、`CFG ID=1/2`、`CFG SAVE`、`CFG APPLY`。
5. 抓 E28 配置帧，确认主站为 `C0 FF FF 28 18 04`，从站 1 为 `C0 00 01 28 18 04`，从站 2 为 `C0 00 02 28 18 04`。
6. 测试 `TARGET=0xFF` 广播帧，两个从站都应处理。
7. 测试 `TARGET=0x01`，只有从站 1 处理。
8. 测试 `TARGET=0x02`，只有从站 2 处理。
9. 测试 ACK 与超时计数。
