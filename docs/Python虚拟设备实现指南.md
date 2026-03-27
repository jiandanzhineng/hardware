# Python虚拟设备实现指南

本文基于项目中的文档与实现，说明如何用Python编写一个可通过MQTT与平台交互的“虚拟设备”。参考文件： [README.md](file:///e:/develop/smart/hard/project_td/pytest/README.md) 与 [virtual_devices.py](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py)。

## 目标
- 以最少代码实现一个可连接MQTT、上报属性、响应命令/动作的设备
- 复用通用基类，快速开发新设备类型

## 目录与依赖
- 代码位置：pytest目录
- 依赖：paho-mqtt、标准库
- 安装：在pytest目录执行
```bash
pip install -r requirements.txt
```

## 通信约定
- 设备上报主题：/dpub/{device_id}
- 设备命令主题：/drecv/{device_id}
- 全局广播主题：/all
- 消息方法：report、set、get、update、action
- 参考：README的“MQTT通信协议”和“消息格式”

## 基类结构
基类位置：[virtual_devices.py](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py)，关键点：
- 属性字典定义与读写权限 [properties](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py#L35-L39)
- 发布/订阅主题 [publish/subscribe](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py#L41-L44)
- 消息处理与方法分派 [_process_mqtt_message](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py#L83-L112)
- 属性响应 [_send_property_response](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py#L113-L123)
- 设备启动/停止 [start/stop](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py#L166-L190)
- 心跳上报与电池模拟 [heartbeat/battery](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py#L133-L165)
- 需实现的抽象方法：
  - _device_init [抽象声明](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py#L213-L216)
  - _on_property_changed [抽象声明](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py#L218-L221)
  - _on_action [抽象声明](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py#L223-L226)

## 开发步骤
- 定义设备类：继承BaseVirtualDevice，指定device_type
- 声明属性：在properties中加入设备特有属性，设置readable/writeable
- 初始化：实现_device_init，启动必要的后台任务（如传感器/控制线程）
- 属性变更：实现_on_property_changed，对set/update后的值执行设备动作或校正
- 动作处理：实现_on_action，响应自定义动作消息
- 启动设备：实例化并调用start，设备会自动连接MQTT、订阅主题并开始上报

## 最小示例
```python
from typing import Any, Dict
from virtual_devices import BaseVirtualDevice

class MyDevice(BaseVirtualDevice):
    def __init__(self, device_id: str, **kwargs):
        super().__init__(device_id, "MYDEVICE", **kwargs)
        self.properties.update({
            "power": {"value": 0, "readable": True, "writeable": True},
            "report_delay_ms": {"value": 1000, "readable": True, "writeable": True}
        })

    def _device_init(self):
        pass

    def _on_property_changed(self, property_name: str, value: Any, msg_id: int):
        if property_name == "power":
            self.properties["power"]["value"] = int(value)

    def _on_action(self, data: Dict[str, Any]):
        action = data.get("action")
        if action == "key_clicked":
            self.properties["power"]["value"] = 0
```

## 消息示例
- 设置属性（set）
```json
{"method":"set","key":"power","value":128,"msg_id":1001}
```
- 查询属性（get）
```json
{"method":"get","key":"power","msg_id":1002}
```
- 批量更新（update）
```json
{"method":"update","power":255,"report_delay_ms":500,"msg_id":1003}
```
- 设备动作（action）
```json
{"method":"action","action":"key_clicked"}
```

## 调试与测试
- 启动MQTT代理（Mosquitto等），或使用Docker快速启动
- 运行设备：python virtual_devices.py 或你的设备脚本
- 监控主题：/dpub/+、/all
- 发送命令：mosquitto_pub 发送到 /drecv/{device_id}
- 打开DEBUG日志可查看详细收发与处理

## 设计建议
- 属性只读/可写要明确，避免写入只读属性
- 后台任务用守护线程，依据running状态安全退出
- 报告周期等参数做基本边界处理，避免极端值
- 动作消息尽量简洁，设备内部自行完成状态更新与上报

## 参考
- 文档：[README.md](file:///e:/develop/smart/hard/project_td/pytest/README.md)
- 实现：[virtual_devices.py](file:///e:/develop/smart/hard/project_td/pytest/virtual_devices.py)
