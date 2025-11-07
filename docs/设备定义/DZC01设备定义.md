# DZC01 电子秤设备定义（HX711 + OLED）

本设备以 HX711 称重传感器和 SSD1306 OLED 显示屏为核心，提供 0–5kg 的重量采集与本地显示能力，遵循项目现有设备属性与消息协议约定。

## 设备标识

- 设备类型：`DZC01`（新增时需按《新增设备类型指南》在固件中注册）
- 适用场景：桌面/便携电子秤，支持去皮与校准，支持本地 OLED 显示

## 硬件接口（参考示例）

- HX711：`SCK=GPIO 0`，`DT=GPIO 1`（见 `mpyDemo/DZC01/hx711_demo.py`）
- OLED（SSD1306 I2C）：`SCL=GPIO 6`，`SDA=GPIO 7`（见 `mpyDemo/DZC01/oled_test.py`）I2C采用esp32c3的硬件功能 不用软实现
- 按键：

  - KEY1: `GPIO 3` (低电平触发)
  - KEY2: `GPIO 2` (低电平触发)
  - KEY3: `GPIO 19` (低电平触发)
  - KEY4: `GPIO 18` (低电平触发)
- 按键功能：

  - KEY1: 开关机。正常状态下单击进入深度睡眠；在深度睡眠模式下单击唤醒恢复正常状态。
  - KEY2: 自定义功能。单击后触发 `key_clicked` 事件。
  - KEY3: 清零（去皮）。
  - KEY4: 校准（此时重量必须为 500g）。
- 量程：0–5000 g（5kg），滤波后输出，超出范围自动夹紧到 [0, 5000]

## 通用设备属性（与现有设备一致）

| 属性名          | 类型   | 读写 | 描述           | 默认值    |
| --------------- | ------ | ---- | -------------- | --------- |
| `device_type` | string | 只读 | 设备类型标识   | `DZC01` |
| `sleep_time`  | int    | 读写 | 休眠时间（秒） | 7200      |
| `battery`     | int    | 只读 | 电池电量（%）  | 0         |

## DZC01 设备属性

| 属性名               | 类型   | 读写 | 描述                   | 默认值 | 范围/枚举                              |
| -------------------- | ------ | ---- | ---------------------- | ------ | -------------------------------------- |
| `weight`           | float  | 只读 | 当前重量（g）          | 0.0    | 0.0–5000.0（g 等效）                  |
| `report_delay_ms`  | int    | 读写 | 周期上报与显示刷新间隔 | 500    | 100–5000                              |
| `display_mode`     | int    | 读写 | 显示模式               | 1      | 0=关闭，1=显示重量和电量，2=自定义文本 |
| `display_on`       | int    | 读写 | 显示屏开关             | 1      | 0/1                                    |
| `display_contrast` | int    | 读写 | OLED对比度（亮度）     | 255    | 0–255                                 |
| `line1_text`       | string | 读写 | 自定义文本第1行        | ""     | 最多32字符                             |
| `line2_text`       | string | 读写 | 自定义文本第2行        | ""     | 最多32字符                             |

说明：

- 当 `display_mode=1` 且 `display_on=1` 时，OLED 显示当前滤波后的重量与电量。
- 当 `display_mode=2` 时，OLED 显示 `line1_text` 与 `line2_text`；若为空则显示默认占位。
- `weight` 始终以滤波值上报；范围控制不改变内部精度。

## 设备事件

设备通过 MQTT `method=event` 主动上报事件。

### 1) `key_clicked` — 按键单击事件

- 作用：当 KEY2 被单击时，设备上报此事件。
- 参数：`key_id` (int)：被单击的按键编号。
- 示例：

```json
{
  "method": "action",
  "action": "key_clicked"
}
```

## 消息与上报

- 设置属性（Set）：

```json
{
  "method": "set",
  "key": "report_delay_ms",
  "value": 500,
  "msg_id": 1001
}
```

- 批量更新（Update）：

```json
{
  "method": "update",
  "display_mode": 1,
  "display_contrast": 180,
  "msg_id": 1002
}
```

- 设备定期上报：按 `report_delay_ms` 周期上报可读属性（含 `weight`）。设备启动后也会进行一次整体属性上报与心跳。

# 显示功能

1. 设备启动时 显示 简单智能电子秤V1 等待配网
2. 设备联网后（on_device_first_ready） 显示配网成功
3. 之后显示重量和电量或其他来自控制端的内容

## 行为与约束

- 采样滤波：采用滑动去极值平均（见 `mpyDemo/DZC01/hx711.py` 的 `_filter`），避免抖动。
- 电池测量：遵循“开关开启才能测电池”的约束；相关引脚需按硬件设计使能后采样。
- OLED 亮度：使用 `display_contrast` 调整（SSD1306 `SET_CONTRAST`），建议在 100–255 范围使用。

## 与新增设备流程的对应关系

- 在 `components/base_device/include/base_device.h` 中新增 `DEVICE_DZC01` 与 `CONFIG_DEVICE_DZC01` 条目。
- 在 `components/base_device/CMakeLists.txt` 中按配置追加 `DZC01.c`。
- 在 `components/base_device/Kconfig` 的 `choice DEVICE_SELECTION` 中新增 `DEVICE_DZC01` 选项与帮助信息。
- 在 `components/base_device/DZC01.c` 中实现上述属性与动作处理逻辑，并将 `device_properties[]` 按本文档顺序组织。

---

本文档旨在简洁定义 DZC01 的属性、动作与消息语义，确保与现有设备生态一致并便于后续固件与测试集成。
