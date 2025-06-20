# 虚拟设备测试系统

这个目录包含了基于ESP32设备MQTT通信协议的Python虚拟设备模拟器，用于测试IoT设备的行为和通信。

## 文件说明

- `virtual_devices.py` - 虚拟设备核心实现
- `test_virtual_devices.py` - 设备测试脚本
- `mqtt_monitor.py` - MQTT消息监控器
- `requirements.txt` - Python依赖包
- `README.md` - 本说明文档

## 支持的设备类型

### 1. TD01 - 双路调光插座
- **设备类型**: `TD01`
- **功能**: 双路PWM调光控制
- **属性**:
  - `power1`: 第一路功率 (0-255)
  - `power2`: 第二路功率 (0-255)
  - `device_type`: 设备类型 (只读)
  - `sleep_time`: 睡眠时间 (秒)
  - `battery`: 电池电量 (百分比，只读)
- **动作**: 
  - `key_boot_clicked`: 按键点击事件

### 2. DIANJI - 电击设备
- **设备类型**: `DIANJI`
- **功能**: 电压控制和电击功能
- **属性**:
  - `voltage`: 目标电压 (V)
  - `delay`: 延时设置 (秒)
  - `shock`: 电击开关 (0/1)
  - `device_type`: 设备类型 (只读)
  - `sleep_time`: 睡眠时间 (秒)
  - `battery`: 电池电量 (百分比，只读)

### 3. ZIDONGSUO - 自动锁
- **设备类型**: `ZIDONGSUO`
- **功能**: 舵机控制的自动锁
- **属性**:
  - `open`: 锁状态 (0=关闭, 1=打开)
  - `device_type`: 设备类型 (只读)
  - `sleep_time`: 睡眠时间 (秒)
  - `battery`: 电池电量 (百分比，只读)
- **动作**:
  - `key_clicked`: 按键点击事件

### 4. QTZ - 超声波距离传感器
- **设备类型**: `QTZ`
- **功能**: 距离检测和阈值报警
- **属性**:
  - `distance`: 当前距离 (mm，只读)
  - `report_delay_ms`: 上报间隔 (毫秒)
  - `low_band`: 低阈值 (mm)
  - `high_band`: 高阈值 (mm)
  - `device_type`: 设备类型 (只读)
  - `sleep_time`: 睡眠时间 (秒)
- **动作**:
  - `distance_low`: 距离低于阈值
  - `distance_high`: 距离高于阈值

## MQTT通信协议

### 主题格式
- **设备上报**: `device/{device_id}/report`
- **设备命令**: `device/{device_id}/command`

### 消息格式

#### 1. 设备上报 (Report)
```json
{
  "method": "report",
  "device_type": "TD01",
  "power1": 128,
  "power2": 200,
  "battery": 85,
  "sleep_time": 7200
}
```

#### 2. 设置属性 (Set)
```json
{
  "method": "set",
  "key": "power1",
  "value": 255,
  "msg_id": 1001
}
```

#### 3. 获取属性 (Get)
```json
{
  "method": "get",
  "key": "battery",
  "msg_id": 1002
}
```

#### 4. 批量更新 (Update)
```json
{
  "method": "update",
  "power1": 255,
  "power2": 0,
  "sleep_time": 3600,
  "msg_id": 1003
}
```

#### 5. 设备动作 (Action)
```json
{
  "method": "action",
  "action": "key_boot_clicked"
}
```

#### 6. 属性响应 (Response)
```json
{
  "method": "response",
  "msg_id": 1002,
  "key": "battery",
  "value": 85
}
```

## 安装和使用

### 1. 安装依赖
```bash
cd pytest
pip install -r requirements.txt
```

### 2. 启动MQTT代理 (如果没有)
```bash
# 使用Docker启动Mosquitto MQTT代理
docker run -it -p 1883:1883 eclipse-mosquitto

# 或者安装本地Mosquitto
# Ubuntu/Debian: sudo apt-get install mosquitto mosquitto-clients
# Windows: 下载并安装 https://mosquitto.org/download/
```

### 3. 运行虚拟设备
```bash
# 运行所有虚拟设备
python virtual_devices.py

# 运行测试脚本 (包含自动化测试场景)
python test_virtual_devices.py
```

