# CRSF Receiver Output 测试指南

## 测试日期
2026-06-10

## 固件信息
- **固件路径**: `build/application/application.bin` / `application.hex`
- **固件大小**: 14596 字节 (代码) + 3400 字节 (数据/BSS)
- **目标芯片**: STM32F103CBTx
- **基地址**: 0x08004000 (Application, Bootloader 偏移)

## 硬件连接

### 2.4G 从站接线
```
从站板            飞控
---------------------------------
USART2_TX    →   UART_RX (任意可用 UART)
GND          →   GND
5V/3.3V      ←   按板卡要求供电
```

### E28 无线模块
```
从站 USART1 (115200) ↔ E28-2G4T12S
```

## 烧录方法

### 方法 1: OpenOCD (推荐，如果有 ST-Link/J-Link)

**创建 `openocd.cfg` 配置文件**:
```cfg
source [find interface/stlink.cfg]
source [find target/stm32f1x.cfg]

# Application 起始地址 (跳过 bootloader)
set FLASH_START 0x08004000

init
reset halt
flash write_image erase build/application/application.bin $FLASH_START
verify_image build/application/application.bin $FLASH_START
reset run
shutdown
```

**执行烧录**:
```bash
openocd -f openocd.cfg
```

### 方法 2: ST-Link Utility (Windows GUI)
1. 打开 STM32 ST-LINK Utility
2. 连接目标板
3. File → Open File → 选择 `application.hex`
4. Target → Program & Verify
5. Start Address: `0x08004000`
6. 点击 Start

### 方法 3: STM32CubeProgrammer (跨平台 GUI)
1. 打开 STM32CubeProgrammer
2. 选择 ST-LINK 连接
3. 点击 Connect
4. 在 Erasing & Programming 页面
5. 选择 `application.bin` 或 `application.hex`
6. Start Address: `0x08004000`
7. 点击 Start Programming

### 方法 4: 串口 Bootloader (如果可用)
如果项目有串口 bootloader：
```bash
# 假设使用自定义协议或 STM32 系统 bootloader
stm32flash -w build/application/application.bin -v -g 0x08004000 /dev/ttyUSB0
```

## 测试流程

### 阶段 1: 固定 CRSF 输出测试 (不需要主站)

**目的**: 验证 CRSF 模块和飞控连接

**步骤**:
1. 连接从站 USART2_TX → 飞控 UART_RX
2. 给从站上电
3. 打开 Betaflight Configurator
4. 进入 Ports 页面
   - 找到连接的 UART
   - Serial RX = 启用
   - 波特率 = 420000
5. 进入 Receiver 页面
   - Receiver Mode = Serial-based receiver
   - Serial Receiver Provider = CRSF
6. 保存并重启飞控

**预期结果**:
- 从站启动后，串口应输出 Banner:
  ```
  === E28 ONE-TO-MANY v1.3 ===
  USART2 HOST READY
  E28 RECONFIG COUNT: 1
  CFG ROLE=SLAVE ID=0001 REMOTE=0001 ...
  ```
- ⚠️ 由于没有收到 RC 数据，不会主动输出 CRSF 帧
- 需要修改代码添加测试代码（见下方）

**临时测试代码修改** (可选):
在 `app.c` 的 `App_Run()` 中添加：
```c
static uint32_t last_test_output = 0;
if ((HAL_GetTick() - last_test_output) >= 100) {
    AppCrsf_SendFailsafe(g_app.hw.host_uart1);
    last_test_output = HAL_GetTick();
}
```

重新编译并烧录后，Betaflight Receiver 页面应该能看到通道数据（全部中位/最低）。

---

### 阶段 2: 无线 RC 帧测试

**目的**: 验证完整链路（主站 → 从站 → 飞控）

**前提**:
- 需要主站固件支持发送 `CMD=0x10` RC_CHANNELS 帧
- 主站和从站 ID 配置匹配

**A5 RC_CHANNELS 帧格式**:
```
0xA5                    // HEAD
0x24                    // LEN (36 = 4 + 32 payload)
0x01                    // TARGET (从站 ID)
0x01                    // SRC (主站 ID)
0x00                    // SEQ
0x10                    // CMD (RC_CHANNELS)
[32 bytes payload]      // 16 * uint16_t (小端)
0xXX                    // CRC (8-bit sum)
```

**Payload 示例** (中位测试):
```c
uint16_t channels[16] = {
    992, 992, 992, 992,  // Roll, Pitch, Throttle, Yaw
    992, 992, 992, 992,  // Arm, Mode, ...
    992, 992, 992, 992,
    992, 992, 992, 992
};
// 小端编码: 0xE0 0x03, 0xE0 0x03, ...
```

**测试步骤**:
1. 确保主站和从站无线连接正常
2. 主站发送 RC_CHANNELS 帧
3. 观察 Betaflight Receiver 页面

**预期结果**:
- CH1-4 (Roll/Pitch/Throttle/Yaw) 跟随主站数据变化
- 通道范围: 172 (最低) ~ 992 (中位) ~ 1811 (最高)
- 飞控显示 "CRSF" 接收机已连接

