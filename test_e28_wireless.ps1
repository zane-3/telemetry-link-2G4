# E28 无线链路自动测试脚本
# 使用 .NET SerialPort 类直接操作串口

param(
    [string]$MasterPort = "COM7",
    [string]$SlavePort = "COM6",
    [int]$Baudrate = 115200
)

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "E28 One-to-Many 无线链路自动诊断工具" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

# 函数：打开串口
function Open-SerialPort {
    param([string]$PortName, [int]$BaudRate)

    try {
        $port = New-Object System.IO.Ports.SerialPort $PortName, $BaudRate, None, 8, One
        $port.ReadTimeout = 2000
        $port.WriteTimeout = 1000
        $port.Open()
        Write-Host "✓ $PortName 已连接" -ForegroundColor Green
        return $port
    }
    catch {
        Write-Host "✗ $PortName 连接失败: $_" -ForegroundColor Red
        return $null
    }
}

# 函数：关闭串口
function Close-SerialPort {
    param($Port)

    if ($Port -and $Port.IsOpen) {
        $Port.Close()
        $Port.Dispose()
    }
}

# 函数：发送字符串
function Send-String {
    param($Port, [string]$Text, [string]$Name)

    if ($Port -and $Port.IsOpen) {
        $Port.Write($Text)
        Write-Host "[$Name 发送] $Text" -ForegroundColor Yellow
        Start-Sleep -Milliseconds 100
    }
}

# 函数：发送字节数组
function Send-Bytes {
    param($Port, [byte[]]$Data, [string]$Name)

    if ($Port -and $Port.IsOpen) {
        $Port.Write($Data, 0, $Data.Length)
        $hexString = ($Data | ForEach-Object { $_.ToString("X2") }) -join ' '
        Write-Host "[$Name 发送] $hexString" -ForegroundColor Yellow
        Start-Sleep -Milliseconds 100
    }
}

# 函数：接收数据
function Receive-Data {
    param($Port, [string]$Name, [int]$TimeoutMs = 1000)

    if (-not $Port -or -not $Port.IsOpen) {
        return $null
    }

    $oldTimeout = $Port.ReadTimeout
    $Port.ReadTimeout = $TimeoutMs

    $data = @()
    $startTime = Get-Date

    try {
        while (((Get-Date) - $startTime).TotalMilliseconds -lt $TimeoutMs) {
            if ($Port.BytesToRead -gt 0) {
                $byte = $Port.ReadByte()
                $data += $byte
                Start-Sleep -Milliseconds 10
            }
            else {
                Start-Sleep -Milliseconds 50
            }
        }
    }
    catch {
        # Timeout is OK
    }

    $Port.ReadTimeout = $oldTimeout

    if ($data.Count -gt 0) {
        $hexString = ($data | ForEach-Object { $_.ToString("X2") }) -join ' '
        Write-Host "[$Name 接收] $hexString" -ForegroundColor Cyan

        # 尝试转换为文本
        $text = [System.Text.Encoding]::UTF8.GetString([byte[]]$data)
        if ($text -match '[a-zA-Z0-9=\s]') {
            Write-Host "           → $text" -ForegroundColor Gray
        }

        return [byte[]]$data
    }

    return $null
}

# 函数：清空接收缓冲
function Clear-Buffer {
    param($Port)

    if ($Port -and $Port.IsOpen -and $Port.BytesToRead -gt 0) {
        $Port.ReadExisting() | Out-Null
    }
}

# 主测试流程
Write-Host "正在连接串口..." -ForegroundColor White
Write-Host ""

$master = Open-SerialPort -PortName $MasterPort -BaudRate $Baudrate
$slave = Open-SerialPort -PortName $SlavePort -BaudRate $Baudrate

if (-not $master -or -not $slave) {
    Write-Host ""
    Write-Host "✗ 串口连接失败，退出测试" -ForegroundColor Red
    Close-SerialPort $master
    Close-SerialPort $slave
    exit 1
}

Start-Sleep -Seconds 1

