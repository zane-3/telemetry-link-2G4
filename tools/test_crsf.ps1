# CRSF 串口测试脚本
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "CRSF 串口数据测试" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$portName = "COM6"
$baudRate = 420000

try {
    Write-Host "打开串口: $portName @ $baudRate bps" -ForegroundColor Yellow

    $port = New-Object System.IO.Ports.SerialPort $portName, $baudRate, None, 8, One
    $port.ReadTimeout = 1000
    $port.Open()

    Write-Host "串口已打开 - 监控 5 秒..." -ForegroundColor Green
    Write-Host ""

    $startTime = Get-Date
    $frameCount = 0
    $byteCount = 0
    $buffer = New-Object System.Collections.ArrayList

    while (((Get-Date) - $startTime).TotalSeconds -lt 5) {
        try {
            if ($port.BytesToRead -gt 0) {
                $bytesToRead = $port.BytesToRead
                $bytes = New-Object byte[] $bytesToRead
                $port.Read($bytes, 0, $bytesToRead) | Out-Null
                $byteCount += $bytesToRead

                foreach ($b in $bytes) {
                    $buffer.Add($b) | Out-Null
                }

                # 尝试解析 CRSF 帧
                while ($buffer.Count -ge 4) {
                    if ($buffer[0] -eq 0xC8) {
                        $frameSize = $buffer[1]
                        if ($buffer.Count -ge ($frameSize + 2)) {
                            $frameType = $buffer[2]
                            $frameCount++

                            $elapsed = ((Get-Date) - $startTime).TotalSeconds
                            Write-Host ("[{0:F2}s] 帧 #{1}: 类型=0x{2:X2}, 大小={3}" -f $elapsed, $frameCount, $frameType, $frameSize) -ForegroundColor Green

                            # 移除已处理的帧
                            $buffer.RemoveRange(0, $frameSize + 2)
                        } else {
                            break
                        }
                    } else {
                        $buffer.RemoveAt(0)
                    }
                }
            }
            Start-Sleep -Milliseconds 10
        } catch {
            # 超时或读取错误，继续
        }
    }

    $port.Close()

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "测试完成！" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "总字节数: $byteCount" -ForegroundColor White
    Write-Host "总帧数: $frameCount" -ForegroundColor White

    if ($frameCount -gt 0) {
        $frameRate = $frameCount / 5
        Write-Host ("平均帧率: {0:F1} Hz" -f $frameRate) -ForegroundColor White
        Write-Host ""
        Write-Host "✓ CRSF 输出正常！从站工作正常。" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "✗ 未收到 CRSF 帧" -ForegroundColor Red
        Write-Host "可能原因:" -ForegroundColor Yellow
        Write-Host "  1. COM6 不是正确的端口" -ForegroundColor Yellow
        Write-Host "  2. 从站未连接或未运行" -ForegroundColor Yellow
        Write-Host "  3. 波特率不匹配 (当前: $baudRate)" -ForegroundColor Yellow
    }

} catch {
    Write-Host ""
    Write-Host "✗ 错误: $_" -ForegroundColor Red
    Write-Host ""
    Write-Host "可用串口:" -ForegroundColor Yellow
    [System.IO.Ports.SerialPort]::GetPortNames() | ForEach-Object {
        Write-Host "  - $_" -ForegroundColor White
    }
} finally {
    if ($port -and $port.IsOpen) {
        $port.Close()
    }
}

Write-Host ""
Write-Host "按任意键退出..."
$null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