### 4. 监控MQTT消息
```bash
# 启动MQTT监控器
python mqtt_monitor.py

# 指定MQTT代理地址
python mqtt_monitor.py --broker 192.168.1.100 --port 1883

# 查看主题帮助
python mqtt_monitor.py --help-topics
```

## 使用示例

### 1. 基本设备控制
```python
from virtual_devices import TD01Device

# 创建TD01设备
device = TD01Device("td01_001")
device.start()

# 设备会自动连接MQTT并开始上报状态
# 可以通过MQTT发送命令控制设备
```

### 2. 发送MQTT命令
```bash
# 使用mosquitto_pub发送命令
mosquitto_pub -h localhost -t "device/td01_001/command" -m '{"method":"set","key":"power1","value":128,"msg_id":1001}'

# 设置自动锁状态
mosquitto_pub -h localhost -t "device/zidongsuo_001/command" -m '{"method":"set","key":"open","value":1,"msg_id":2001}'
```

### 3. 监听设备上报
```bash
# 监听所有设备上报
mosquitto_sub -h localhost -t "device/+/report"

# 监听特定设备
mosquitto_sub -h localhost -t "device/td01_001/report"
```

## 测试场景

`test_virtual_devices.py` 包含以下自动化测试场景:

1. **TD01调光测试** - 测试双路PWM调光功能
2. **电击设备电压测试** - 测试电压设置和电击控制
3. **自动锁开关测试** - 测试锁的开关控制
4. **距离传感器阈值测试** - 测试距离阈值设置
5. **批量属性更新测试** - 测试批量更新功能
6. **属性查询测试** - 测试属性查询功能

## 设备行为模拟

### 自动行为
- **心跳上报**: 每10秒自动上报所有属性
- **电池模拟**: 模拟电池缓慢消耗
- **按键模拟**: 随机模拟按键点击事件
- **距离变化**: QTZ设备模拟距离值变化和阈值触发
- **电压控制**: DIANJI设备模拟PID电压控制

### 响应行为
- **属性设置**: 响应MQTT设置命令
- **属性查询**: 响应MQTT查询命令
- **批量更新**: 支持一次更新多个属性
- **消息确认**: 支持msg_id消息确认机制

## 日志和调试

所有设备都有详细的日志输出，包括:
- MQTT连接状态
- 消息收发记录
- 属性变化记录
- 设备状态变化
- 错误和异常信息

可以通过修改日志级别来控制输出详细程度:
```python
import logging
logging.basicConfig(level=logging.DEBUG)  # 显示详细调试信息
```

## 扩展开发

要添加新的设备类型:

1. 继承 `BaseVirtualDevice` 类
2. 实现必要的抽象方法:
   - `_device_init()`: 设备初始化
   - `_on_property_changed()`: 属性变化处理
   - `_on_action()`: 动作处理
3. 在 `properties` 字典中定义设备属性
4. 添加设备特有的行为线程

示例:
```python
class MyDevice(BaseVirtualDevice):
    def __init__(self, device_id: str, **kwargs):
        super().__init__(device_id, "MYDEVICE", **kwargs)
        self.properties.update({
            "my_property": {"value": 0, "readable": True, "writeable": True}
        })
    
    def _device_init(self):
        # 设备初始化逻辑
        pass
    
    def _on_property_changed(self, property_name: str, value: Any, msg_id: int):
        # 属性变化处理逻辑
        pass
    
    def _on_action(self, data: Dict[str, Any]):
        # 动作处理逻辑
        pass
```

## 故障排除

### 常见问题

1. **MQTT连接失败**
   - 检查MQTT代理是否运行
   - 检查网络连接和防火墙设置
   - 确认MQTT代理地址和端口正确

2. **设备无响应**
   - 检查设备是否正确启动
   - 检查MQTT主题是否正确
   - 查看设备日志输出

3. **消息格式错误**
   - 确认JSON格式正确
   - 检查必要字段是否存在
   - 参考协议文档中的消息格式

### 调试技巧

1. 使用MQTT监控器查看所有消息
2. 启用DEBUG日志级别
3. 使用mosquitto_pub/sub工具测试
4. 检查设备属性定义和权限

## 许可证

本项目仅用于测试和开发目的。