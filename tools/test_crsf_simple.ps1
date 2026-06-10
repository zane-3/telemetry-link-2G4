# CRSF Serial Port Test Script
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "CRSF Serial Data Test" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$portName = "COM6"
$baudRate = 420000

try {
    Write-Host "Opening port: $portName @ $baudRate bps" -ForegroundColor Yellow

    $port = New-Object System.IO.Ports.SerialPort $portName, $baudRate, None, 8, One
    $port.ReadTimeout = 1000
    $port.Open()

    Write-Host "Port opened - monitoring for 5 seconds..." -ForegroundColor Green
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

                # Try to parse CRSF frames
                while ($buffer.Count -ge 4) {
                    if ($buffer[0] -eq 0xC8) {
                        $frameSize = $buffer[1]
                        if ($buffer.Count -ge ($frameSize + 2)) {
                            $frameType = $buffer[2]
                            $frameCount++

                            $elapsed = ((Get-Date) - $startTime).TotalSeconds
                            Write-Host ("[{0:F2}s] Frame #{1}: Type=0x{2:X2}, Size={3}" -f $elapsed, $frameCount, $frameType, $frameSize) -ForegroundColor Green

                            # Remove processed frame
                            $buffer.RemoveRange(0, $frameSize + 2)
                        }
                        else {
                            break
                        }
                    }
                    else {
                        $buffer.RemoveAt(0)
                    }
                }
            }
            Start-Sleep -Milliseconds 10
        }
        catch {
            # Timeout or read error, continue
        }
    }

    $port.Close()

    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Test Complete!" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "Total bytes: $byteCount" -ForegroundColor White
    Write-Host "Total frames: $frameCount" -ForegroundColor White

    if ($frameCount -gt 0) {
        $frameRate = $frameCount / 5
        Write-Host ("Average frame rate: {0:F1} Hz" -f $frameRate) -ForegroundColor White
        Write-Host ""
        Write-Host "SUCCESS: CRSF output is working!" -ForegroundColor Green
    }
    else {
        Write-Host ""
        Write-Host "ERROR: No CRSF frames received" -ForegroundColor Red
        Write-Host "Possible causes:" -ForegroundColor Yellow
        Write-Host "  1. COM6 is not the correct port" -ForegroundColor Yellow
        Write-Host "  2. Device is not connected or not running" -ForegroundColor Yellow
        Write-Host "  3. Baud rate mismatch (current: $baudRate)" -ForegroundColor Yellow
    }

}
catch {
    Write-Host ""
    Write-Host "ERROR: $_" -ForegroundColor Red
    Write-Host ""
    Write-Host "Available ports:" -ForegroundColor Yellow
    [System.IO.Ports.SerialPort]::GetPortNames() | ForEach-Object {
        Write-Host "  - $_" -ForegroundColor White
    }
}
finally {
    if ($port -and $port.IsOpen) {
        $port.Close()
    }
}

Write-Host ""
