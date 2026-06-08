# Codex 任务：E28-2G4T12S 一对多通信模式

## 目标

请基于当前 `zane-3/telemetry-link-2G4` 仓库，实现 E28-2G4T12S 的一对多通信配置、运行时主从角色配置、目标接收端过滤与 ACK 机制。

本任务只定义实现目标和验证步骤，不假设硬件测试已经通过。

## 手册依据

根据 E28-2G4T12S 产品规格书：

- 传输模式（模式 0）：`M2=1, M1=0, M0=0`，串口打开，无线打开，连续透明传输。
- 配置模式（模式 3）：`M2=1, M1=1, M0=1`，串口打开，无线关闭，用于参数配置，波特率固定 `9600 8N1`。
- 广播地址：发送端地址设置为 `0xFFFF` 且信道一致时，同信道下所有接收模块都可以收到数据。
- 监听地址：接收端地址设置为 `0xFFFF` 且信道一致时，可以接收同信道下所有数据。
- 定点传输：`OPTION bit7=1` 时，用户数据帧前 3 个字节作为目标地址高字节、目标地址低字节、目标信道。

## 关键设计结论

当前仓库是串口透传固件，核心目标是尽量保持上层串口数据不被破坏。因此一对多优先采用 **透明广播/监听方案**，不默认启用 E28 定点传输。

原因：

- 当前代码是字节级透传，没有完整业务包边界。
- 如果直接启用 E28 定点传输，E28 会把用户数据的前 3 个字节当成目标地址和信道，可能破坏原始串口数据流。
- 若后续要使用 E28 定点传输，必须先在 MCU 侧增加帧缓存、包边界识别和目标地址前缀插入逻辑。

本任务采用三层设计：

1. E28 无线层：透明广播/监听。
2. MCU 配置层：通过串口命令运行时修改主站/从站角色、本机 ID、信道、是否启用过滤。
3. MCU 协议层：通过 `TARGET/SRC/SEQ/CMD/ACK` 判断数据发给哪个接收端。

## 运行时配置要求

### 不再使用编译期固定主从角色

主站/从站角色不能只靠编译宏写死。必须改成 **运行时配置**：

- 通过主机串口发送配置命令修改。
- 配置保存到 MCU Flash 参数区。
- 重启后读取 Flash 参数并继续使用。
- 当角色、本机 ID、信道或 E28 相关参数变化时，必须触发 E28 重新配置。

编译期宏只能作为默认值或恢复出厂默认值使用，不能作为唯一配置来源。

### 建议运行时配置结构

新增配置结构，示例：

```c
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t crc;
    uint8_t role;
    uint8_t local_id;
    uint8_t remote_id;
    uint8_t channel;
    uint8_t option;
    uint8_t filter_enable;
    uint8_t ack_enable;
    uint8_t reserved[8];
} app_radio_runtime_config_t;
```

建议默认值：

```c
role          = APP_RADIO_ROLE_SLAVE
local_id      = 1
remote_id     = 1
channel       = 0x18
option        = 0x04
filter_enable = 1
ack_enable    = 1
```

### 配置保存要求

请实现：

1. Flash 中保存运行时配置。
2. 配置包含 `magic/version/crc`，防止误读坏数据。
3. 如果 Flash 配置无效，使用默认配置。
4. 串口修改配置后，必须写入 Flash。
5. 修改影响 E28 参数的配置后，需要清除或更新 E28 配置 marker，使下次立即或重启后重新配置 E28。
6. 角色和本机 ID 必须参与 E28 配置 marker 计算。主站、从站 1、从站 2 的 marker 不能相同。

### 串口配置命令

请实现一个简单 ASCII 命令接口，建议走主机调试/控制串口。命令以 `\r\n` 结束。

最低要求命令：

```text
CFG?
CFG ROLE=MASTER
CFG ROLE=SLAVE
CFG ID=1
CFG ID=2
CFG REMOTE=1
CFG CH=18
CFG FILTER=1
CFG FILTER=0
CFG ACK=1
CFG ACK=0
CFG SAVE
CFG DEFAULT
```

命令说明：

