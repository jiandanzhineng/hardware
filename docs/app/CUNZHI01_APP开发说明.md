# CUNZHI01 APP开发说明

## 1. 目标

APP 通过 BLE 连接 `CUNZHI01` 设备，完成以下能力：

- 连接设备并切换到 BLE 模式
- 实时显示 `pressure`、`pressure1`、`battery`
- 配置并启动 2 个内置玩法
- 手动停止玩法，并在异常断开时做兜底停止

说明：玩法逻辑在固件内执行，APP 只负责参数配置、开始/停止、状态展示。

## 2. BLE接入方式

- 服务 UUID：`0x00FF`
- 模式特征：`0xFF02`
- 连接后先向 `0xFF02` 写入 `0x01`，切换到 BLE 模式
- 各属性特征通过 `0x2901 User Description` 查找，描述文本就是属性名，例如 `power`、`pressure`、`game_mode`

类型建议：

- `int`：4 字节，小端
- `float`：APP 侧按数值属性封装，不要在业务层直接拼字节

当前设备开发必须用到的属性：

| 属性 | 类型 | 用途 |
| :--- | :--- | :--- |
| `battery` | int | 电量显示 |
| `power` | int | 当前电机输出，运行状态展示 |
| `voltage` | int | 当前电脉冲输出，运行状态展示 |
| `pressure` | float | 压力通道1，寸止/提肛判定 |
| `pressure1` | float | 压力通道2，踮脚判定 |
| `report_delay_ms` | int | 上报周期 |
| `game_mode` | int | 玩法开关，`0`停止，`1`寸止，`2`踮脚提肛 |
| `game_duration` | int | 玩法时长，单位秒，`0`表示不自动结束 |
| `game_fly_dur` | int | 寸止玩法起飞期时长，单位秒 |
| `game_e_vol` | int | 惩罚电脉冲强度 |
| `game_e_dur` | int | 惩罚电脉冲时长，毫秒 |
| `game_p1_thresh` | float | 压力阈值1 |
| `game_p2_thresh` | float | 压力阈值2 |
| `game_m_dur` | int | 电机刺激时长，毫秒 |
| `game_m_power` | int | 电机刺激强度 |
| `game_m_step` | int | 寸止玩法中电机每秒递增强度 |
| `game_cooldown` | int | 惩罚后的冷静期，毫秒 |
| `game_kegel_t` | int | 连续未提肛判定时长，毫秒 |

建议订阅通知：

- `pressure`
- `pressure1`
- `battery`
- `power`
- `voltage`
- `game_mode`

## 3. APP建议页面

建议做 3 个页面：

### 3.1 设备页

显示内容：

- 连接状态
- 设备类型：应为 `CUNZHI01`
- 电量
- 实时 `pressure`
- 实时 `pressure1`

操作：

- 连接/断开
- 进入“寸止玩法”
- 进入“踮脚提肛玩法”
- 全局停止按钮：写入 `game_mode = 0`

### 3.2 寸止玩法页

配置项：

- 玩法时长：`game_duration`
- 起飞期时长：`game_fly_dur`
- 寸止阈值：`game_p1_thresh`
- 惩罚电脉冲强度：`game_e_vol`
- 惩罚电脉冲时长：`game_e_dur`
- 电机递增强度：`game_m_step`
- 冷静期：`game_cooldown`

展示项：

- 当前 `pressure`
- 当前电机强度 `power`
- 当前玩法状态：运行中 / 惩罚中 / 冷静期 / 已结束

### 3.3 踮脚提肛玩法页

配置项：

- 玩法时长：`game_duration`
- 提肛阈值：`game_p1_thresh`
- 踮脚阈值：`game_p2_thresh`
- 连续未提肛判定时长：`game_kegel_t`
- 惩罚电脉冲强度：`game_e_vol`
- 惩罚电脉冲时长：`game_e_dur`
- 惩罚电机强度：`game_m_power`
- 惩罚电机时长：`game_m_dur`
- 冷静期：`game_cooldown`

