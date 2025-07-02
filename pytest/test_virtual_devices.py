#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
虚拟设备测试脚本
演示如何使用虚拟设备进行MQTT通信测试
"""

import time
import json
import threading
import paho.mqtt.client as mqtt
from virtual_devices import TD01Device, DianjiDevice, ZidongsuoDevice, QTZDevice
import logging

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
logger = logging.getLogger("TestController")

class DeviceTestController:
    """设备测试控制器"""
    
    def __init__(self, mqtt_broker="localhost", mqtt_port=1883):
        self.mqtt_broker = mqtt_broker
        self.mqtt_port = mqtt_port
        self.devices = []
        
        # MQTT客户端用于发送测试命令
        self.test_client = mqtt.Client()
        self.test_client.on_connect = self._on_test_connect
        self.test_client.on_message = self._on_test_message
        
        self.running = False
        
    def _on_test_connect(self, client, userdata, flags, rc):
        """测试客户端连接回调"""
        if rc == 0:
            logger.info("Test controller connected to MQTT broker")
            # 订阅所有设备的上报主题
            client.subscribe("/dpub/+")
            client.subscribe("/all")
        else:
            logger.error(f"Test controller failed to connect, return code {rc}")
    
    def _on_test_message(self, client, userdata, msg):
        """测试客户端消息接收回调"""
        try:
            topic = msg.topic
            payload = msg.payload.decode('utf-8')
            data = json.loads(payload)
            
            # 解析设备ID
            device_id = topic.split('/')[2] if topic.startswith('/dpub/') else 'unknown'
            logger.info(f"Received from {device_id}: {payload}")
            
        except Exception as e:
            logger.error(f"Error processing test message: {e}")
    
    def add_device(self, device):
        """添加设备"""
        self.devices.append(device)
    
    def start_all_devices(self):
        """启动所有设备"""
        logger.info("Starting all virtual devices...")
        
        # 连接测试客户端
        self.test_client.connect(self.mqtt_broker, self.mqtt_port, 60)
        self.test_client.loop_start()
        
        # 启动所有设备
        for device in self.devices:
            device.start()
            time.sleep(0.5)  # 错开启动时间
        
        self.running = True
        logger.info(f"Started {len(self.devices)} virtual devices")
    
    def stop_all_devices(self):
        """停止所有设备"""
        logger.info("Stopping all virtual devices...")
        self.running = False
        
        for device in self.devices:
            device.stop()
        
        self.test_client.loop_stop()
        self.test_client.disconnect()
        
        logger.info("All devices stopped")
    
    def send_command_to_device(self, device_id: str, command: dict):
        """向指定设备发送命令"""
        topic = f"/drecv/{device_id}"
        message = json.dumps(command)
        self.test_client.publish(topic, message, qos=1)
        logger.info(f"Sent command to {device_id}: {message}")
    
    def run_test_scenarios(self):
        """运行测试场景"""
        logger.info("Starting test scenarios...")
        
        # 等待设备启动完成
        time.sleep(3)
        
        # 测试场景1: TD01设备调光测试
        logger.info("=== Test Scenario 1: TD01 Dimming Test ===")
        self.send_command_to_device("td01_001", {
            "method": "set",
            "key": "power1",
            "value": 128,
            "msg_id": 1001
        })
        time.sleep(2)
        
        self.send_command_to_device("td01_001", {
            "method": "set",
            "key": "power2",
            "value": 200,
            "msg_id": 1002
        })
        time.sleep(3)
        
        # 测试场景2: 电击设备电压设置
        logger.info("=== Test Scenario 2: DIANJI Voltage Test ===")
        self.send_command_to_device("dianji_001", {
            "method": "set",
            "key": "voltage",
            "value": 50,
            "msg_id": 2001
        })
        time.sleep(2)
        
        self.send_command_to_device("dianji_001", {
            "method": "set",
            "key": "shock",
            "value": 1,
            "msg_id": 2002
        })
        time.sleep(3)
        
        self.send_command_to_device("dianji_001", {
            "method": "set",
            "key": "shock",
            "value": 0,
            "msg_id": 2003
        })
        time.sleep(2)
        
        # 测试场景3: 自动锁开关测试
        logger.info("=== Test Scenario 3: ZIDONGSUO Lock Test ===")
        self.send_command_to_device("zidongsuo_001", {
            "method": "set",
            "key": "open",
            "value": 1,
            "msg_id": 3001
        })
        time.sleep(3)
        
        self.send_command_to_device("zidongsuo_001", {
            "method": "set",
            "key": "open",
            "value": 0,
            "msg_id": 3002
        })
        time.sleep(2)
        
        # 测试场景4: QTZ距离传感器阈值设置
        logger.info("=== Test Scenario 4: QTZ Distance Sensor Test ===")
        self.send_command_to_device("qtz_001", {
            "method": "set",
            "key": "low_band",
            "value": 80,
            "msg_id": 4001
        })
        time.sleep(1)
        
        self.send_command_to_device("qtz_001", {
            "method": "set",
            "key": "high_band",
            "value": 120,
            "msg_id": 4002
        })
        time.sleep(2)
        
        # 测试场景5: 批量更新属性
        logger.info("=== Test Scenario 5: Batch Update Test ===")
        self.send_command_to_device("td01_001", {
            "method": "update",
            "power1": 255,
            "power2": 0,
            "sleep_time": 3600,
            "msg_id": 5001
        })
        time.sleep(2)
        
        # 测试场景6: 属性查询测试
        logger.info("=== Test Scenario 6: Property Query Test ===")
        for device_id in ["td01_001", "dianji_001", "zidongsuo_001", "qtz_001"]:
            self.send_command_to_device(device_id, {
                "method": "get",
                "key": "battery",
                "msg_id": 6000 + hash(device_id) % 1000
            })
            time.sleep(0.5)
        
        logger.info("Test scenarios completed")


def main():
    """主函数"""
    # 创建测试控制器
    controller = DeviceTestController()
    
    try:
        # 创建虚拟设备
        td01 = TD01Device("td01_001")
        dianji = DianjiDevice("dianji_001")
        zidongsuo = ZidongsuoDevice("zidongsuo_001")
        qtz = QTZDevice("qtz_001")
        
        # 添加到控制器
        controller.add_device(td01)
        controller.add_device(dianji)
        controller.add_device(zidongsuo)
        controller.add_device(qtz)
        
        # 启动所有设备
        controller.start_all_devices()
        
        # 运行测试场景
        test_thread = threading.Thread(target=controller.run_test_scenarios, daemon=True)
        test_thread.start()
        
        print("Virtual devices test started.")
        print("Devices running:")
        print("  - TD01 (td01_001): Dual-channel dimming socket")
        print("  - DIANJI (dianji_001): Electric shock device")
        print("  - ZIDONGSUO (zidongsuo_001): Automatic lock")
        print("  - QTZ (qtz_001): Ultrasonic distance sensor")
        print("\nMQTT Topics:")
        print("  - Subscribe to: /dpub/+ (device reports)")
        print("  - Subscribe to: /all (global broadcast)")
        print("  - Publish to: /drecv/{device_id} (device commands)")
        print("\nPress Ctrl+C to stop all devices.")
        
        # 保持运行
        while True:
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\nStopping test...")
        controller.stop_all_devices()
        print("Test stopped.")
    except Exception as e:
        logger.error(f"Error in main: {e}")
        controller.stop_all_devices()


if __name__ == "__main__":
    main()