try {
    # 测试1: 配置查询
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "测试1: 主机配置查询" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan

    Send-String -Port $master -Text "+++" -Name "主机"
    $response = Receive-Data -Port $master -Name "主机" -TimeoutMs 500

    if ($response -and ([System.Text.Encoding]::UTF8.GetString($response) -match "OK CFG MODE")) {
        Write-Host "✓ 主机进入配置模式成功" -ForegroundColor Green

        Start-Sleep -Milliseconds 500
        Send-String -Port $master -Text "CFG?`r`n" -Name "主机"
        $response = Receive-Data -Port $master -Name "主机" -TimeoutMs 1000

        if ($response) {
            Write-Host "✓ 主机配置查询成功" -ForegroundColor Green
        }
    }

    Start-Sleep -Seconds 2

    # 测试2: 从机配置查询
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "测试2: 从机配置查询" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan

    Send-String -Port $slave -Text "+++" -Name "从机"
    $response = Receive-Data -Port $slave -Name "从机" -TimeoutMs 500

    if ($response -and ([System.Text.Encoding]::UTF8.GetString($response) -match "OK CFG MODE")) {
        Write-Host "✓ 从机进入配置模式成功" -ForegroundColor Green

        Start-Sleep -Milliseconds 500
        Send-String -Port $slave -Text "CFG?`r`n" -Name "从机"
        $response = Receive-Data -Port $slave -Name "从机" -TimeoutMs 1000

        if ($response) {
            Write-Host "✓ 从机配置查询成功" -ForegroundColor Green
        }
    }

    Start-Sleep -Seconds 2

    # 清空缓冲
    Clear-Buffer $master
    Clear-Buffer $slave

    # 测试3: 主机发送广播帧
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "测试3: 主机发送广播帧 (目标0xFF)" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan

    $broadcastFrame = [byte[]](0xA5, 0x04, 0xFF, 0x02, 0x10, 0x20, 0xD4)
    Send-Bytes -Port $master -Data $broadcastFrame -Name "主机"

    Start-Sleep -Milliseconds 500

    $slaveResponse = Receive-Data -Port $slave -Name "从机" -TimeoutMs 1000

    if ($slaveResponse) {
        $found = $false
        for ($i = 0; $i -le ($slaveResponse.Length - $broadcastFrame.Length); $i++) {
            $match = $true
            for ($j = 0; $j -lt $broadcastFrame.Length; $j++) {
                if ($slaveResponse[$i + $j] -ne $broadcastFrame[$j]) {
                    $match = $false
                    break
                }
            }
            if ($match) {
                $found = $true
                break
            }
        }

        if ($found) {
            Write-Host "✓ 从机收到广播帧" -ForegroundColor Green
        }
        else {
            Write-Host "✗ 从机接收到数据，但不是预期的广播帧" -ForegroundColor Red
        }
    }
    else {
        Write-Host "✗ 从机未收到广播帧" -ForegroundColor Red
    }

    Start-Sleep -Seconds 1

    # 测试4: 主机发送单播帧到从机1
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "测试4: 主机发送单播帧 (目标0x01)" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan

    Clear-Buffer $master
    Clear-Buffer $slave

    $unicastFrame = [byte[]](0xA5, 0x04, 0x01, 0x02, 0x11, 0x21, 0xD9)
    Send-Bytes -Port $master -Data $unicastFrame -Name "主机"

    Start-Sleep -Milliseconds 500

    $slaveResponse = Receive-Data -Port $slave -Name "从机" -TimeoutMs 1000

    if ($slaveResponse) {
        Write-Host "✓ 从机收到单播帧" -ForegroundColor Green

        # 等待ACK
        Start-Sleep -Milliseconds 500
        $masterResponse = Receive-Data -Port $master -Name "主机" -TimeoutMs 1000

        if ($masterResponse -and $masterResponse[0] -eq 0xA5) {
            Write-Host "✓ 主机收到ACK应答" -ForegroundColor Green
        }
        else {
            Write-Host "✗ 主机未收到ACK应答" -ForegroundColor Red
        }
    }
    else {
        Write-Host "✗ 从机未收到单播帧" -ForegroundColor Red
    }

    Start-Sleep -Seconds 1

    # 测试5: 从机发送到主机
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "测试5: 从机发送单播帧 (目标0x02)" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan

    Clear-Buffer $master
    Clear-Buffer $slave

    $slaveToMaster = [byte[]](0xA5, 0x04, 0x02, 0x01, 0x12, 0x22, 0xDC)
    Send-Bytes -Port $slave -Data $slaveToMaster -Name "从机"

    Start-Sleep -Milliseconds 500

    $masterResponse = Receive-Data -Port $master -Name "主机" -TimeoutMs 1000

    if ($masterResponse) {
        Write-Host "✓ 主机收到从机数据" -ForegroundColor Green
    }
    else {
        Write-Host "✗ 主机未收到从机数据" -ForegroundColor Red
    }

    # 测试总结
    Write-Host ""
    Write-Host "============================================================" -ForegroundColor Cyan
    Write-Host "测试完成" -ForegroundColor Cyan
    Write-Host "============================================================" -ForegroundColor Cyan
}
catch {
    Write-Host ""
    Write-Host "✗ 测试出错: $_" -ForegroundColor Red
}
finally {
    Write-Host ""
    Write-Host "正在关闭串口..." -ForegroundColor White
    Close-SerialPort $master
    Close-SerialPort $slave
    Write-Host "测试结束" -ForegroundColor White
}
