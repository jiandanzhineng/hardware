# DAN01 设备定义（ESP32-C3 + HLK-AS201 十轴姿态）

本设备以自绘 ESP32-C3-20-N4 为主控，串口读取 HLK-AS201 十轴姿态传感器，带板载按键与电池。
接入 `hardware` 后走 MQTT / BLE 属性协议，不做 SoftAP 网页，不做设备内玩法。

参考：原工程 `ESP32-C3-DAN`，说明书《HLK-AS201 系列姿态传感器模块说明书 V1.1》。

## 设备标识

- 设备类型：`DAN01`（新增时需按《新增设备类型指南》在固件中注册）
- 设备类型数字：`11`（接在 `DEVICE_RT01 = 10` 之后）
- 适用场景：姿态感知、倾斜/晃动检测、按键输入
- 硬件版本：V1.2
- 主控：ESP32-C3（4MB Flash）
- 传感器：HLK-AS201（十轴：加速度 + 陀螺仪 + 磁力计 + 气压计）

## 硬件接口

| 功能模块 | 信号名称 | GPIO | 类型 | 说明 |
| :--- | :--- | :--- | :--- | :--- |
| AS201 UART TX | AS201_TX | GPIO 18 | 输出 | 接 AS201 UART_RX（发命令） |
| AS201 UART RX | AS201_RX | GPIO 19 | 输入 | 接 AS201 UART_TX（收数据） |
| AS201 复位 | AS201_RST | GPIO 4 | 输出 | 低电平复位，上电拉低再拉高 |
| 板载按键 | KEY | GPIO 5 | 输入上拉 | 低电平有效 |
| 电池监测 | BAT_ADC | GPIO 0 | ADC | ADC1_CH0，27k/10k 分压 |
| 电池监测使能 | BAT_EN | GPIO 1 | 输出 | 高电平使能采样，用完拉低 |
| 联网指示 | LED | GPIO 10 | 输出 | LED_RUN，作 CONNECTED_LED，低电平点亮 |

说明：引脚以 `ESP32-C3-DAN` 原理图 SCH V1.2 为准。GPIO18/19 作 AS201 UART，须关闭 USB Serial JTAG 次控制台。

电池测量复用 `components/Battery`。

## 通用设备属性（与现有设备一致）

| 属性名 | 类型 | 读写 | 描述 | 默认值 |
| :--- | :--- | :--- | :--- | :--- |
| `device_type` | string | 只读 | 设备类型标识 | `DAN01` |
| `sleep_time` | int | 读写 | 休眠时间（秒） | 7200 |
| `battery` | int | 只读 | 电池电量（%） | 0 |

## DAN01 设备属性

加速度、角速度、磁场、四元数各打成一个 HEX 字符串。欧拉角不上报。

| 属性名 | 类型 | 读写 | 描述 | 默认值 | 范围/单位 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `report_delay_ms` | int | 读写 | 周期 `update` 间隔 | 200 | 50–10000，单位 ms |
| `accel` | string | 只读 | 加速度 XYZ | `""` | 12 位 HEX，3×int16 |
| `gyro` | string | 只读 | 角速度 XYZ | `""` | 12 位 HEX，3×int16 |
| `mag` | string | 只读 | 磁场 XYZ | `""` | 12 位 HEX，3×int16 |
| `quat` | string | 只读 | 四元数 Q0–Q3 | `""` | 16 位 HEX，4×int16 |
| `temperature` | float | 只读 | 板上温度 | 0 | 约 -45~85，单位 ℃ |
| `height` | float | 只读 | 气压换算高度 | 0 | 约 -500~9000，单位 m |
| `button0` | int | 只读 | 按键电平 | 0 | 0=松开，1=按下 |
| `mag_accuracy` | int | 只读 | 磁力计精度 TYPE bit2~3 | 0 | 0=干扰/未校准，3=已校准 |

属性合计 12 项（含 3 个通用）。BLE GATT 最坏 `10 + 12×4 = 58`。

## HEX 打包

- 小写十六进制，无 `0x`，无分隔符。
- 字节与 AS201 数据段一致：每轴 `int16` 小端；`accel`/`gyro`/`mag` 顺序 X,Y,Z；`quat` 顺序 Q0,Q1,Q2,Q3。
- 固件把帧内对应字节直接转 HEX，不先转 float。
- 上层：每 2 字符 → 1 字节，再按小端 `int16`，乘下面的系数。
- 欧拉角由上层用 `quat` 自算，不上属性。

