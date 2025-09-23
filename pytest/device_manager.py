#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
设备管理器
用于单独启动和管理虚拟设备
"""

import json
import time
import threading
import argparse
from virtual_devices import TD01Device, DianjiDevice, ZidongsuoDevice, QTZDevice, PJ01Device, QiyaDevice, BaseVirtualDevice
import logging

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
logger = logging.getLogger("DeviceManager")

class DeviceManager:
    """设备管理器"""
    
    # 支持的设备类型
    DEVICE_TYPES = {
        'td01': TD01Device,
        'dianji': DianjiDevice,
        'zidongsuo': ZidongsuoDevice,
        'qtz': QTZDevice,
        'pj01': PJ01Device,
        'qiya': QiyaDevice
    }
    
    def __init__(self):
        self.devices = {}
        self.running = False
    
    def create_device(self, device_type: str, device_id: str, mqtt_broker: str = "localhost", mqtt_port: int = 1883) -> BaseVirtualDevice:
        """创建设备实例"""
        if device_type.lower() not in self.DEVICE_TYPES:
            raise ValueError(f"Unsupported device type: {device_type}. Supported types: {list(self.DEVICE_TYPES.keys())}")
        
        device_class = self.DEVICE_TYPES[device_type.lower()]
        device = device_class(device_id, mqtt_broker=mqtt_broker, mqtt_port=mqtt_port)
        
        return device
    
    def add_device(self, device: BaseVirtualDevice):
        """添加设备到管理器"""
        self.devices[device.device_id] = device
        logger.info(f"Added device {device.device_id} ({device.device_type})")
    
    def remove_device(self, device_id: str):
        """从管理器移除设备"""
        if device_id in self.devices:
            device = self.devices[device_id]
            if device.running:
                device.stop()
            del self.devices[device_id]
            logger.info(f"Removed device {device_id}")
        else:
            logger.warning(f"Device {device_id} not found")
    
    def start_device(self, device_id: str):
        """启动指定设备"""
        if device_id in self.devices:
            device = self.devices[device_id]
            if not device.running:
                device.start()
                logger.info(f"Started device {device_id}")
            else:
                logger.warning(f"Device {device_id} is already running")
        else:
            logger.error(f"Device {device_id} not found")
    
    def stop_device(self, device_id: str):
        """停止指定设备"""
        if device_id in self.devices:
            device = self.devices[device_id]
            if device.running:
                device.stop()
                logger.info(f"Stopped device {device_id}")
            else:
                logger.warning(f"Device {device_id} is not running")
        else:
            logger.error(f"Device {device_id} not found")
    
    def start_all_devices(self):
        """启动所有设备"""
        logger.info("Starting all devices...")
        for device_id, device in self.devices.items():
            if not device.running:
                device.start()
                time.sleep(0.5)  # 错开启动时间
        self.running = True
        logger.info(f"Started {len(self.devices)} devices")
    
    def stop_all_devices(self):
        """停止所有设备"""
        logger.info("Stopping all devices...")
        for device_id, device in self.devices.items():
            if device.running:
                device.stop()
        self.running = False
        logger.info("All devices stopped")
    
    def list_devices(self):
        """列出所有设备"""
        if not self.devices:
            print("No devices registered.")
            return
        
        print(f"\nRegistered Devices ({len(self.devices)}):")
        print("-" * 60)
        for device_id, device in self.devices.items():
            status = "🟢 Running" if device.running else "🔴 Stopped"
            print(f"{device_id:20} | {device.device_type:12} | {status}")
        print("-" * 60)
    
    def get_device_status(self, device_id: str):
        """获取设备状态"""
        if device_id in self.devices:
            device = self.devices[device_id]
            print(f"\nDevice Status: {device_id}")
            print(f"Type: {device.device_type}")
            print(f"Running: {device.running}")
            print(f"MQTT Broker: {device.mqtt_broker}:{device.mqtt_port}")
            print(f"Publish Topic: {device.publish_topic}")
            print(f"Subscribe Topic: {device.subscribe_topic}")
            
            print("\nProperties:")
            for prop_name, prop_info in device.properties.items():
                readable = "R" if prop_info["readable"] else "-"
                writeable = "W" if prop_info["writeable"] else "-"
                value = prop_info["value"]
                print(f"  {prop_name:15} | {readable}{writeable} | {value}")
        else:
            print(f"Device {device_id} not found")
    
    def interactive_mode(self):
        """交互模式"""
        print("\n🎮 Device Manager Interactive Mode")
        print("Type 'help' for available commands, 'quit' to exit.\n")
        
        while True:
            try:
                command = input("device_manager> ").strip().split()
                if not command:
                    continue
                
                cmd = command[0].lower()
                
                if cmd == 'help':
                    self._print_help()
                elif cmd == 'quit' or cmd == 'exit':
                    break
                elif cmd == 'list':
                    self.list_devices()
                elif cmd == 'create':
                    if len(command) >= 3:
                        device_type, device_id = command[1], command[2]
                        broker = command[3] if len(command) > 3 else "localhost"
                        port = int(command[4]) if len(command) > 4 else 1883
                        try:
                            device = self.create_device(device_type, device_id, broker, port)
                            self.add_device(device)
                            print(f"✅ Created device {device_id} ({device_type})")
                        except Exception as e:
                            print(f"❌ Error creating device: {e}")
                    else:
                        print("Usage: create <type> <id> [broker] [port]")
                elif cmd == 'start':
                    if len(command) >= 2:
                        device_id = command[1]
                        self.start_device(device_id)
                    else:
                        print("Usage: start <device_id>")
                elif cmd == 'stop':
                    if len(command) >= 2:
                        device_id = command[1]
                        self.stop_device(device_id)
                    else:
                        print("Usage: stop <device_id>")
                elif cmd == 'startall':
                    self.start_all_devices()
                elif cmd == 'stopall':
                    self.stop_all_devices()
                elif cmd == 'status':
                    if len(command) >= 2:
                        device_id = command[1]
                        self.get_device_status(device_id)
                    else:
                        print("Usage: status <device_id>")
                elif cmd == 'remove':
                    if len(command) >= 2:
                        device_id = command[1]
                        self.remove_device(device_id)
                        print(f"✅ Removed device {device_id}")
                    else:
                        print("Usage: remove <device_id>")
                else:
                    print(f"Unknown command: {cmd}. Type 'help' for available commands.")
                    
            except KeyboardInterrupt:
                print("\nUse 'quit' to exit.")
            except Exception as e:
                print(f"Error: {e}")
        
        # 退出时停止所有设备
        if self.running:
            self.stop_all_devices()
    
    def _print_help(self):
        """打印帮助信息"""
        print("\n📖 Available Commands:")
        print("  help                          - Show this help")
        print("  list                          - List all devices")
        print("  create <type> <id> [broker] [port] - Create a new device")
        print("  start <device_id>             - Start a device")
        print("  stop <device_id>              - Stop a device")
        print("  startall                      - Start all devices")
        print("  stopall                       - Stop all devices")
        print("  status <device_id>            - Show device status")
        print("  remove <device_id>            - Remove a device")
        print("  quit/exit                     - Exit the manager")
        print("\n📱 Supported Device Types:")
        for device_type in self.DEVICE_TYPES.keys():
            print(f"  {device_type}")
        print("\n💡 Examples:")
        print("  create td01 my_socket")
        print("  create dianji shock_device localhost 1883")
        print("  start my_socket")
        print("  status my_socket")


def main():
    """主函数"""
    parser = argparse.ArgumentParser(description='Virtual Device Manager')
    parser.add_argument('--interactive', '-i', action='store_true', help='Start in interactive mode')
    parser.add_argument('--type', '-t', choices=['td01', 'dianji', 'zidongsuo', 'qtz', 'pj01', 'qiya'], help='Device type')
    parser.add_argument('--id', '-d', help='Device ID')
    parser.add_argument('--broker', '-b', default='localhost', help='MQTT broker address')
    parser.add_argument('--port', '-p', type=int, default=1883, help='MQTT broker port')
    parser.add_argument('--auto-start', action='store_true', help='Auto start the device')
    
    args = parser.parse_args()
    
    manager = DeviceManager()
    
    try:
        if args.interactive:
            # 交互模式
            manager.interactive_mode()
        elif args.type and args.id:
            # 命令行模式 - 创建单个设备
            device = manager.create_device(args.type, args.id, args.broker, args.port)
            manager.add_device(device)
            
            if args.auto_start:
                manager.start_device(args.id)
                print(f"✅ Started {args.type} device '{args.id}'")
                print(f"📡 MQTT: {args.broker}:{args.port}")
                print(f"📤 Publish: {device.publish_topic}")
                print(f"📥 Subscribe: {device.subscribe_topic}")
                print("\nPress Ctrl+C to stop the device.")
                
                try:
                    while True:
                        time.sleep(1)
                except KeyboardInterrupt:
                    print("\nStopping device...")
                    manager.stop_device(args.id)
            else:
                print(f"✅ Created {args.type} device '{args.id}' (not started)")
                print("Use --auto-start to start immediately, or use interactive mode.")
        else:
            # 显示帮助
            parser.print_help()
            print("\n💡 Examples:")
            print("  # Interactive mode")
            print("  python device_manager.py -i")
            print("\n  # Create and start a TD01 device")
            print("  python device_manager.py -t td01 -d my_socket --auto-start")
            print("\n  # Create a device with custom MQTT broker")
            print("  python device_manager.py -t dianji -d shock_001 -b 192.168.1.100 -p 1883 --auto-start")
            
    except KeyboardInterrupt:
        print("\nExiting...")
        manager.stop_all_devices()
    except Exception as e:
        logger.error(f"Error in main: {e}")
        manager.stop_all_devices()


if __name__ == "__main__":
    main()