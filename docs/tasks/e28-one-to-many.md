# Codex 任务：E28-2G4T12S 一对多通信模式

## 目标

请基于当前 `zane-3/telemetry-link-2G4` 仓库，实现 E28-2G4T12S 的一对多通信配置与转发策略。

本任务只定义实现目标和验证步骤，不假设硬件测试已经通过。

## 手册依据

根据 E28-2G4T12S 产品规格书：

- 传输模式（模式 0）：`M2=1, M1=0, M0=0`，串口打开，无线打开，连续透明传输。
- 配置模式（模式 3）：`M2=1, M1=1, M0=1`，串口打开，无线关闭，用于参数配置，波特率固定 `9600 8N1`。
- 广播地址：发送端地址设置为 `0xFFFF` 且信道一致时，同信道下所有接收模块都可以收到数据。
- 监听地址：接收端地址设置为 `0xFFFF` 且信道一致时，可以接收同信道下所有数据。
- 定点传输：`OPTION bit7=1` 时，用户数据帧前 3 个字节作为目标地址高字节、目标地址低字节、目标信道。

## 推荐实现方案

当前仓库是串口透传固件，核心目标是尽量保持上层串口数据不被破坏。因此一对多优先采用 **透明广播/监听方案**，而不是直接启用定点传输。

原因：

- 当前代码是字节级透传，没有完整业务包边界。
- 如果直接启用定点传输，E28 会把用户数据的前 3 个字节当成目标地址和信道，可能破坏原始串口数据流。
- 若要使用定点传输，必须先在 MCU 侧增加帧缓存、包边界识别和目标地址前缀插入逻辑。

因此本任务推荐分两阶段实现：

1. 第一阶段：透明一对多广播/监听。
2. 第二阶段：可选实现定点传输一对多寻址。

## 第一阶段：透明一对多广播/监听

### 角色定义

新增编译期角色：

```c
#define APP_RADIO_ROLE_MASTER 1U
#define APP_RADIO_ROLE_SLAVE  2U
```

默认建议：

- 主站：`APP_RADIO_DEVICE_ROLE = APP_RADIO_ROLE_MASTER`
- 从站：`APP_RADIO_DEVICE_ROLE = APP_RADIO_ROLE_SLAVE`

### 主站配置

主站用于向多个从站广播，也用于监听多个从站回复。

建议配置：

```text
地址：0xFFFF
信道：0x18
OPTION：0x04
模式：透明传输
UART：115200 8N1
```

配置帧：

```text
C0 FF FF 28 18 04
```

说明：

- 地址 `0xFFFF` 作为广播地址，主站发送时，同信道从站都能收到。
- 地址 `0xFFFF` 作为监听地址，主站接收时，可以监听同信道下多个从站的数据。
- `OPTION=0x04` 保持透明传输，不启用定点传输，避免破坏原始串口流。

### 从站配置

每个从站使用唯一地址，但与主站保持相同信道和空中参数。

建议配置：

```text
从站 1：地址 0x0001，信道 0x18，OPTION 0x04
从站 2：地址 0x0002，信道 0x18，OPTION 0x04
从站 3：地址 0x0003，信道 0x18，OPTION 0x04
...
```

对应配置帧示例：

```text
C0 00 01 28 18 04
C0 00 02 28 18 04
C0 00 03 28 18 04
```

### 代码实现要求

请修改配置生成逻辑：

1. 不再使用单一固定 `APP_RADIO_E28_LINK_ADDRESS` 表示所有设备地址。
2. 根据角色生成 E28 地址：
   - 主站：`0xFFFF`
   - 从站：根据 `APP_RADIO_LOCAL_ID` 生成，如 `0x0001 ~ 0x00FE`
3. 信道保持一致，如 `APP_RADIO_E28_CHANNEL = 0x18`。
4. 第一阶段保持 `OPTION=0x04`，不要启用定点传输。
5. 保持首次烧录配置一次的逻辑：
   - 首次配置成功后写入 Flash marker。
   - 后续上电直接模式 0 + 115200。
6. Flash marker 必须与角色和本地地址绑定。
   - 主站和从站 marker 不应相同。
   - 如果修改 `APP_RADIO_LOCAL_ID`，应重新配置 E28。

### 目标接收端区分要求

透明广播只能让同信道从站都收到数据；E28 模块不会自动告诉接收端“这包数据是给谁的”。因此必须在 MCU 上层协议里增加目标 ID 和源 ID。

新增一个最小业务帧格式，建议如下：