## AS201 通信

- 串口：115200 8N1，小端。默认主动上报 20Hz。
- 帧：`FA FB | len | cmd | data | check | FC FD`。`check` = cmd+data 累加和低字节。
- 数据上报 `cmd=0x00`，十轴默认 50 字节，`len=0x2D`。
- 上电：GPIO4 拉低 ≥100ms，再拉高，等待 ≥300ms 后开始收帧。
- 固件用长度字段状态机收帧，主循环不 `delay()`。

解析系数（数据段小端）：

| 字段 | 类型 | 系数 |
| :--- | :--- | :--- |
| `accel` Ax/Ay/Az | int16 | × 0.00478515625 → m/s² |
| `gyro` Gx/Gy/Gz | int16 | × 0.0625 → °/s |
| `mag` Mx/My/Mz | int16 | × 0.006103515625 → μT |
| `quat` Q0–Q3 | int16 | × 0.000030517578125 |
| 温度 | int16 | × 0.01 → ℃ |
| 高度 | uint32 | × 0.0010728836 → m |

## 按键

- GPIO5，内部上拉，低电平有效，消抖 30ms。
- 按下：`button0=1`。
- 松开：`button0=0`。
- 下降沿另发 action：

```json
{"method":"action","action":"key_clicked"}
```

## 设备动作（下行）

协议：`{"method":"action","action":"<名>"}`。固件转成 AS201 串口命令。

| action | AS201 cmd | 说明 |
| :--- | :--- | :--- |
| `mag_cal_start` | `0x1D` | 开始磁场校准 |
| `mag_cal_end` | `0x1E` | 结束磁场校准，offset 写入模块 Flash |

开始：

```json
{"method":"action","action":"mag_cal_start"}
```

结束：

```json
{"method":"action","action":"mag_cal_end"}
```

- 仅九轴/十轴有效。开始前远离磁性物质 ≥20cm。
- `mag_cal_start` 之后绕 X/Y/Z 各转一圈以上（多转更好），限时 1 分钟。
- 超时未转完再 `mag_cal_end`，模块仍会按当前磁场存 offset，校准会差。
- 校准过程看 `mag_accuracy`（3=已校准好）。

## 消息与上报

协议遵循 `docs/MQTT消息类型与格式.md`。只用 set / get / update / report / action。

内部仍按 AS201 约 20Hz 解析并更新属性缓存；对外按 `report_delay_ms` 发 `update`。10 秒心跳 `report` 照旧带全部可读属性。

改上报间隔：

```json
{"method":"set","key":"report_delay_ms","value":200,"msg_id":5001}
```

周期上报示例（字段可多于下列）：

```json
{"method":"update","accel":"000100020003","gyro":"000000000000","mag":"000000000000","quat":"0001000000000000","button0":0}
```

读单个属性：

```json
{"method":"get","key":"quat","msg_id":5002}
```

无 `game_*` 属性。可写属性只有 `sleep_time`、`report_delay_ms`。

## 行为与约束

- 新设备不接原工程 SoftAP / WebSocket 网页。
- 磁力计要准航向需按说明书校准：下行 `mag_cal_start` / `mag_cal_end`，远离磁干扰 ≥20cm。
- 不做设备内玩法。
- 电池：采样前拉高 BAT_EN（GPIO1），采完拉低；ADC 为 GPIO0。
- 属性列表按本文档表格顺序放入 `device_properties[]`。

## 与新增设备流程的对应关系

- 在 `components/base_device/include/base_device.h` 中新增 `DEVICE_DAN01`（数字 11）与 `CONFIG_DEVICE_DAN01`。
- 在 `components/base_device/CMakeLists.txt` 中追加 `DAN01.c`。
- 在 `components/base_device/Kconfig` 中新增 `DEVICE_DAN01`。
- 在 `components/base_device/DAN01.c` 中实现属性与 AS201/按键/电池逻辑。
- 在 `pytest/virtual_devices.py`、`pytest/device_manager.py`、`docs/device_api_reference.md` 中补齐。
