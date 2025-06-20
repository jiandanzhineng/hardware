#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
MQTT消息监控器
用于监听和显示所有虚拟设备的MQTT消息
"""

import json
import time
import paho.mqtt.client as mqtt
import logging
from datetime import datetime
from typing import Dict, Any

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger("MQTTMonitor")

class MQTTMonitor:
    """MQTT消息监控器"""
    
    def __init__(self, mqtt_broker="localhost", mqtt_port=1883):
        self.mqtt_broker = mqtt_broker
        self.mqtt_port = mqtt_port
        
        # MQTT客户端
        self.client = mqtt.Client()
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        
        # 消息统计
        self.message_count = 0
        self.device_stats = {}
        
        # 设备类型映射
        self.device_types = {
            "TD01": "双路调光插座",
            "DIANJI": "电击设备",
            "ZIDONGSUO": "自动锁",
            "QTZ": "超声波距离传感器"
        }
    
    def _on_connect(self, client, userdata, flags, rc):
        """MQTT连接回调"""
        if rc == 0:
            logger.info(f"Connected to MQTT broker at {self.mqtt_broker}:{self.mqtt_port}")
            # 订阅所有设备相关主题
            client.subscribe("device/+/report")
            client.subscribe("device/+/command")
            logger.info("Subscribed to device/+/report and device/+/command")
        else:
            logger.error(f"Failed to connect to MQTT broker, return code {rc}")
    
    def _on_message(self, client, userdata, msg):
        """MQTT消息接收回调"""
        try:
            topic = msg.topic
            payload = msg.payload.decode('utf-8')
            timestamp = datetime.now().strftime('%H:%M:%S')
            
            # 解析主题
            topic_parts = topic.split('/')
            if len(topic_parts) >= 3:
                device_id = topic_parts[1]
                message_type = topic_parts[2]  # report 或 command
            else:
                device_id = "unknown"
                message_type = "unknown"
            
            # 更新统计信息
            self.message_count += 1
            if device_id not in self.device_stats:
                self.device_stats[device_id] = {"report": 0, "command": 0}
            if message_type in self.device_stats[device_id]:
                self.device_stats[device_id][message_type] += 1
            
            # 解析JSON数据
            try:
                data = json.loads(payload)
                formatted_data = json.dumps(data, indent=2, ensure_ascii=False)
            except json.JSONDecodeError:
                formatted_data = payload
                data = {}
            
            # 获取设备类型描述
            device_type = data.get('device_type', 'Unknown')
            device_desc = self.device_types.get(device_type, device_type)
            
            # 格式化输出
            print(f"\n{'='*80}")
            print(f"[{timestamp}] {message_type.upper()} - {device_id} ({device_desc})")
            print(f"Topic: {topic}")
            
            if message_type == "report":
                self._format_report_message(data)
            elif message_type == "command":
                self._format_command_message(data)
            
            print(f"Raw Data:\n{formatted_data}")
            print(f"{'='*80}")
            
        except Exception as e:
            logger.error(f"Error processing message: {e}")
    
    def _format_report_message(self, data: Dict[str, Any]):
        """格式化设备上报消息"""
        method = data.get('method', '')
        if method == 'report':
            print("📊 Device Status Report:")
            
            # 显示关键属性
            key_props = ['device_type', 'battery', 'power1', 'power2', 'voltage', 'distance', 'open']
            for prop in key_props:
                if prop in data:
                    value = data[prop]
                    if prop == 'battery':
                        print(f"  🔋 Battery: {value}%")
                    elif prop == 'power1':
                        print(f"  ⚡ Power1: {value}/255 ({value/255*100:.1f}%)")
                    elif prop == 'power2':
                        print(f"  ⚡ Power2: {value}/255 ({value/255*100:.1f}%)")
                    elif prop == 'voltage':
                        print(f"  🔌 Voltage: {value}V")
                    elif prop == 'distance':
                        print(f"  📏 Distance: {value}mm")
                    elif prop == 'open':
                        status = "🔓 OPEN" if value else "🔒 CLOSED"
                        print(f"  🚪 Lock Status: {status}")
                    elif prop == 'device_type':
                        print(f"  📱 Device Type: {value}")
        
        elif method == 'action':
            action = data.get('action', 'unknown')
            print(f"🎬 Device Action: {action}")
            
            if action == 'key_boot_clicked':
                print("  👆 Boot button was pressed")
            elif action == 'key_clicked':
                print("  👆 Button was pressed")
            elif action == 'distance_low':
                distance = data.get('distance', 'unknown')
                print(f"  ⚠️  Distance LOW alert: {distance}mm")
            elif action == 'distance_high':
                distance = data.get('distance', 'unknown')
                print(f"  ⚠️  Distance HIGH alert: {distance}mm")
    
    def _format_command_message(self, data: Dict[str, Any]):
        """格式化设备命令消息"""
        method = data.get('method', '')
        msg_id = data.get('msg_id', 'N/A')
        
        print(f"📤 Command (ID: {msg_id}):")
        
        if method == 'set':
            key = data.get('key', 'unknown')
            value = data.get('value', 'unknown')
            print(f"  🔧 SET {key} = {value}")
            
        elif method == 'get':
            key = data.get('key', 'unknown')
            print(f"  📥 GET {key}")
            
        elif method == 'update':
            print(f"  🔄 BATCH UPDATE:")
            for key, value in data.items():
                if key not in ['method', 'msg_id']:
                    print(f"    • {key} = {value}")
        
        elif method == 'response':
            key = data.get('key', 'unknown')
            value = data.get('value', 'unknown')
            print(f"  📤 RESPONSE {key} = {value}")
    
    def start(self):
        """启动监控器"""
        try:
            logger.info("Starting MQTT monitor...")
            self.client.connect(self.mqtt_broker, self.mqtt_port, 60)
            self.client.loop_start()
            
            print(f"\n🚀 MQTT Monitor Started")
            print(f"📡 Broker: {self.mqtt_broker}:{self.mqtt_port}")
            print(f"📋 Monitoring topics: device/+/report, device/+/command")
            print(f"⏰ Started at: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
            print(f"\n{'='*80}")
            print("Waiting for messages... (Press Ctrl+C to stop)")
            
        except Exception as e:
            logger.error(f"Error starting monitor: {e}")
    
    def stop(self):
        """停止监控器"""
        logger.info("Stopping MQTT monitor...")
        self.client.loop_stop()
        self.client.disconnect()
        
        # 显示统计信息
        print(f"\n\n📊 Session Statistics:")
        print(f"Total messages: {self.message_count}")
        print(f"Devices monitored: {len(self.device_stats)}")
        
        for device_id, stats in self.device_stats.items():
            total = stats.get('report', 0) + stats.get('command', 0)
            print(f"  {device_id}: {total} messages (📊 {stats.get('report', 0)} reports, 📤 {stats.get('command', 0)} commands)")
        
        print("\n👋 Monitor stopped.")
    
    def print_help(self):
        """打印帮助信息"""
        print("\n📖 MQTT Monitor Help:")
        print("\nMessage Types:")
        print("  📊 REPORT - Device status reports (device/+/report)")
        print("  📤 COMMAND - Commands sent to devices (device/+/command)")
        print("\nDevice Types:")
        for code, desc in self.device_types.items():
            print(f"  {code} - {desc}")
        print("\nCommands:")
        print("  Ctrl+C - Stop monitoring")
        print("  h - Show this help")


def main():
    """主函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description='MQTT Device Monitor')
    parser.add_argument('--broker', default='localhost', help='MQTT broker address (default: localhost)')
    parser.add_argument('--port', type=int, default=1883, help='MQTT broker port (default: 1883)')
    parser.add_argument('--help-topics', action='store_true', help='Show help about topics and exit')
    
    args = parser.parse_args()
    
    if args.help_topics:
        print("\n📖 MQTT Topics Help:")
        print("\nSubscribed Topics:")
        print("  device/+/report - Device status reports and actions")
        print("  device/+/command - Commands sent to devices")
        print("\nExample Topics:")
        print("  device/td01_001/report - TD01 device reports")
        print("  device/td01_001/command - Commands to TD01 device")
        print("\nMessage Examples:")
        print("  Report: {'method': 'report', 'device_type': 'TD01', 'power1': 128, 'battery': 85}")
        print("  Command: {'method': 'set', 'key': 'power1', 'value': 255, 'msg_id': 1001}")
        return
    
    monitor = MQTTMonitor(args.broker, args.port)
    
    try:
        monitor.start()
        
        # 主循环
        while True:
            try:
                user_input = input().strip().lower()
                if user_input == 'h':
                    monitor.print_help()
                elif user_input == 'q':
                    break
            except EOFError:
                # 处理Ctrl+D
                break
            except KeyboardInterrupt:
                # 处理Ctrl+C
                break
            
    except KeyboardInterrupt:
        pass
    finally:
        monitor.stop()


if __name__ == "__main__":
    main()