```text
+------+---------+---------+-----+-----+-----+---------+-----+
| HEAD | LEN     | TARGET  | SRC | SEQ | CMD | PAYLOAD | CRC |
+------+---------+---------+-----+-----+-----+---------+-----+
  1B     1B        1B        1B    1B    1B    0~N       1B
```

建议定义：

```c
#define APP_FRAME_HEAD              0xA5U
#define APP_FRAME_TARGET_BROADCAST  0xFFU
#define APP_FRAME_CMD_ACK_BASE      0x80U
```

字段说明：

- `HEAD`：固定帧头，例如 `0xA5`。
- `LEN`：从 `TARGET` 到 `PAYLOAD` 的长度，不包含 `HEAD/LEN/CRC`。
- `TARGET`：目标接收端 ID。
- `SRC`：发送端 ID。
- `SEQ`：序号，用于 ACK 和重发匹配。
- `CMD`：业务命令。
- `PAYLOAD`：业务数据。
- `CRC`：建议先用 8-bit sum 或 xor，后续可升级 CRC16。

接收端处理规则：

1. 收到一帧后先校验帧头、长度和 CRC。
2. 如果 `TARGET == APP_RADIO_LOCAL_ID`，处理该帧。
3. 如果 `TARGET == APP_FRAME_TARGET_BROADCAST`，所有从站处理该帧。
4. 如果 `TARGET` 不是本机 ID 且不是广播 ID，直接丢弃，不向上层输出。
5. 主站收到从站回包时，通过 `SRC` 区分是哪一个从站。

发送示例：

```text
# 主站 1 发给从站 2
A5 04 02 01 10 20 CRC

# 主站 1 广播给所有从站
A5 04 FF 01 11 20 CRC

# 从站 2 回复主站 1
A5 05 01 02 10 A0 00 CRC
```

实现方式建议：

- 在透明广播方案中，E28 仍然只发送原始字节流，不添加 E28 定点前缀。
- MCU 需要在转发层实现帧解析，只有匹配 `TARGET` 的数据才转发给本地上层串口。
- 如果当前必须保持完全透明透传，则至少提供编译开关：

```c
#define APP_RADIO_FILTER_ENABLE 1U
```

当 `APP_RADIO_FILTER_ENABLE=0` 时保持原始透传；当为 `1` 时启用 `TARGET/SRC/SEQ/CMD` 帧解析和过滤。

### ACK 和重发要求

如果主站需要确认指定从站是否收到数据，需要实现 ACK。

建议规则：

1. 主站发送非广播帧时记录 `TARGET + SEQ + CMD`。
2. 从站收到目标为自己的有效帧后，回复 ACK：

   ```text
   TARGET = 主站 ID
   SRC    = 本机 ID
   SEQ    = 原请求 SEQ
   CMD    = 原请求 CMD | APP_FRAME_CMD_ACK_BASE
   PAYLOAD[0] = status
   ```

3. 主站收到 ACK 后根据 `SRC + SEQ` 匹配发送记录。
4. 广播帧默认不要求所有从站 ACK，避免多个从站同时回 ACK 造成碰撞。
5. 如果广播也必须 ACK，应使用主站轮询或从站时隙回传。

### 上层协议注意事项

透明一对多只能保证无线层广播和监听，不能解决多个从站同时回传导致的空口碰撞。

因此上层业务必须满足至少一种策略：

1. 主站只广播，从站不主动回传。
2. 主站轮询从站，从站只在被点名后回传。
3. 从站按固定时隙回传，例如 `slot = APP_RADIO_LOCAL_ID * slot_ms`。
4. 上层数据包带 `source_id`，主站才能区分不同从站。

如果当前上层数据没有 `source_id`，请在 MCU 转发层增加可选源标记，例如：

```text
[S01] payload
[S02] payload
```

或者要求上层协议自己携带设备 ID。

## 第二阶段：可选定点传输方案

如果后续需要主站单独发给某一个从站，可实现 E28 定点传输。

### 定点传输配置

开启 `OPTION bit7=1`：

```text
OPTION = 0x84
```

例如：

```text
C0 00 01 28 18 84
```

### 定点发送数据格式

主站发给从站 2：

```text
00 02 18 + payload
```

主站广播给所有从站：

```text
FF FF 18 + payload
```

### 定点传输注意事项

当前代码是字节透传，不能直接切换到定点传输。必须先实现：

1. 串口输入缓存。
2. 业务包边界识别。
3. 目标地址选择。
4. 发送前插入 3 字节目标地址和信道。
5. 接收端确认 E28 输出是否自动去掉地址前缀。

