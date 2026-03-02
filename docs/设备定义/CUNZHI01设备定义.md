# CUNZHI01 设备定义（跳蛋 + 压力监测 + 电脉冲）

本设备以 ESP32-C3 为核心，集成跳蛋电机控制、双通道压力监测、高压电脉冲输出及电池管理功能。

## 设备标识

- 设备类型：`CUNZHI01`（新增时需按《新增设备类型指南》在固件中注册）
- 适用场景：情趣玩具、理疗设备，支持震动与电刺激，具备压力反馈
- 硬件版本：V1.0

## 硬件接口

| 功能模块 | 信号名称 | GPIO | 类型 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| **电机控制** | TD01 | **GPIO 7** | PWM | 控制跳蛋震动强度。高电平/PWM有效。 |
| **电池监测** | BAT_ADC | **GPIO 0** | ADC | 测量电池电压 (分压电路)。 |
| **电池监测使能** | BAT_EN | **GPIO 1** | 输出 | 电池电压监测使能信号。高电平使能，低电平禁用。 |
| **压力监测** | PR_ADC1 | **GPIO 2** | ADC | 压力传感器通道 1 电压读取。 |
| **压力监测** | PR_ADC2 | **GPIO 3** | ADC | 压力传感器通道 2 电压读取。 |
| **脉冲方向** | IO4 | **GPIO 4** | 输出 | 电脉冲输出方向控制 A。 |
| **脉冲方向** | IO5 | **GPIO 5** | 输出 | 电脉冲输出方向控制 B。 |
| **脉冲电压** | BOOST_EN | **GPIO 6** | PWM | 控制升压电路。PWM 占空比调节输出电压 (最高约 75V)。 |
| **LED指示灯** | LED | **GPIO 7** | 输出 | 板载 LED 指示灯 (与电机共用引脚，震动时亮起)。 |

## 通用设备属性（与现有设备一致）

| 属性名 | 类型 | 读写 | 描述 | 默认值 |
| :--- | :--- | :--- | :--- | :--- |
| `device_type` | string | 只读 | 设备类型标识 | `CUNZHI01` |
| `sleep_time` | int | 读写 | 休眠时间（秒），无操作后自动休眠 | 7200 |
| `battery` | int | 只读 | 电池电量（%） | 0 |

## CUNZHI01 设备属性

| 属性名 | 类型 | 读写 | 描述 | 默认值 | 范围/枚举 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `power` | int | 读写 | 电机震动强度 | 0 | 0-255 (0=关闭) |
| `voltage` | int | 读写 | 电脉冲强度 (升压PWM) | 0 | 0-100 (0=关闭) |
| `pressure` | int | 只读 | 压力通道1数值 (欧姆) | 0 | 0-99999 |
| `pressure1` | int | 只读 | 压力通道2数值 (欧姆) | 0 | 0-99999 |
| `report_delay_ms` | int | 读写 | 状态上报周期 (毫秒) | 1000 | 100-10000 |

## 消息与上报

### 1. 设置属性 (Set)

控制设备震动和电脉冲：

```json
{
  "method": "set",
  "key": "power",
  "value": 50,
  "msg_id": 2001
}
```

### 2. 批量更新 (Update)

同时设置多个属性：

```json
{
  "method": "update",
  "power": 80,
  "voltage": 30,
  "msg_id": 2002
}
```

### 3. 设备定期上报

设备按 `report_delay_ms` 周期上报当前状态（含压力值、电池、运行状态）：
// 分段延时，最多1秒生效延迟 参考 qiya实现
```json
{
  "method": "update",
  "pressure": 1205,
  "pressure1": 2302
}
```

## 行为与约束

### 1. 电机控制 (Motor)
- **引脚**: `GPIO 7`
- **逻辑**: `power` 映射为 PWM 占空比 (0-255)。0 为完全关闭。 参考TD01实现

### 2. 电脉冲控制 (Pulse)
- **升压 (Boost)**: `GPIO 6`。`voltage` 映射为 PWM 占空比(0-20000 参考dianji设备实现)。
- **方向 (Direction)**: `GPIO 4`, `GPIO 5`。
  脉冲方向参考 dianji 设备实现 正反交替
- **安全约束**: 严禁 GPIO 4 和 GPIO 5 同时为高电平，防止 H 桥短路。

### 3. 压力监测 (Pressure)
- **通道**: `GPIO 2` (PR_ADC1), `GPIO 3` (PR_ADC2)。
- **逻辑**: 周期性采样 ADC 值，计算阻值，更新到 `pressure` / `pressure1` 属性。分压电阻1K欧，定义为宏变量方便后续修改。

### 4. 电池管理
- **引脚**: `GPIO 0` (ADC), `GPIO 1` (Enable)。
- **逻辑**: 采样前需拉高 `GPIO 1`，采样后拉低以省电。 参考TD01中的电池实现 通过components\Battery实现电量采集 不需要重写

## 与新增设备流程的对应关系

- 在 `components/base_device/include/base_device.h` 中新增 `DEVICE_CUNZHI01` 与 `CONFIG_DEVICE_CUNZHI01`。
- 在 `components/base_device/CMakeLists.txt` 中追加 `CUNZHI01.c`。
- 在 `components/base_device/Kconfig` 中新增 `DEVICE_CUNZHI01` 选项。
- 在 `components/base_device/CUNZHI01.c` 中实现属性读写与硬件控制逻辑。
