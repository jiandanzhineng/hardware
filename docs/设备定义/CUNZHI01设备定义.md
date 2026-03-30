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
| `game_mode` | int | 读写 | 当前玩法模式 | 0 | 0=停止, 1=寸止玩法, 2=踮脚提肛玩法 |
| `game_duration` | int | 读写 | 玩法持续时间 | 0 | 秒 |
| `game_fly_dur` | int | 读写 | 寸止玩法起飞期时长 | 60 | 秒 |
| `game_e_vol` | int | 读写 | 玩法电击电压 | 0 | 0-75 |
| `game_e_dur` | int | 读写 | 玩法电击时长 | 0 | 毫秒 |
| `game_p1_thresh` | float | 读写 | 临界压力1(寸止阈值/括约肌阈值) | 0 | 浮点数 |
| `game_p2_thresh` | float | 读写 | 临界压力2(脚后跟踮脚阈值) | 0 | 浮点数 |
| `game_m_dur` | int | 读写 | 玩法电机刺激时间 | 0 | 毫秒 |
| `game_m_power` | int | 读写 | 玩法电机刺激强度 | 0 | 0-255 |
| `game_m_step` | int | 读写 | 寸止电机每秒提升强度 | 0 | 整数 |
| `game_cooldown` | int | 读写 | 玩法惩罚冷静期 | 0 | 毫秒 |
| `game_kegel_t` | int | 读写 | 连续未提肛判定时间 | 0 | 毫秒 |

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

# 设备内玩法

通过设置 `game_mode` 属性启动特定玩法，玩法参数通过各类 `game_` 前缀的属性进行配置。所有玩法均由固件内部的独立任务（周期100ms）处理。玩法结束后会自动将 `game_mode` 置 0 并关闭所有输出。

## 寸止玩法 (`game_mode = 1`)

检测 `pressure` (压力通道1) 的压力值：
- **高于阈值 (`game_p1_thresh`)**：触发惩罚。电机强度清零并停止，启动电刺激(`game_e_vol`)持续一段时间(`game_e_dur`)。惩罚结束后进入冷静期(`game_cooldown`)，期间电机保持停止，且不会再次触发惩罚。
- **低于或等于阈值**：若不在冷静期/惩罚期内，电机从 0 开始，每秒按 `game_m_step` 逐步提升强度，最高不超过 255。
- **起飞期**：在游戏结束前的指定时间（`game_fly_dur`）内进入起飞期。此期间不再因超过阈值而触发新的惩罚，允许电机强度持续提升。如果进入起飞期时处于惩罚或冷静期，将等待其自然结束。

**相关参数 (对应 Property)**
- `game_duration`: 玩法持续时间 (秒)
- `game_fly_dur`: 起飞期时长 (秒)
- `game_p1_thresh`: 临界压力
- `game_e_vol`: 惩罚电击电压
- `game_e_dur`: 惩罚电击时长 (毫秒)
- `game_m_step`: 电机每秒提升强度
- `game_cooldown`: 惩罚冷静期 (毫秒)

## 踮脚提肛玩法 (`game_mode = 2`)

通过 `pressure` 检测括约肌压力，通过 `pressure1` 检测脚后跟压强。

**惩罚触发条件**（两者满足其一即触发电击）：
1. **未踮脚**：`pressure1` 大于脚后跟临界压力 (`game_p2_thresh`)。
2. **未提肛**：`pressure` 连续 `game_kegel_t` 毫秒内均小于括约肌临界压力 (`game_p1_thresh`)。

**惩罚执行逻辑**：
- 如果满足 **未踮脚** 或 **未提肛** 条件：启动电脉冲刺激(`game_e_vol`)，持续 `game_e_dur` 毫秒。
- 如果满足 **未提肛** 条件：同时启动电机进行刺激(`game_m_power`)，持续 `game_m_dur` 毫秒。
- 惩罚结束后，进入 `game_cooldown` 毫秒的冷静期。冷静期内不会再次触发惩罚，也不累加未提肛时间。

**相关参数 (对应 Property)**
- `game_duration`: 玩法持续时间 (秒)
- `game_p1_thresh`: 括约肌临界压力
- `game_p2_thresh`: 脚后跟部临界压力
- `game_kegel_t`: 连续未提肛判定时间 (毫秒)
- `game_e_vol`: 惩罚电击电压
- `game_e_dur`: 惩罚电击时长 (毫秒)
- `game_m_power`: 惩罚电机刺激强度
- `game_m_dur`: 惩罚电机刺激时间 (毫秒)
- `game_cooldown`: 惩罚冷静期 (毫秒)
