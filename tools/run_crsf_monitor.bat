@echo off
echo ========================================
echo CRSF 接收机模拟器启动
echo ========================================
echo.

REM 检查 Python 是否安装
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 未找到 Python，请先安装 Python 3.x
    echo 下载地址: https://www.python.org/downloads/
    pause
    exit /b 1
)

echo [1/3] 检查 Python 环境...
python --version

echo.
echo [2/3] 安装依赖包...
pip install pyserial >nul 2>&1
if %errorlevel% neq 0 (
    echo [警告] pyserial 可能已安装或安装失败
)

echo.
echo [3/3] 启动 CRSF 接收机模拟器...
echo.
echo ========================================
echo 使用说明:
echo 1. 选择 COM6 (您的从站连接端口)
echo 2. 波特率选择 420000
echo 3. 点击"启动监控"
echo 4. 观察通道数据是否更新
echo ========================================
echo.

cd /d "%~dp0"
python crsf_receiver_ui.py

if %errorlevel% neq 0 (
    echo.
    echo [错误] 程序运行失败
    pause
)
