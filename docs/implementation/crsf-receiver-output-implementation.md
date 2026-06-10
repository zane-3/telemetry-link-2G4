# CRSF Receiver Output Implementation

## 实施日期
2026-06-10

## 概述
成功实现 2.4G 从站扩展为 CRSF 接收机输出功能，使其能被飞控识别为 CRSF 接收机。

## 实施内容

### 1. 波特率配置拆分
**文件**: `application/App/Inc/app_config.h`

拆分原统一波特率配置为三个独立定义：
- `APP_RADIO_UART_BAUDRATE = 115200` - E28 无线模块通信
- `APP_CRSF_UART_BAUDRATE = 420000` - CRSF 飞控输出
- `APP_DEBUG_UART_BAUDRATE = 115200` - 调试串口

### 2. 新增 CRSF 模块
**文件**: 
- `application/App/Inc/app_crsf.h`
- `application/App/Src/app_crsf.c`

**功能**:
- `AppCrsf_SendRcChannels()` - 发送 RC 通道数据（CRSF RC_CHANNELS_PACKED）
- `AppCrsf_SendFailsafe()` - 发送安全通道值
- `AppCrsf_Crc8()` - CRSF CRC8 校验（poly 0xD5）
- `AppCrsf_PackChannels()` - 16 通道 11-bit 打包
- `AppCrsf_ClampChannel()` - 通道值限幅到 172-1811

**CRSF 帧格式**:
```
0xC8 0x18 0x16 [22 bytes packed payload] CRC8
```

### 3. RC_CHANNELS 命令支持
**文件**: `application/App/Src/app.c`

**新增**:
- `APP_FRAME_CMD_RC_CHANNELS = 0x10` - RC 通道命令
- `last_rc_ms` - 最后收到 RC 时间戳
- `last_rc_failsafe_output_ms` - 最后输出 failsafe 时间戳

**修改**:
- `App_ProcessFrame()` - 识别并处理 RC_CHANNELS 命令
  - 从 A5 帧 payload 解析 16 路 uint16_t 通道值
  - 转换为 CRSF 格式输出到 host_uart1
  - RC 帧不再透传原始 A5 帧给飞控
  - 非 RC 帧保持原有透传逻辑

- `App_ParseRcChannels()` - 解析 A5 RC payload
  - 期望 32 字节（16 * uint16_t 小端）
  - 数据不足时填充中位值

- `App_CheckRcFailsafe()` - 检查 RC 超时
  - 仅从站模式生效
  - 超过 300ms 未收到 RC 时输出安全值
  - 每 100ms 周期性输出 failsafe

### 4. Failsafe 安全通道值
- CH1 Roll = 992 (中位)
- CH2 Pitch = 992 (中位)
- CH3 Throttle = 172 (最低)
- CH4 Yaw = 992 (中位)
- CH5 Arm = 172 (disarm)
- CH6-16 = 992 (中位)

### 5. UART 波特率配置
**文件**: `application/Core/Src/main.c`

- USART1 (radio_uart): 115200 bps
- USART2 (host_uart1): 420000 bps - **CRSF 输出**
- USART3 (host_uart2): 115200 bps

### 6. 构建系统更新
**文件**: `application/Makefile`

添加 `App/Src/app_crsf.c` 到 `C_SOURCES`

## 构建结果
```
   text	   data	    bss	    dec	    hex	filename
  14596	     32	   3368	  17996	   464c	application.elf
```

固件大小增加约 ~500 字节（CRSF 模块代码）

## 技术细节

### A5 RC_CHANNELS 帧格式
```
HEAD   = 0xA5
LEN    = TARGET 到 PAYLOAD 的长度
TARGET = 从站 ID
SRC    = 主站 ID
SEQ    = 序号
CMD    = 0x10
PAYLOAD = 16 个 uint16_t (小端)
CRC    = 8-bit sum
```

### CRSF RC_CHANNELS_PACKED 帧
```
0xC8 = Flight Controller Address
0x18 = Frame Size (24)
0x16 = RC_CHANNELS_PACKED Type
[22 bytes] = 16 channels * 11-bit packed
CRC8 = poly 0xD5, 计算范围 type + payload
```

### 11-bit 通道打包
CRSF 每通道使用 11-bit：
- 最小值: 172
- 中位值: 992
- 最大值: 1811

16 通道 * 11 bits = 176 bits = 22 bytes

## 配置保留
- 现有 CFG 命令完全保留（ROLE/ID/REMOTE/CH/FILTER/ACK/SAVE）
- E28 配置流程不变
- 主从模式切换不受影响
- 非 RC 帧继续透传（兼容调试和其他业务）

## 测试建议

### 阶段 1: 固定 CRSF 测试
在从站代码中临时添加周期性输出固定 CRSF 帧：
```c
AppCrsf_SendFailsafe(g_app.hw.host_uart1);
```
验证 Betaflight Receiver 页面能看到通道。

### 阶段 2: 无线 RC 测试
从主站发送 `CMD=0x10` 的 A5 帧，验证：
- 从站能正确解析
- CRSF 输出到飞控
- Betaflight 通道跟随变化

### 阶段 3: Failsafe 测试
停止主站发送超过 300ms，验证：
- 油门降到最低
- ARM 通道关闭
- Roll/Pitch/Yaw 回中

## Betaflight 配置
**Ports 页面**:
- 对应 UART 打开 Serial RX

**Receiver 页面**:
- Receiver Mode = Serial-based receiver
- Serial Receiver Provider = CRSF

## 验收标准
- ✅ E28 Radio UART 仍然 115200 正常工作
- ✅ Host UART 以 420000 输出 CRSF
- ✅ 编译通过，固件大小合理
- ⏳ Betaflight 能识别 CRSF 接收机（需硬件测试）
- ⏳ Receiver 页面能看到 CH1~CH5（需硬件测试）
- ⏳ 300ms 超时进入 failsafe（需硬件测试）

## 未实施内容
- ❌ 手持端 IMU 驱动
- ❌ 姿态解算
- ❌ 油门由 IMU 控制
- ❌ E28 配置模式修改

## 风险和注意事项
1. **波特率误差**: 420000 bps 可能在某些飞控上有误差，需实测验证
2. **CRSF 帧率**: 当前按需发送，未限制帧率。正常应为 50-150Hz
3. **通道映射**: 当前假设主站发送的通道顺序为 Roll/Pitch/Throttle/Yaw/Arm/Mode
4. **Failsafe 触发**: 首次 RC 帧到达前不会输出 failsafe

## 后续优化建议
1. 添加 CRSF 帧率控制（建议 100Hz）
2. 添加诊断计数器（RC 帧接收数、CRSF 发送数、Failsafe 触发数）
3. 考虑支持 CRSF 双向通信（Link Statistics）
4. 添加通道映射配置