未实现上述逻辑前，不要把 `OPTION` 默认改成 `0x84`。

## 建议文件修改范围

请重点检查：

- `application/App/Inc/app_config.h`
- `application/App/Src/app.c`
- `application/Core/Src/main.c`
- 构建参数或不同角色配置方式
- README 或 `docs/` 下的说明文档

## 建议新增宏

```c
#define APP_RADIO_ROLE_MASTER       1U
#define APP_RADIO_ROLE_SLAVE        2U
#define APP_RADIO_DEVICE_ROLE       APP_RADIO_ROLE_SLAVE
#define APP_RADIO_MASTER_ADDRESS    0xFFFFU
#define APP_RADIO_SLAVE_BASE_ADDR   0x0000U
#define APP_RADIO_LOCAL_ID          1U
#define APP_RADIO_E28_CHANNEL       0x18U
#define APP_RADIO_E28_OPTION        0x04U
#define APP_RADIO_FILTER_ENABLE     1U
#define APP_FRAME_HEAD              0xA5U
#define APP_FRAME_TARGET_BROADCAST  0xFFU
#define APP_FRAME_CMD_ACK_BASE      0x80U
```

从站地址建议由以下方式生成：

```c
slave_addr = APP_RADIO_SLAVE_BASE_ADDR + APP_RADIO_LOCAL_ID;
```

需要保证 `APP_RADIO_LOCAL_ID != 0`，并且不能超过允许范围。

## 验证步骤

### 编译验证

分别编译主站和从站配置：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Project application
```

如项目支持编译期覆盖宏，建议测试：

```powershell
# 主站
make APP_RADIO_DEVICE_ROLE=APP_RADIO_ROLE_MASTER

# 从站 1
make APP_RADIO_DEVICE_ROLE=APP_RADIO_ROLE_SLAVE APP_RADIO_LOCAL_ID=1

# 从站 2
make APP_RADIO_DEVICE_ROLE=APP_RADIO_ROLE_SLAVE APP_RADIO_LOCAL_ID=2
```

### 首次配置验证

主站首次配置应发送：

```text
C0 FF FF 28 18 04
```

从站 1 首次配置应发送：

```text
C0 00 01 28 18 04
```

从站 2 首次配置应发送：

```text
C0 00 02 28 18 04
```

### 一对多广播验证

1. 准备 1 个主站模块、至少 2 个从站模块。
2. 所有模块信道一致，如 `0x18`。
3. 主站发送广播业务帧，`TARGET=0xFF`。
4. 从站 1 和从站 2 都应收到并处理同样数据。
5. 主站不应因为从站数量增加而改变 E28 发送数据格式。

### 指定接收端验证

1. 主站发送指定从站 1 的业务帧，`TARGET=0x01`。
2. 从站 1 应处理并转发给本地上层串口。
3. 从站 2 应收到无线数据但在 MCU 过滤层丢弃，不应转发给本地上层串口。
4. 主站发送指定从站 2 的业务帧，`TARGET=0x02`。
5. 从站 2 应处理，从站 1 应丢弃。
6. 主站通过 ACK 的 `SRC` 字段确认是哪一个从站回复。

### ACK 验证

1. 主站向从站 1 发送 `SEQ=1` 的非广播命令。
2. 从站 1 回复 `SRC=1, SEQ=1, CMD=原CMD|0x80`。
3. 主站应匹配 ACK 并标记发送成功。
4. 主站向不存在的从站发送命令时，应超时并允许重发或报错。
5. 广播帧默认不要求 ACK。

### 多从站回传验证

1. 从站 1 单独回传，主站应收到。
2. 从站 2 单独回传，主站应收到。
3. 从站 1 和从站 2 同时回传时，记录是否丢包或冲突。
4. 若存在冲突，需要在上层增加轮询或时隙，不应在无线层假设可同时多发一收。

## 验收标准

- 主站可广播到多个从站。
- 主站可通过 `TARGET` 指定某个从站处理数据。
- 从站可通过 `TARGET` 判断是否处理或丢弃数据。
- 主站可通过 `SRC` 区分不同从站回包。
- 非广播单播命令支持 ACK 与超时处理。
- 从站地址唯一，信道一致。
- 首次烧录配置一次，后续上电不重复配置。
- 不破坏当前 115200 串口透传数据流。
- 不默认启用定点传输，除非已实现包边界和目标地址前缀逻辑。
- 文档明确说明多从站同时回传需要上层协议避免冲突。
