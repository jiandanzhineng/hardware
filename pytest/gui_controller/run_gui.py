#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
虚拟设备GUI控制器启动脚本
简化的启动入口，包含基本的环境检查和错误处理
"""

import sys
import os
import subprocess
import tkinter as tk
from tkinter import messagebox

def check_dependencies():
    """检查依赖项"""
    missing_deps = []
    
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        missing_deps.append("paho-mqtt")
    
    try:
        import tkinter
    except ImportError:
        missing_deps.append("tkinter (通常包含在Python标准库中)")
    
    return missing_deps

def check_mqtt_broker():
    """检查MQTT代理是否可用"""
    try:
        import paho.mqtt.client as mqtt
        import socket
        
        # 尝试连接到本地MQTT代理
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2)
        result = sock.connect_ex(('localhost', 1883))
        sock.close()
        
        return result == 0
    except Exception:
        return False

def install_dependencies():
    """安装缺失的依赖项"""
    try:
        subprocess.check_call([sys.executable, "-m", "pip", "install", "-r", "requirements.txt"])
        return True
    except subprocess.CalledProcessError:
        return False

def show_mqtt_help():
    """显示MQTT代理安装帮助"""
    help_text = """
MQTT代理未运行或不可用。

请安装并启动MQTT代理（推荐mosquitto）：

Windows:
1. 下载mosquitto: https://mosquitto.org/download/
2. 安装后启动服务
3. 或者使用命令: mosquitto -v

Linux (Ubuntu/Debian):
sudo apt-get install mosquitto mosquitto-clients
sudo systemctl start mosquitto

macOS:
brew install mosquitto
brew services start mosquitto

或者使用Docker:
docker run -it -p 1883:1883 eclipse-mosquitto

启动MQTT代理后，请重新运行此程序。
"""
    
    root = tk.Tk()
    root.withdraw()  # 隐藏主窗口
    messagebox.showinfo("MQTT代理帮助", help_text)
    root.destroy()

def main():
    """主函数"""
    print("IoT虚拟设备GUI控制器启动检查...")
    
    # 检查依赖项
    print("检查Python依赖项...")
    missing_deps = check_dependencies()
    
    if missing_deps:
        print(f"缺少依赖项: {', '.join(missing_deps)}")
        print("尝试自动安装...")
        
        if install_dependencies():
            print("依赖项安装成功！")
        else:
            print("依赖项安装失败，请手动运行: pip install -r requirements.txt")
            return
    else:
        print("依赖项检查通过！")
    
    # 检查MQTT代理
    print("检查MQTT代理连接...")
    if not check_mqtt_broker():
        print("警告: MQTT代理不可用 (localhost:1883)")
        print("设备将无法正常通信，但GUI仍可启动用于测试界面")
        
        # 询问用户是否继续
        root = tk.Tk()
        root.withdraw()
        
        result = messagebox.askyesnocancel(
            "MQTT代理不可用",
            "MQTT代理未运行或不可用。\n\n" +
            "选择操作:\n" +
            "• 是: 显示MQTT安装帮助\n" +
            "• 否: 仍然启动GUI (仅用于界面测试)\n" +
            "• 取消: 退出程序"
        )
        
        root.destroy()
        
        if result is None:  # 取消
            print("用户取消启动")
            return
        elif result:  # 是 - 显示帮助
            show_mqtt_help()
            return
        # 否 - 继续启动
        
    else:
        print("MQTT代理连接正常！")
    
    # 启动GUI控制器
    print("启动GUI控制器...")
    try:
        # 添加上级目录到Python路径，以便导入virtual_devices
        import sys
        import os
        parent_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        if parent_dir not in sys.path:
            sys.path.insert(0, parent_dir)
        
        from device_gui_controller import DeviceGUIController
        
        controller = DeviceGUIController()
        print("GUI控制器已启动，请在窗口中操作")
        controller.run()
        
    except ImportError as e:
        print(f"导入错误: {e}")
        print("请确保 device_gui_controller.py 文件存在")
    except Exception as e:
        print(f"启动失败: {e}")
        
        # 显示错误对话框
        root = tk.Tk()
        root.withdraw()
        messagebox.showerror("启动失败", f"GUI控制器启动失败:\n\n{str(e)}")
        root.destroy()

if __name__ == "__main__":
    main()