展示项：

- 当前 `pressure`
- 当前 `pressure1`
- 当前电机强度 `power`
- 当前玩法状态：运行中 / 惩罚中 / 冷静期 / 已结束

## 4. 两个玩法的APP交互逻辑

## 4.1 寸止玩法

启动顺序：

1. 写入本玩法全部参数
2. 写入 `game_mode = 1`
3. 开始监听 `pressure`、`power`、`voltage`、`game_mode`

玩法规则(这些规则在设备中实现 此处仅展示)：

- 当不在起飞期且 `pressure > game_p1_thresh` 时，触发惩罚
- 惩罚时电机立即归零，并触发电脉冲 `game_e_vol`，持续 `game_e_dur`
- 惩罚结束后进入 `game_cooldown`
- 不在惩罚/冷静期时，电机从 0 开始按 `game_m_step` 每秒递增，最大 255
- 进入最后 `game_fly_dur` 秒后即进入**起飞期**，此期间不再因压力过大触发新的惩罚，允许电机强度持续提升
- 到达 `game_duration` 后，设备自动停止并把 `game_mode` 置为 0

APP 侧重点：

- 页面上要持续显示实时压力，方便用户调阈值
- 阈值建议提供“取当前值”按钮，降低手工输入成本
- 停止按钮只需要写 `game_mode = 0`

## 4.2 踮脚提肛玩法

启动顺序：

1. 写入本玩法全部参数
2. 写入 `game_mode = 2`
3. 开始监听 `pressure`、`pressure1`、`power`、`voltage`、`game_mode`

玩法规则(这些规则在设备中实现 此处仅展示)：

- 满足以下任一条件即触发电脉冲惩罚：
  - `pressure1 > game_p2_thresh`，判定为未踮脚
  - `pressure` 连续 `game_kegel_t` 毫秒小于 `game_p1_thresh`，判定为未提肛
- 触发惩罚后输出电脉冲，持续 `game_e_dur`
- 如果是“未提肛”触发，还要同时启动电机刺激，强度 `game_m_power`，持续 `game_m_dur`
- 惩罚结束后进入 `game_cooldown`
- 到达 `game_duration` 后，设备自动停止并把 `game_mode` 置为 0

APP 侧重点：

- 页面上同时显示两个实时压力值
- 建议对两个阈值分别提供“取当前值”按钮
- 可以用颜色提示当前是否满足“提肛”和“踮脚”条件

## 5. 停止与异常处理

- 用户主动停止：写 `game_mode = 0`
- 玩法自然结束：设备会自动把 `game_mode` 置为 `0`
- APP 监听到 `game_mode = 0` 后，应刷新页面状态为“已结束”
- 断开连接前建议兜底写一次 `game_mode = 0`

## 6. 实现注意事项

- 玩法参数先写完，再写 `game_mode`，不要反过来
- 阈值类参数依赖实时压力值，页面必须有实时数值展示
- `pressure` / `pressure1` 建议做数值平滑展示，不要只做文本跳变
- 不建议在玩法运行中频繁改参数；如要改，建议先停止再重启玩法
- 设备已内置玩法执行逻辑，APP 不需要自己做计时和惩罚判定

## 7. 编码注意

若 APP 直接实现 BLE 属性编解码，需要注意：

- `int` 属性写入：4 字节小端
- `pressure`、`pressure1`、`game_p1_thresh`、`game_p2_thresh` 属于 `float` 属性
- 当前固件中，`float` 属性读取/通知为 4 字节浮点值
- 当前固件中，`float` 属性写入格式为：前 3 字节有符号 `mantissa` 小端，第 4 字节有符号 `exponent`，实际值 = `mantissa * 10^exponent`
- 例：写入 `123.4` 时，可编码为 `mantissa = 1234`，`exponent = -1`

业务层建议只关心“数值”，不要直接在玩法页面处理字节编码。