**调试输出**:
从站不会输出 A5 原始帧到 USART2（已改为 CRSF），但可以通过：
- USART3 (调试串口，如果连接)
- LED 闪烁模式
- 使用逻辑分析仪抓取 USART2 TX

---

### 阶段 3: Failsafe 测试

**目的**: 验证 300ms 超时保护

**测试步骤**:
1. 主站正常发送 RC 帧
2. 观察 Betaflight Receiver 通道数据正常
3. **停止主站发送**（关闭主站或断开无线）
4. 等待 300ms

**预期结果**:
- CH1 (Roll) = 992 (中位)
- CH2 (Pitch) = 992 (中位)
- CH3 (Throttle) = 172 (最低) ← **关键：油门归零**
- CH4 (Yaw) = 992 (中位)
- CH5 (Arm) = 172 (disarm) ← **关键：解锁关闭**
- CH6-16 = 992 (中位)
- 每 100ms 周期性输出 failsafe 帧

**安全验证**:
- 如果飞机已解锁，failsafe 应触发飞控的失控保护
- Betaflight 应显示 "RX LOSS" 或类似警告

---

## 常见问题排查

### 问题 1: Betaflight 不识别接收机

**检查**:
- ✅ USART2_TX 连接到飞控 UART_RX（不是 TX）
- ✅ 飞控 Ports 页面对应 UART 启用了 Serial RX
- ✅ 波特率设置为 420000
- ✅ Receiver Provider 设置为 CRSF
- ✅ 从站正常上电（LED 闪烁）

**调试**:
```bash
# 使用逻辑分析仪或示波器检查 USART2_TX
# 应该看到 420000 bps 串口信号
```

### 问题 2: 通道数据不动

**可能原因**:
1. 从站没有收到 RC 帧
   - 检查主站是否发送 `CMD=0x10`
   - 检查主站/从站 ID 配置
   - 检查无线信号强度

2. CRSF 帧格式错误
   - 使用串口监视器抓取 USART2 输出
   - 验证帧格式: `0xC8 0x18 0x16 [22 bytes] CRC8`

### 问题 3: 波特率误差

**症状**: 飞控时而识别时而丢失

**解决**:
- STM32F103 在 72MHz 下，420000 bps 可能有小误差
- 可尝试调整为 416666 或 400000
- 修改 `app_config.h`:
  ```c
  #define APP_CRSF_UART_BAUDRATE  400000U  // 降低波特率
  ```

### 问题 4: Failsafe 不触发

**检查**:
- 从站角色是否为 SLAVE（MASTER 不会触发 failsafe）
- `last_rc_ms` 是否被正确更新
- 确认至少收到过一次 RC 帧

**调试代码**:
在 `App_CheckRcFailsafe()` 添加诊断输出（需要 USART3）

---

## 性能指标

### CRSF 帧率
- **当前实现**: 按需发送（收到 A5 RC 帧时立即转发）
- **建议**: 50-150 Hz
- **优化**: 添加帧率限制，避免过载飞控

### 延迟
- A5 接收 → CRSF 输出: < 1ms
- 总链路延迟: 主站 IMU → 无线 → 从站 → 飞控 ≈ 10-30ms

### 可靠性
- Failsafe 触发时间: 300ms (符合 RC 标准)
- CRSF CRC8 保护数据完整性

---

## 配置命令参考

### 查看当前配置
```
1. 通过 USART2 发送: +++
2. 等待响应: OK CFG MODE
3. 发送: CFG?
4. 响应显示当前配置
```

### 修改从站 ID
```
CFG ID=1
```

### 保存并重启
```
CFG SAVE
```

---

## 下一步开发建议

### 优先级 P0 (必须)
- [ ] 硬件测试验证 CRSF 输出
- [ ] 验证 Betaflight 兼容性
- [ ] 测试 Failsafe 可靠性

### 优先级 P1 (重要)
- [ ] 添加 CRSF 帧率控制 (100Hz)
- [ ] 添加诊断计数器 (RC 接收数、CRSF 发送数、Failsafe 次数)
- [ ] 主站固件实现 RC_CHANNELS 发送

### 优先级 P2 (改进)
- [ ] 支持 CRSF Link Statistics (双向通信)
- [ ] 可配置通道映射
- [ ] 可调整 Failsafe 超时时间
- [ ] RSSI 信号强度传递

---

## 测试检查清单

- [ ] 固件成功烧录
- [ ] 从站正常启动（串口 banner 输出）
- [ ] E28 配置成功
- [ ] Betaflight 识别 CRSF 接收机
- [ ] Receiver 页面显示通道数据
- [ ] 通道数据跟随主站输入变化
- [ ] Failsafe 在 300ms 后触发
- [ ] 油门 failsafe 归零
- [ ] ARM 通道 failsafe disarm
- [ ] 恢复主站发送后正常工作
- [ ] 长时间稳定性测试 (>5 分钟)

---

## 联系和支持

如遇到问题，请收集以下信息：
1. 从站启动 banner 输出
2. Betaflight Receiver 页面截图
3. 逻辑分析仪抓取的 USART2 数据（如有）
4. 主站/从站配置信息

测试完成后请更新验收状态到实施文档。
