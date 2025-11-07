# MQTT消息类型与格式（base_device，不含 DZC01）

适用范围：`components/base_device` 目录下设备（排除 DZC01）。设备通过 MQTT 与云端通信，主题约定如下：

- 发布主题：`/dpub/{mac}`
- 订阅主题：`/drecv/{mac}`，同时订阅：`/all`

通用字段约定：

- `method`：消息类型标识（字符串）
- `msg_id`：可选，请求跟踪用（整数），发起方携带，设备响应中原样返回

## 消息类型与格式

以下为所有设备共用的消息类型与格式（不区分具体设备）。总计 8 种。

- set（收）

  - 作用：设置单个属性
  - 格式：`{"method":"set","key":"<属性>","value":<值>,"msg_id":<可选>}`
  - 示例：`{"method":"set","key":"sleep_time","value":3600,"msg_id":101}`
- get（收）

  - 作用：读取单个属性
  - 格式：`{"method":"get","key":"<属性>","msg_id":<可选>}`
  - 示例：`{"method":"get","key":"battery","msg_id":102}`
- update（发）

  - 作用：属性变更/上报（统一写成一个，给一个例子）
  - 格式（扁平属性）：`{"method":"update", "<属性1>":<值1>, "<属性2>":<值2> ...}`
  - 示例：`{"method":"update","pressure":101.325,"temperature":25.1234}`
- report（发）

  - 作用：周期性全量属性上报
  - 格式：`{"method":"report", "<属性1>":<值1>, "<属性2>":<值2> ...}`
  - 示例：`{"method":"report","device_type":"QIYA","battery":85,"sleep_time":7200}`
- action（发）

  - 作用：事件上报（如按键、紧急状态）
  - 格式：`{"method":"action","action":"<事件名>"}`
  - 示例：`{"method":"action","action":"key_clicked"}`、`{"method":"action","action":"emergency_open"}`
- low（发）（新设备不再使用，统一使用action）

  - 作用：阈值事件（低阈触发）
  - 格式：`{"method":"low"}`
  - 示例：`{"method":"low"}`
- high（发）（新设备不再使用，统一使用action）

  - 作用：阈值事件（高阈触发）
  - 格式：`{"method":"high"}`
  - 示例：`{"method":"high"}`
- dian（收）（新设备不再使用，统一使用action）

  - 作用：设备动作命令（电击控制）
  - 格式：`{"method":"dian","time":<毫秒>,"voltage":<电压>}`
  - 示例：`{"method":"dian","time":1000,"voltage":60}`

## 属性值类型

- 整数：`int`
- 浮点：`float`
- 字符串：`string`

说明：属性名来源于各设备 `device_properties` 列表（如 `device_type`、`battery`、`pressure` 等）。
