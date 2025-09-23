#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
虚拟设备模拟器
基于ESP32设备的MQTT通信协议，模拟各种IoT设备的行为
"""

import json
import time
import random
import threading
import paho.mqtt.client as mqtt
from abc import ABC, abstractmethod
from typing import Dict, Any, Optional
import logging

# 配置日志
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')

class BaseVirtualDevice(ABC):
    """虚拟设备基类"""
    
    def __init__(self, device_id: str, device_type: str, mqtt_broker: str = "localhost", mqtt_port: int = 1883):
        self.device_id = device_id
        self.device_type = device_type
        self.mqtt_broker = mqtt_broker
        self.mqtt_port = mqtt_port
        
        # MQTT客户端
        self.client = mqtt.Client()
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        
        # 设备属性
        self.properties = {
            "device_type": {"value": device_type, "readable": True, "writeable": False},
            "sleep_time": {"value": 7200, "readable": True, "writeable": True},
            "battery": {"value": random.randint(80, 100), "readable": True, "writeable": False}
        }
        
        # MQTT主题
        self.publish_topic = f"/dpub/{device_id}"
        self.subscribe_topic = f"/drecv/{device_id}"
        
        # 运行状态
        self.running = False
        self.last_msg_time = time.time()
        
        # 日志记录器
        self.logger = logging.getLogger(f"{device_type}_{device_id}")
        
        # 心跳任务线程
        self.heartbeat_thread = None
        self.battery_thread = None
        
    def _on_connect(self, client, userdata, flags, rc):
        """MQTT连接回调"""
        if rc == 0:
            self.logger.info(f"Connected to MQTT broker")
            client.subscribe(self.subscribe_topic)
            self.logger.info(f"Subscribed to {self.subscribe_topic}")
            # 订阅公共topic
            client.subscribe("/all")
            self.logger.info(f"Subscribed to /all")
        else:
            self.logger.error(f"Failed to connect to MQTT broker, return code {rc}")
    
    def _on_message(self, client, userdata, msg):
        """MQTT消息接收回调"""
        try:
            topic = msg.topic
            payload = msg.payload.decode('utf-8')
            self.logger.info(f"Received message on {topic}: {payload}")
            
            data = json.loads(payload)
            self.last_msg_time = time.time()
            
            self._process_mqtt_message(data)
            
        except Exception as e:
            self.logger.error(f"Error processing message: {e}")
    
    def _process_mqtt_message(self, data: Dict[str, Any]):
        """处理MQTT消息"""
        method = data.get("method")
        msg_id = data.get("msg_id", -1)
        
        if method == "set":
            key = data.get("key")
            value = data.get("value")
            if key and key in self.properties and self.properties[key]["writeable"]:
                old_value = self.properties[key]["value"]
                self.properties[key]["value"] = value
                self.logger.info(f"Property {key} changed from {old_value} to {value}")
                self._on_property_changed(key, value, msg_id)
                
        elif method == "get":
            key = data.get("key")
            if key and key in self.properties:
                self._send_property_response(key, msg_id)
                
        elif method == "update":
            for key, value in data.items():
                if key not in ["method", "msg_id"] and key in self.properties and self.properties[key]["writeable"]:
                    old_value = self.properties[key]["value"]
                    self.properties[key]["value"] = value
                    self.logger.info(f"Property {key} updated from {old_value} to {value}")
                    self._on_property_changed(key, value, msg_id)
        else:
            # 处理动作
            self._on_action(data)
    
    def _send_property_response(self, property_name: str, msg_id: int):
        """发送属性响应"""
        if property_name in self.properties:
            response = {
                "method": "update",
                "msg_id": msg_id,
                "key": property_name,
                "value": self.properties[property_name]["value"]
            }
            self._publish_message(response)
    
    def _publish_message(self, data: Dict[str, Any]):
        """发布MQTT消息"""
        try:
            message = json.dumps(data)
            self.client.publish(self.publish_topic, message, qos=1)
            self.logger.debug(f"Published: {message}")
        except Exception as e:
            self.logger.error(f"Error publishing message: {e}")
    
    def _heartbeat_task(self):
        """心跳任务 - 定期上报所有属性"""
        while self.running:
            try:
                report_data = {"method": "report"}
                for prop_name, prop_info in self.properties.items():
                    if prop_info["readable"]:
                        report_data[prop_name] = prop_info["value"]
                
                self._publish_message(report_data)
                time.sleep(10)  # 每10秒上报一次
                
            except Exception as e:
                self.logger.error(f"Error in heartbeat task: {e}")
                time.sleep(1)
    
    def _battery_simulation_task(self):
        """电池电量模拟任务"""
        while self.running:
            try:
                # 模拟电池缓慢消耗
                current_battery = self.properties["battery"]["value"]
                if current_battery > 0:
                    # 每分钟随机消耗0-1%的电量
                    if random.random() < 0.1:  # 10%的概率消耗电量
                        self.properties["battery"]["value"] = max(0, current_battery - 1)
                
                time.sleep(60)  # 每分钟检查一次
                
            except Exception as e:
                self.logger.error(f"Error in battery simulation: {e}")
                time.sleep(1)
    
    def start(self):
        """启动虚拟设备"""
        try:
            self.logger.info(f"Starting {self.device_type} device {self.device_id}")
            self.client.connect(self.mqtt_broker, self.mqtt_port, 60)
            self.client.loop_start()
            
            self.running = True
            
            # 启动心跳任务
            self.heartbeat_thread = threading.Thread(target=self._heartbeat_task, daemon=True)
            self.heartbeat_thread.start()
            
            # 启动电池模拟任务
            self.battery_thread = threading.Thread(target=self._battery_simulation_task, daemon=True)
            self.battery_thread.start()
            
            # 调用设备特定的初始化
            self._device_init()
            
            self.logger.info(f"Device {self.device_id} started successfully")
            
        except Exception as e:
            self.logger.error(f"Error starting device: {e}")
    
    def stop(self):
        """停止虚拟设备"""
        self.logger.info(f"Stopping device {self.device_id}")
        self.running = False
        
        if self.heartbeat_thread:
            self.heartbeat_thread.join(timeout=1)
        if self.battery_thread:
            self.battery_thread.join(timeout=1)
            
        self.client.loop_stop()
        self.client.disconnect()
    
    def send_action(self, action: str, **kwargs):
        """发送动作消息"""
        action_data = {
            "method": "action",
            "action": action
        }
        action_data.update(kwargs)
        self._publish_message(action_data)
    
    @abstractmethod
    def _device_init(self):
        """设备特定的初始化"""
        pass
    
    @abstractmethod
    def _on_property_changed(self, property_name: str, value: Any, msg_id: int):
        """属性变化回调"""
        pass
    
    @abstractmethod
    def _on_action(self, data: Dict[str, Any]):
        """动作处理回调"""
        pass


class TD01Device(BaseVirtualDevice):
    """TD01设备 - 单路调光插座"""
    
    def __init__(self, device_id: str, **kwargs):
        super().__init__(device_id, "TD01", **kwargs)
        
        # TD01特有属性
        self.properties.update({
            "power": {"value": 0, "readable": True, "writeable": True}   # 0-255
        })
        
        # 按键模拟线程
        self.button_thread = None
    
    def _device_init(self):
        """TD01设备初始化"""
        self.logger.info("TD01 device initialized with power=0")
        
        # GUI控制模式下不启动自动按键模拟
        # self.button_thread = threading.Thread(target=self._button_simulation_task, daemon=True)
        # self.button_thread.start()
    
    def _on_property_changed(self, property_name: str, value: Any, msg_id: int):
        """TD01属性变化处理"""
        if property_name == "power":
            self.logger.info(f"Power set to {value} (PWM duty cycle)")
            # 模拟PWM控制
    
    def _on_action(self, data: Dict[str, Any]):
        """TD01动作处理"""
        pass
    
    def _button_simulation_task(self):
        """按键模拟任务"""
        while self.running:
            try:
                # 随机模拟按键点击 (每30-120秒随机触发一次)
                time.sleep(random.randint(30, 120))
                if self.running:
                    self.send_action("key_boot_clicked")
                    self.logger.info("Simulated button click")
                    
            except Exception as e:
                self.logger.error(f"Error in button simulation: {e}")


class DianjiDevice(BaseVirtualDevice):
    """电击设备"""
    
    def __init__(self, device_id: str, **kwargs):
        super().__init__(device_id, "DIANJI", **kwargs)
        
        # 电击设备特有属性
        self.properties.update({
            "voltage": {"value": 0, "readable": True, "writeable": True},     # 目标电压
            "delay": {"value": 5, "readable": True, "writeable": True},       # 延时
            "shock": {"value": 0, "readable": True, "writeable": True}       # 电击开关
        })
        
        # 内部状态
        self.current_voltage = 0.0
        self.target_voltage = 0.0
        self.voltage_thread = None
    
    def _device_init(self):
        """电击设备初始化"""
        self.logger.info("DIANJI device initialized")
        
        # 启动电压控制任务
        self.voltage_thread = threading.Thread(target=self._voltage_control_task, daemon=True)
        self.voltage_thread.start()
    
    def _on_property_changed(self, property_name: str, value: Any, msg_id: int):
        """电击设备属性变化处理"""
        if property_name == "voltage":
            self.target_voltage = float(value)
            self.logger.info(f"Target voltage set to {value}V")
        elif property_name == "shock":
            if value:
                self.logger.warning("SHOCK ACTIVATED!")
            else:
                self.logger.info("Shock deactivated")
        elif property_name == "delay":
            self.logger.info(f"Delay set to {value}s")
    
    def _on_action(self, data: Dict[str, Any]):
        """电击设备动作处理"""
        pass
    
    def _voltage_control_task(self):
        """电压控制任务 - 模拟PID控制"""
        while self.running:
            try:
                # 模拟电压逐渐调整到目标值
                if abs(self.current_voltage - self.target_voltage) > 0.1:
                    if self.current_voltage < self.target_voltage:
                        self.current_voltage += 0.5
                    else:
                        self.current_voltage -= 0.5
                
                # 更新电压属性
                self.properties["voltage"]["value"] = int(self.current_voltage)
                
                # 模拟电池电压变化
                bat_voltage = 3.7 + random.uniform(-0.2, 0.2)
                
                time.sleep(1)
                
            except Exception as e:
                self.logger.error(f"Error in voltage control: {e}")


class PJ01Device(BaseVirtualDevice):
    """PJ01 PWM电机控制设备"""
    
    def __init__(self, device_id: str, **kwargs):
        super().__init__(device_id, "PJ01", **kwargs)
        
        # PJ01设备特有属性
        self.properties.update({
            "pwm_duty": {"value": 0, "readable": True, "writeable": True},  # PWM占空比 (0-1023)
        })
        
        # 移除电池属性，因为PJ01设备没有电池
        if "battery" in self.properties:
            del self.properties["battery"]
        
        # 内部状态
        self.current_pwm_duty = 0
        self.pwm_thread = None
    
    def _device_init(self):
        """PJ01设备初始化"""
        self.logger.info("PJ01 PWM motor control device initialized")
        
        # 启动PWM控制任务
        self.pwm_thread = threading.Thread(target=self._pwm_control_task, daemon=True)
        self.pwm_thread.start()
    
    def _on_property_changed(self, property_name: str, value: Any, msg_id: int):
        """PJ01设备属性变化处理"""
        if property_name == "pwm_duty":
            if 0 <= value <= 1023:
                self.current_pwm_duty = int(value)
                self.logger.info(f"PWM duty set to {value} (motor speed: {value/1023*100:.1f}%)")
            else:
                self.logger.error(f"Invalid PWM duty value: {value} (should be 0-1023)")
    
    def _on_action(self, data: Dict[str, Any]):
        """PJ01设备动作处理"""
        method = data.get("method")
        if method == "motor_control":
            duty = data.get("duty", 0)
            if 0 <= duty <= 1023:
                self.properties["pwm_duty"]["value"] = duty
                self.current_pwm_duty = duty
                self.logger.info(f"Motor control action: PWM duty set to {duty}")
            else:
                self.logger.error(f"Invalid motor control duty: {duty}")
    
    def _pwm_control_task(self):
        """PWM控制任务 - 模拟电机控制"""
        while self.running:
            try:
                # 更新PWM占空比属性
                self.properties["pwm_duty"]["value"] = self.current_pwm_duty
                
                # 模拟电机运行状态日志
                if self.current_pwm_duty > 0:
                    motor_speed_percent = self.current_pwm_duty / 1023 * 100
                    if motor_speed_percent > 80:
                        self.logger.debug(f"Motor running at high speed: {motor_speed_percent:.1f}%")
                    elif motor_speed_percent > 30:
                        self.logger.debug(f"Motor running at medium speed: {motor_speed_percent:.1f}%")
                    else:
                        self.logger.debug(f"Motor running at low speed: {motor_speed_percent:.1f}%")
                
                time.sleep(2)  # 每2秒更新一次
                
            except Exception as e:
                self.logger.error(f"Error in PWM control: {e}")
    
    def _battery_simulation_task(self):
        """重写电池模拟任务 - PJ01设备没有电池"""
        # PJ01设备没有电池，所以不需要电池模拟
        pass


class ZidongsuoDevice(BaseVirtualDevice):
    """自动锁设备"""
    
    def __init__(self, device_id: str, **kwargs):
        super().__init__(device_id, "ZIDONGSUO", **kwargs)
        
        # 自动锁特有属性
        self.properties.update({
            "open": {"value": 0, "readable": True, "writeable": True}  # 0=关闭, 1=打开
        })
        
        # 按键模拟线程
        self.button_thread = None
    
    def _device_init(self):
        """自动锁设备初始化"""
        self.logger.info("ZIDONGSUO device initialized - lock closed")
        
        # GUI控制模式下不启动自动按键模拟
        # self.button_thread = threading.Thread(target=self._button_simulation_task, daemon=True)
        # self.button_thread.start()
    
    def _on_property_changed(self, property_name: str, value: Any, msg_id: int):
        """自动锁属性变化处理"""
        if property_name == "open":
            if value:
                self.logger.info("Lock OPENED - Servo angle: 0°, LED OFF")
            else:
                self.logger.info("Lock CLOSED - Servo angle: 180°, LED ON")
    
    def _on_action(self, data: Dict[str, Any]):
        """自动锁动作处理"""
        pass
    
    def _button_simulation_task(self):
        """按键模拟任务"""
        while self.running:
            try:
                # 随机模拟按键点击 (每60-300秒随机触发一次)
                time.sleep(random.randint(60, 300))
                if self.running:
                    self.send_action("key_clicked")
                    self.logger.info("Simulated button click")
                    
            except Exception as e:
                self.logger.error(f"Error in button simulation: {e}")


class QiyaDevice(BaseVirtualDevice):
    """气压检测设备"""
    
    def __init__(self, device_id: str, **kwargs):
        super().__init__(device_id, "QIYA", **kwargs)
        
        # 气压设备特有属性
        self.properties.update({
            "pressure": {"value": 1013.25, "readable": True, "writeable": False},
            "temperature": {"value": 25.0, "readable": True, "writeable": False},
            "report_interval": {"value": 5000, "readable": True, "writeable": True}
        })
        
        # 内部状态
        self.current_pressure = self.properties["pressure"]["value"]
        self.current_temperature = self.properties["temperature"]["value"]
        
        # 任务线程
        self.pressure_thread = None
        
    def _device_init(self):
        """设备初始化"""
        self.logger.info("Initializing QIYA pressure sensor device")
        
        # 启动气压数据模拟任务
        self.pressure_thread = threading.Thread(target=self._pressure_simulation_task, daemon=True)
        self.pressure_thread.start()
        
    def _on_property_changed(self, property_name: str, value: Any, msg_id: int):
        """属性变化处理"""
        if property_name == "report_interval":
            self.logger.info(f"Report interval changed to {value}ms")
            
    def _on_action(self, data: Dict[str, Any]):
        """动作处理"""
        action = data.get("action")
        if action == "calibrate":
            self.logger.info("Calibrating pressure sensor")
            # 模拟校准过程
            self.current_pressure = 1013.25
            self.properties["pressure"]["value"] = self.current_pressure
            
    def _pressure_simulation_task(self):
        """气压数据模拟任务"""
        while self.running:
            try:
                # 模拟气压变化 (1000-1030 hPa)
                pressure_change = random.uniform(-2.0, 2.0)
                self.current_pressure = max(1000.0, min(1030.0, self.current_pressure + pressure_change))
                
                # 模拟温度变化 (15-35°C)
                temp_change = random.uniform(-1.0, 1.0)
                self.current_temperature = max(15.0, min(35.0, self.current_temperature + temp_change))
                
                # 更新属性
                self.properties["pressure"]["value"] = round(self.current_pressure, 2)
                self.properties["temperature"]["value"] = round(self.current_temperature, 1)
                
                # 发布数据
                report_data = {
                    "properties": {
                        "pressure": self.properties["pressure"]["value"],
                        "temperature": self.properties["temperature"]["value"]
                    }
                }
                self._publish_message(report_data)
                
                # 等待报告间隔
                time.sleep(self.properties["report_interval"]["value"] / 1000.0)
                
            except Exception as e:
                self.logger.error(f"Error in pressure simulation: {e}")
                time.sleep(1)


class QTZDevice(BaseVirtualDevice):
    """QTZ设备 - VL6180X激光距离传感器 + 双按钮"""
    
    def __init__(self, device_id: str, **kwargs):
        super().__init__(device_id, "QTZ", **kwargs)
        
        # QTZ特有属性
        self.properties.update({
            "distance": {"value": 100.0, "readable": True, "writeable": False},        # 当前距离(mm)
            "report_delay_ms": {"value": 10000, "readable": True, "writeable": True}, # 上报间隔
            "low_band": {"value": 60, "readable": True, "writeable": True},          # 低阈值
            "high_band": {"value": 150, "readable": True, "writeable": True},        # 高阈值
            "button0": {"value": 0, "readable": True, "writeable": False},        # 按钮0状态 (0=释放, 1=按下)
            "button1": {"value": 0, "readable": True, "writeable": False}         # 按钮1状态 (0=释放, 1=按下)
        })
        
        # 内部状态
        self.distance_thread = None
        self.report_thread = None
        self.button_thread = None
        self.current_distance = 100.0
        self.average_distance = 100.0
        self.low_state_flag = False
        self.high_state_flag = False
        
        # VL6180X传感器特性
        self.sensor_noise = 2.0  # 传感器噪声
        self.measurement_range = (10, 500)  # VL6180X测量范围10-500mm
    
    def _device_init(self):
        """QTZ设备初始化"""
        self.logger.info("QTZ device initialized - VL6180X sensor and buttons ready")
        
        # 启动距离检测任务
        self.distance_thread = threading.Thread(target=self._distance_detection_task, daemon=True)
        self.distance_thread.start()
        
        # 启动距离上报任务（模拟qtz.c中的report_distance_task）
        self.report_thread = threading.Thread(target=self._distance_report_task, daemon=True)
        self.report_thread.start()
        
        # GUI控制模式下不启动自动按键模拟
        # self.button_thread = threading.Thread(target=self._button_simulation_task, daemon=True)
        # self.button_thread.start()
    
    def _on_property_changed(self, property_name: str, value: Any, msg_id: int):
        """QTZ属性变化处理"""
        if property_name == "low_band":
            self.logger.info(f"Low threshold set to {value}mm")
        elif property_name == "high_band":
            self.logger.info(f"High threshold set to {value}mm")
        elif property_name == "report_delay_ms":
            self.logger.info(f"Report delay set to {value}ms")
        elif property_name == "button0":
            state = "pressed" if value else "released"
            self.logger.info(f"Button 0 {state}")
        elif property_name == "button1":
            state = "pressed" if value else "released"
            self.logger.info(f"Button 1 {state}")
    
    def _on_action(self, data: Dict[str, Any]):
        """QTZ动作处理"""
        method = data.get("method")
        if method == "low":
            self.logger.warning(f"Distance LOW event triggered - current distance: {self.current_distance:.1f}mm")
        elif method == "high":
            self.logger.warning(f"Distance HIGH event triggered - current distance: {self.current_distance:.1f}mm")
        elif method == "button0_pressed":
            self.logger.info("Button 0 pressed event")
        elif method == "button0_released":
            self.logger.info("Button 0 released event")
        elif method == "button1_pressed":
            self.logger.info("Button 1 pressed event")
        elif method == "button1_released":
            self.logger.info("Button 1 released event")
    
    def _distance_detection_task(self):
        """VL6180X距离检测任务"""
        while self.running:
            try:
                # 模拟VL6180X传感器的距离变化
                # 添加传感器噪声和更真实的变化模式
                noise = random.uniform(-self.sensor_noise, self.sensor_noise)
                
                # 模拟物体移动：缓慢变化 + 偶尔快速变化
                if random.random() < 0.05:  # 5%概率快速变化（物体快速移动）
                    self.current_distance += random.uniform(-50, 50)
                else:  # 95%概率缓慢变化
                    self.current_distance += random.uniform(-2, 2)
                
                # 限制在VL6180X测量范围内
                self.current_distance = max(self.measurement_range[0], 
                                          min(self.measurement_range[1], self.current_distance))
                
                # 添加传感器噪声
                measured_distance = self.current_distance + noise
                measured_distance = max(self.measurement_range[0], 
                                      min(self.measurement_range[1], measured_distance))
                
                # 计算平均值（模拟C代码中的滤波）
                self.average_distance = 0.7 * self.average_distance + 0.3 * measured_distance
                
                # 更新距离属性
                self.properties["distance"]["value"] = round(measured_distance, 1)
                
                # 检查阈值触发（使用平均值，如C代码中的逻辑）
                low_band = self.properties["low_band"]["value"]
                high_band = self.properties["high_band"]["value"]
                
                # 低阈值检测（带滞回，防止抖动）
                if not self.low_state_flag:
                    if self.average_distance < low_band:
                        self.low_state_flag = True
                        self._publish_message({"method": "low"})
                        self.logger.warning(f"Distance LOW triggered: avg={self.average_distance:.1f}mm < {low_band}mm")
                else:
                    if self.average_distance > low_band + 10:  # 滞回10mm
                        self.low_state_flag = False
                        self.logger.info(f"Distance LOW cleared: avg={self.average_distance:.1f}mm > {low_band + 10}mm")
                
                # 高阈值检测（带滞回，防止抖动）
                if not self.high_state_flag:
                    if self.average_distance > high_band:
                        self.high_state_flag = True
                        self._publish_message({"method": "high"})
                        self.logger.warning(f"Distance HIGH triggered: avg={self.average_distance:.1f}mm > {high_band}mm")
                else:
                    if self.average_distance < high_band - 10:  # 滞回10mm
                        self.high_state_flag = False
                        self.logger.info(f"Distance HIGH cleared: avg={self.average_distance:.1f}mm < {high_band - 10}mm")
                
                # VL6180X传感器100ms读取间隔
                time.sleep(0.1)
                
            except Exception as e:
                 self.logger.error(f"Error in distance detection: {e}")
                 time.sleep(1)
    
    def _distance_report_task(self):
        """距离上报任务 - 模拟qtz.c中的report_distance_task"""
        while self.running:
            try:
                # 上报距离属性（模拟get_property("distance", 0)）
                self._send_property_response("distance", 0)
                
                # 按照report_delay_ms间隔等待
                report_delay_ms = self.properties["report_delay_ms"]["value"]
                time.sleep(report_delay_ms / 1000.0)  # 转换为秒
                
            except Exception as e:
                self.logger.error(f"Error in distance report: {e}")
                time.sleep(1)
    
    def _button_simulation_task(self):
        """按钮模拟任务 - 随机触发按钮事件"""
        while self.running:
            try:
                wait_time = 5
                time.sleep(wait_time)
                
                if not self.running:
                    break
                
                # 随机选择按钮（IO2或IO3）
                button_name = random.choice(["button0", "button1"])
                button_gpio = "0" if button_name == "button0" else "1"
                
                # 模拟按钮按下
                self.properties[button_name]["value"] = 1
                self.logger.info(f"Button {button_gpio} pressed (simulated)")
                self._send_property_response(button_name, 0)
                
                press_duration = 5
                time.sleep(press_duration)
                
                # 模拟按钮释放
                self.properties[button_name]["value"] = 0
                self.logger.info(f"Button {button_gpio} released (simulated)")
                self._send_property_response(button_name, 0)
                
            except Exception as e:
                self.logger.error(f"Error in button simulation: {e}")
                time.sleep(1)


if __name__ == "__main__":
    # 测试代码
    devices = []
    
    try:
        # 创建各种设备实例
        td01 = TD01Device("td01001aabbc")
        dianji = DianjiDevice("dianji001aab")
        zidongsuo = ZidongsuoDevice("zidongsuo001")
        qtz = QTZDevice("qtz001aabbcc")
        pj01 = PJ01Device("pj01001aabbcc")
        qiya = QiyaDevice("qiya001aabbcc")

        devices = [td01, dianji, zidongsuo, qtz, pj01, qiya]
        
        # 启动所有设备
        for device in devices:
            device.start()
            time.sleep(1)  # 错开启动时间
        
        print("All virtual devices started. Press Ctrl+C to stop.")
        
        # 保持运行
        while True:
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\nStopping all devices...")
        for device in devices:
            device.stop()
        print("All devices stopped.")