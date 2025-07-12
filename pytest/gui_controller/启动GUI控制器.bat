@echo off
chcp 65001 >nul
echo ========================================
echo IoT虚拟设备GUI控制器
echo ========================================
echo.

:: 检查Python是否安装
python --version >nul 2>&1
if errorlevel 1 (
    echo 错误: 未找到Python，请先安装Python 3.7+
    echo 下载地址: https://www.python.org/downloads/
    pause
    exit /b 1
)

echo 检测到Python版本:
python --version
echo.

:: 切换到脚本目录
cd /d "%~dp0"

:: 检查虚拟环境
if exist "..\venv\Scripts\activate.bat" (
    echo 发现虚拟环境，正在激活...
    call ..\venv\Scripts\activate.bat
    echo.
)

:: 检查requirements.txt
if not exist "..\requirements.txt" (
    echo 警告: 未找到requirements.txt文件
    echo.
)

:: 启动GUI控制器
echo 正在启动GUI控制器...
echo.
python run_gui.py

:: 如果出错，显示错误信息
if errorlevel 1 (
    echo.
    echo ========================================
    echo 启动失败！
    echo ========================================
    echo 可能的解决方案:
    echo 1. 安装依赖: pip install -r requirements.txt
    echo 2. 检查MQTT代理是否运行
    echo 3. 确保所有Python文件完整
    echo.
    pause
)

echo.
echo 程序已退出
pause