- `CFG?`：打印当前运行时配置，包括 role/local_id/remote_id/channel/option/filter/ack/e28_addr/e28_frame。
- `CFG ROLE=MASTER`：设置为主站。
- `CFG ROLE=SLAVE`：设置为从站。
- `CFG ID=n`：设置本机 ID。从站地址由 ID 映射生成，主站业务源 ID 也使用该 ID。
- `CFG REMOTE=n`：设置默认远端 ID，用于后续单播默认目标。
- `CFG CH=xx`：设置信道，十六进制或十进制均可。
- `CFG FILTER=1/0`：启用或关闭 `TARGET/SRC` 帧过滤。
- `CFG ACK=1/0`：启用或关闭 ACK。
- `CFG SAVE`：保存运行时配置到 MCU Flash。
- `CFG SAVE`：保存当前配置后重启，启动时按新配置重新生成 E28 参数并执行配置流程。
- `CFG DEFAULT`：恢复默认配置并清除 E28 配置 marker。

建议返回：

```text
OK
ERR <reason>
```

### 串口配置状态机要求

由于固件原本做透传，配置命令不能误吞正常业务数据。建议实现以下任意一种方式：

#### 方案 A：配置前缀触发

只有收到固定前缀才进入配置命令模式，例如：

```text
+++
```

进入配置模式后，30 秒内接收 `CFG ...` 命令；超时自动退出并恢复透传。

#### 方案 B：独立控制串口

如果硬件上有明确的调试/控制串口，则配置命令只在该串口解析；无线透传数据不参与配置解析。

Codex 需要根据当前仓库串口分工选择更安全的实现，避免业务透传数据被误判为配置命令。

## E28 地址生成规则

### 主站配置

主站用于向多个从站广播，也用于监听多个从站回复。

运行时配置为主站时，E28 参数建议：

```text
地址：0xFFFF
信道：runtime.channel，例如 0x18
OPTION：0x04
模式：透明传输
UART：115200 8N1
```

对应 E28 配置帧示例：

```text
C0 FF FF 28 18 04
```

说明：

- 地址 `0xFFFF` 作为广播地址，主站发送时，同信道从站都能收到。
- 地址 `0xFFFF` 作为监听地址，主站接收时，可以监听同信道下多个从站的数据。
- `OPTION=0x04` 保持透明传输，不启用定点传输，避免破坏原始串口流。

### 从站配置

运行时配置为从站时，每个从站使用唯一地址，但与主站保持相同信道和空中参数。

建议地址映射：

```c
slave_addr = APP_RADIO_SLAVE_BASE_ADDR + runtime.local_id;
```

示例：

```text
从站 1：地址 0x0001，信道 0x18，OPTION 0x04
从站 2：地址 0x0002，信道 0x18，OPTION 0x04
从站 3：地址 0x0003，信道 0x18，OPTION 0x04
```

对应配置帧示例：

```text
C0 00 01 28 18 04
C0 00 02 28 18 04
C0 00 03 28 18 04
```

## 代码实现要求

请修改配置生成逻辑：

1. 不再使用单一固定 `APP_RADIO_E28_LINK_ADDRESS` 表示所有设备地址。
2. 不再只通过 `APP_RADIO_DEVICE_ROLE` 编译宏决定主从角色。
3. 从 Flash 读取运行时配置，决定当前角色和本机 ID。
4. 根据运行时角色生成 E28 地址：
   - 主站：`0xFFFF`
   - 从站：根据 `runtime.local_id` 生成，如 `0x0001 ~ 0x00FE`
5. 信道使用 `runtime.channel`。
6. 第一阶段保持 `OPTION=0x04`，不要默认启用定点传输。
7. 保持首次烧录配置一次的逻辑，但配置 marker 必须与运行时参数绑定：
   - 同一角色、同一 ID、同一信道、同一 option，配置成功后可跳过重复配置。
   - 修改角色、ID、信道、option 后，必须重新配置 E28。
8. `CFG SAVE` 保存配置后重启，并在启动时按新配置重新配置 E28。

## 目标接收端区分要求

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
2. 如果 `TARGET == runtime.local_id`，处理该帧。
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
- 如果当前必须保持完全透明透传，则提供运行时配置项 `filter_enable`：

```text
CFG FILTER=1
CFG FILTER=0
```

当 `filter_enable=0` 时保持原始透传；当为 `1` 时启用 `TARGET/SRC/SEQ/CMD` 帧解析和过滤。

## ACK 和重发要求

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
6. ACK 是否启用由运行时配置 `ack_enable` 决定。

## 多从站回传注意事项

