# 设备API参考文档

本文档描述了项目中所有虚拟设备的API接口、属性定义和使用方法。

## 目录

- [通用设备属性](#通用设备属性)
- [TD01 双路电机控制器](#td01-双路电机控制器)
- [DIANJI 电击设备](#dianji-电击设备)
- [ZIDONGSUO 自动锁](#zidongsuo-自动锁)
- [QTZ 激光测距传感器](#qtz-激光测距传感器)
- [MQTT通信协议](#mqtt通信协议)
- [错误代码](#错误代码)

## 通用设备属性

所有设备都继承以下基础属性：

### 基础属性

| 属性名 | 类型 | 读写权限 | 描述 | 默认值 | 范围 |
|--------|------|----------|------|--------|---------|
| `device_type` | string | 只读 | 设备类型标识 | 根据设备而定 | - |
| `sleep_time` | int | 读写 | 设备休眠时间(秒) | 7200 | 0-86400 |
| `battery` | int | 只读 | 电池电量百分比 | 80-100 | 0-100 |

### 通用动作

- **心跳上报**: 设备每10秒自动上报所有可读属性
- **电池监控**: 设备自动监控电池电量变化
- **休眠管理**: 当超过`sleep_time`秒无消息时，设备进入深度休眠

---

## TD01 单路电机控制器

**设备类型**: `TD01`  
**描述**: 单路可调电机智能插座，支持PWM电机控制

### 设备属性

| 属性名 | 类型 | 读写权限 | 描述 | 默认值 | 范围 |
|--------|------|----------|------|--------|---------|
| `power` | int | 读写 | 功率控制 | 0 | 0-255 |

### 功能说明

- **功率控制**: 通过PWM信号控制输出的功率
- **按键检测**: 内置按键，按下时触发`key_boot_clicked`动作
- **实时响应**: 属性变化立即生效，无延迟

### 硬件接口

- **GPIO 7**: 功率输出 (DIMMABLE_GPIO_1)
- **GPIO 9**: 按键输入
- **GPIO 2/3**: 开关控制输出

### 使用示例

```json
// 设置功率为50%
{
  "method": "set",
  "key": "power",
  "value": 128,
  "msg_id": 1001
}

// 设置功率为最大值
{
  "method": "update",
  "power": 255,
  "msg_id": 1002
}
```

### 触发事件

- **按键事件**: 当按下设备按键时，发送`key_boot_clicked`动作消息

---

## DIANJI 电击设备

**设备类型**: `DIANJI`  
**描述**: 电击设备，支持电压控制和定时电击功能

### 设备属性

| 属性名 | 类型 | 读写权限 | 描述 | 默认值 | 范围 |
|--------|------|----------|------|--------|---------|
| `voltage` | int | 读写 | 输出电压值 | 0 | 0-100 |
| `delay` | int | 读写 | 电击间隔时间(ms) | 30 | 20-1000 |
| `shock` | int | 读写 | 电击开关状态 | 0 | 0-1 |

### 功能说明

- **电压控制**: 通过PWM升压电路控制输出电压
- **定时电击**: 支持设定时间间隔的电击功能
- **安全保护**: 内置电压监控和过压保护
- **PID控制**: 使用PID算法精确控制输出电压

### 硬件接口

- **GPIO 3**: 输出控制1 (O1)
- **GPIO 19**: 输出控制2 (O2)
- **GPIO 10**: PWM升压控制 (BOOST_PWM)
- **GPIO 4**: 升压电压检测 (BOOST_ADC)
- **GPIO 12/13**: LED指示灯

### 特殊动作

```json
// 执行定时电击
{
  "method": "dian",
  "time": 5000,
  "voltage": 50
}
```

### 使用示例

```json
// 设置电压为50V
{
  "method": "set",
  "key": "voltage",
  "value": 50,
  "msg_id": 2001
}

// 开启电击
{
  "method": "set",
  "key": "shock",
  "value": 1,
  "msg_id": 2002
}
```

### 安全注意事项

⚠️ **警告**: 此设备涉及高压电路，使用时请确保安全措施到位 因使用不当可能导致设备损坏或人身伤害需自行负责

---

## ZIDONGSUO 自动锁

**设备类型**: `ZIDONGSUO`  
**描述**: 智能自动锁，支持舵机控制和状态指示

### 设备属性

| 属性名 | 类型 | 读写权限 | 描述 | 默认值 | 范围 |
|--------|------|----------|------|--------|---------|
| `open` | int | 读写 | 锁开关状态 | 0 | 0-1 |

### 功能说明

- **舵机控制**: 通过舵机实现锁的开关动作
- **状态指示**: LED灯显示当前锁状态
- **按键控制**: 支持物理按键操作
- **电池监控**: 实时监控电池电压和电量

### 硬件接口

- **GPIO 6**: 按键输入 (BUTTON_PIN)
- **GPIO 7**: 舵机控制 (SERVO_PIN)
- **GPIO 10**: LED状态指示 (LED_PIN)
- **GPIO 1**: 电池检测使能 (BAT_EN_GPIO)
- **ADC Channel 0**: 电池电压检测

### 状态映射

- `open = 0`: 锁关闭状态，舵机角度180°，LED亮
- `open = 1`: 锁开启状态，舵机角度0°，LED灭

### 使用示例

```json
// 开锁
{
  "method": "set",
  "key": "open",
  "value": 1,
  "msg_id": 3001
}

// 关锁
{
  "method": "set",
  "key": "open",
  "value": 0,
  "msg_id": 3002
}
```

### 触发事件

- **按键事件**: 当按下设备按键时，发送`key_clicked`动作消息

**示例**:
```json
{
  "method": "action",
  "action": "key_clicked"
}
```

---

## QTZ 激光测距传感器

**设备类型**: `QTZ`  
**描述**: 基于VL6180X的高精度激光测距传感器，支持距离检测和阈值报警

### 设备属性

| 属性名 | 类型 | 读写权限 | 描述 | 默认值 | 范围 |
|--------|------|----------|------|--------|---------|
| `distance` | float | 只读 | 当前检测距离(mm) | 0 | 10-500 |
| `report_delay_ms` | int | 读写 | 上报间隔时间(ms) | 10000 | 100-10000 |
| `low_band` | int | 读写 | 低阈值(mm) | 60 | 0-200 |
| `high_band` | int | 读写 | 高阈值(mm) | 150 | 0-200 |
| `button0` | int | 只读 | 按键0状态 | 0 | 0-1 |
| `button1` | int | 只读 | 按键1状态 | 0 | 0-1 |

### 功能说明

- **精确测距**: 使用VL6180X传感器，测量精度±1mm
- **阈值监控**: 支持设置高低阈值，超出范围时自动报警
- **双按键**: 两个独立按键，支持状态检测
- **数据持久化**: 阈值设置保存到NVS存储

### 硬件接口

- **GPIO 4**: I2C SDA (I2C_MASTER_SDA_IO)
- **GPIO 5**: I2C SCL (I2C_MASTER_SCL_IO)
- **GPIO 2**: 按键0输入 (BUTTON0_GPIO)
- **GPIO 3**: 按键1输入 (BUTTON1_GPIO)

### 传感器规格

- **测量范围**: 0-500mm
- **精度**: ±1mm
- **接口**: I2C (地址: 0x29)
- **更新频率**: 可配置 (默认1秒)

### 使用示例

```json
// 设置低阈值
{
  "method": "set",
  "key": "low_band",
  "value": 80,
  "msg_id": 4001
}

// 设置高阈值
{
  "method": "set",
  "key": "high_band",
  "value": 120,
  "msg_id": 4002
}

// 查询当前距离
{
  "method": "get",
  "key": "distance",
  "msg_id": 4003
}
```

### 触发事件

- **低阈值触发**: 当距离小于`low_band`时，发送`low`动作消息
- **高阈值触发**: 当距离大于`high_band`时，发送`high`动作消息
- **按键事件**: 按键按下/释放时发送相应动作消息

**触发事件示例**:
```json
// 距离低于阈值时的触发消息
{
  "method": "low"
}

// 距离高于阈值时的触发消息
{
  "method": "high"
}

// 按键0按下时的消息
{
  "method": "update",
  "msg_id": 0,
  "key": "button0",
  "value": 1
}

// 按键0释放时的消息
{
  "method": "update",
  "msg_id": 0,
  "key": "button0",
  "value": 0
}
```

---

## MQTT通信协议

### 主题格式

- **设备上报**: `/dpub/{mac_address}` (设备发布消息到此主题)
- **设备命令**: `/drecv/{mac_address}` (设备订阅此主题接收命令)
- **全局广播**: `/all` (所有设备都会订阅此主题)

其中 `{mac_address}` 是设备的MAC地址，格式为12位十六进制字符串（如：`aabbccddeeff`）

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

#### 5. 属性响应 (Response)

```json
{
  "method": "update",
  "msg_id": 1002,
  "key": "battery",
  "value": 85
}
```

#### 6. 动作消息 (Action)

```json
{
  "method": "action",
  "action": "key_boot_clicked"
}
```

### QoS级别

- **设备上报**: QoS 1 (至少一次)
- **命令下发**: QoS 1 (至少一次)
- **响应消息**: QoS 1 (至少一次)

---

## 错误代码

| 错误代码 | 描述 | 解决方案 |
|----------|------|----------|
| -1 | 属性不存在 | 检查属性名称是否正确 |
| -2 | 属性只读 | 使用只读属性不能设置值 |
| -3 | 值超出范围 | 检查设置值是否在允许范围内 |
| -4 | JSON格式错误 | 检查消息格式是否正确 |
| -5 | 设备离线 | 检查设备连接状态 |

---

## 编程示例

### Python示例 (使用paho-mqtt)

```python
import paho.mqtt.client as mqtt
import json
import time

class DeviceController:
    def __init__(self, broker_host="localhost", broker_port=1883):
        self.client = mqtt.Client()
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.connect(broker_host, broker_port, 60)
        self.client.loop_start()
    
    def _on_connect(self, client, userdata, flags, rc):
        print(f"Connected with result code {rc}")
        # 订阅所有设备上报和全局广播
        client.subscribe("/dpub/+")
        client.subscribe("/all")
    
    def _on_message(self, client, userdata, msg):
        topic = msg.topic
        payload = json.loads(msg.payload.decode())
        print(f"Received from {topic}: {payload}")
    
    def set_device_property(self, mac_address, key, value, msg_id=None):
        """设置设备属性"""
        if msg_id is None:
            msg_id = int(time.time() * 1000)
        
        command = {
            "method": "set",
            "key": key,
            "value": value,
            "msg_id": msg_id
        }
        
        topic = f"/drecv/{mac_address}"
        self.client.publish(topic, json.dumps(command), qos=1)
    
    def get_device_property(self, mac_address, key, msg_id=None):
        """获取设备属性"""
        if msg_id is None:
            msg_id = int(time.time() * 1000)
        
        command = {
            "method": "get",
            "key": key,
            "msg_id": msg_id
        }
        
        topic = f"/drecv/{mac_address}"
        self.client.publish(topic, json.dumps(command), qos=1)
    
    def update_device_properties(self, mac_address, properties, msg_id=None):
        """批量更新设备属性"""
        if msg_id is None:
            msg_id = int(time.time() * 1000)
        
        command = {
            "method": "update",
            "msg_id": msg_id
        }
        command.update(properties)
        
        topic = f"/drecv/{mac_address}"
        self.client.publish(topic, json.dumps(command), qos=1)

# 使用示例
controller = DeviceController()

# 控制TD01设备调光 (使用设备的MAC地址)
controller.set_device_property("aabbccddeeff", "power", 128)

# 控制自动锁开关
controller.set_device_property("112233445566", "open", 1)

# 设置距离传感器阈值
controller.update_device_properties("778899aabbcc", {
    "low_band": 80,
    "high_band": 120
})

# 查询电池电量
controller.get_device_property("aabbccddeeff", "battery")

# 向所有设备发送广播命令
controller.client.publish("/all", json.dumps({"method": "get", "key": "battery"}), qos=1)
```

### JavaScript示例 (Node.js)

```javascript
const mqtt = require('mqtt');

class DeviceController {
    constructor(brokerUrl = 'mqtt://localhost:1883') {
        this.client = mqtt.connect(brokerUrl);
        
        this.client.on('connect', () => {
            console.log('Connected to MQTT broker');
            this.client.subscribe('/dpub/+');
            this.client.subscribe('/all');
        });
        
        this.client.on('message', (topic, message) => {
            const payload = JSON.parse(message.toString());
            console.log(`Received from ${topic}:`, payload);
        });
    }
    
    setDeviceProperty(macAddress, key, value, msgId = null) {
        if (!msgId) msgId = Date.now();
        
        const command = {
            method: 'set',
            key: key,
            value: value,
            msg_id: msgId
        };
        
        const topic = `/drecv/${macAddress}`;
        this.client.publish(topic, JSON.stringify(command), { qos: 1 });
    }
    
    getDeviceProperty(macAddress, key, msgId = null) {
        if (!msgId) msgId = Date.now();
        
        const command = {
            method: 'get',
            key: key,
            msg_id: msgId
        };
        
        const topic = `/drecv/${macAddress}`;
        this.client.publish(topic, JSON.stringify(command), { qos: 1 });
    }
}

// 使用示例
const controller = new DeviceController();

// 控制设备 (使用设备的MAC地址)
setTimeout(() => {
    controller.setDeviceProperty('aabbccddeeff', 'power', 255);
    controller.setDeviceProperty('112233445566', 'open', 1);
    controller.getDeviceProperty('778899aabbcc', 'distance');
    
    // 向所有设备发送广播命令
    controller.client.publish('/all', JSON.stringify({method: 'get', key: 'battery'}), {qos: 1});
}, 1000);
```

---

## 版本历史

- **v1.0.0** (2024-01-01): 初始版本，支持TD01、DIANJI、ZIDONGSUO、QTZ四种设备类型

---

## 联系信息

如有问题或建议，请联系开发团队。

**文档更新时间**: 2024-01-01  
**API版本**: v1.0.0