透明一对多只能保证无线层广播和监听，不能解决多个从站同时回传导致的空口碰撞。

因此上层业务必须满足至少一种策略：

1. 主站只广播，从站不主动回传。
2. 主站轮询从站，从站只在被点名后回传。
3. 从站按固定时隙回传，例如 `slot = runtime.local_id * slot_ms`。
4. 上层数据包带 `source_id`，主站才能区分不同从站。

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
- Flash 参数存储相关文件
- 串口命令解析相关文件
- README 或 `docs/` 下的说明文档

## 建议新增默认宏

这些宏只作为默认值，不作为最终运行时配置来源：

```c
#define APP_RADIO_ROLE_MASTER       1U
#define APP_RADIO_ROLE_SLAVE        2U
#define APP_RADIO_DEFAULT_ROLE      APP_RADIO_ROLE_SLAVE
#define APP_RADIO_MASTER_ADDRESS    0xFFFFU
#define APP_RADIO_SLAVE_BASE_ADDR   0x0000U
#define APP_RADIO_DEFAULT_LOCAL_ID  1U
#define APP_RADIO_DEFAULT_REMOTE_ID 1U
#define APP_RADIO_DEFAULT_CHANNEL   0x18U
#define APP_RADIO_DEFAULT_OPTION    0x04U
#define APP_FRAME_HEAD              0xA5U
#define APP_FRAME_TARGET_BROADCAST  0xFFU
#define APP_FRAME_CMD_ACK_BASE      0x80U
```

从站地址建议由以下方式生成：

```c
slave_addr = APP_RADIO_SLAVE_BASE_ADDR + runtime.local_id;
```

需要保证 `runtime.local_id != 0`，并且不能超过允许范围。

## 验证步骤

### 编译验证

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Project application
```

### 运行时配置命令验证

1. 恢复默认配置：

   ```text
   CFG DEFAULT
   CFG SAVE
   ```

2. 配置主站：

   ```text
   CFG ROLE=MASTER
   CFG ID=1
   CFG CH=18
   CFG FILTER=1
   CFG ACK=1
   CFG SAVE
   ```

3. 配置从站 1：

   ```text
   CFG ROLE=SLAVE
   CFG ID=1
   CFG CH=18
   CFG FILTER=1
   CFG ACK=1
   CFG SAVE
   ```

4. 配置从站 2：

   ```text
   CFG ROLE=SLAVE
   CFG ID=2
   CFG CH=18
   CFG FILTER=1
   CFG ACK=1
   CFG SAVE
   ```

5. 查询配置：

   ```text
   CFG?
   ```

### 首次配置验证

主站配置后，E28 应发送：

```text
C0 FF FF 28 18 04
```

从站 1 配置后，E28 应发送：

```text
C0 00 01 28 18 04
```

从站 2 配置后，E28 应发送：

```text
C0 00 02 28 18 04
```

修改角色或 ID 后，执行 `CFG SAVE` 并重启后，必须重新生成并写入对应 E28 配置。

### 重启保存验证

1. 配置从站 2 并执行 `CFG SAVE`。
2. 断电重启。
3. 执行 `CFG?`。
4. 应显示仍为 `ROLE=SLAVE, ID=2`。
5. 如果 E28 marker 与该配置匹配，不应重复配置 E28。
6. 如果修改 `ID=3` 并执行 `CFG SAVE` 后，应重新配置为 `C0 00 03 28 18 04`。

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

- 主站/从站角色可通过串口命令在运行时修改。
- 本机 ID、远端 ID、信道、过滤开关、ACK 开关可通过串口命令修改。
- 运行时配置可保存到 Flash，断电重启后保持。
- 修改角色、ID、信道或 option 后，E28 会重新配置。
- 主站可广播到多个从站。
- 主站可通过 `TARGET` 指定某个从站处理数据。
- 从站可通过 `TARGET` 判断是否处理或丢弃数据。
- 主站可通过 `SRC` 区分不同从站回包。
- 非广播单播命令支持 ACK 与超时处理。
- 从站地址唯一，信道一致。
- 首次烧录配置一次，后续上电不重复配置，除非运行时配置变化。
- 不破坏当前 115200 串口透传数据流。
- 不默认启用定点传输，除非已实现包边界和目标地址前缀逻辑。
- 文档明确说明多从站同时回传需要上层协议避免冲突。
