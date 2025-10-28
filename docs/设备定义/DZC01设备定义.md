# DZC01 电子秤设备定义（HX711 + OLED）

本设备以 HX711 称重传感器和 SSD1306 OLED 显示屏为核心，提供 0–5kg 的重量采集与本地显示能力，遵循项目现有设备属性与消息协议约定。

## 设备标识

- 设备类型：`DZC01`（新增时需按《新增设备类型指南》在固件中注册）
- 适用场景：桌面/便携电子秤，支持去皮与校准，支持本地 OLED 显示

## 硬件接口（参考示例）

- HX711：`SCK=GPIO 0`，`DT=GPIO 1`（见 `mpyDemo/DZC01/hx711_demo.py`）
- OLED（SSD1306 I2C）：`SCL=GPIO 6`，`SDA=GPIO 7`（见 `mpyDemo/DZC01/oled_test.py`）
- 指示灯：`GPIO 10`（与现有设备的 `CONNECTED_LED` 约定一致）
- 量程：0–5000 g（5kg），滤波后输出，超出范围自动夹紧到 [0, 5000]

## 通用设备属性（与现有设备一致）

| 属性名 | 类型 | 读写 | 描述 | 默认值 |
|---|---|---|---|---|
| `device_type` | string | 只读 | 设备类型标识 | `DZC01` |
| `sleep_time` | int | 读写 | 休眠时间（秒） | 7200 |
| `battery` | int | 只读 | 电池电量（%） | 0 |

> 注意：依据《Feature.md》，“必须在开关开启情况下才能测得电池电压”。若硬件采用开关式测量使能，需在开关开启时采样电池电压。

## DZC01 设备属性

| 属性名 | 类型 | 读写 | 描述 | 默认值 | 范围/枚举 |
|---|---|---|---|---|---|
| `weight` | float | 只读 | 当前重量（g） | 0.0 | 0.0–5000.0（g 等效） |
| `report_delay_ms` | int | 读写 | 周期上报与显示刷新间隔 | 500 | 100–5000 |
| `display_mode` | int | 读写 | 显示模式 | 1 | 0=关闭，1=显示重量，2=自定义文本 |
| `display_on` | int | 读写 | 显示屏开关 | 1 | 0/1 |
| `display_contrast` | int | 读写 | OLED对比度（亮度） | 255 | 0–255 |
| `line1_text` | string | 读写 | 自定义文本第1行 | "" | 最多32字符 |
| `line2_text` | string | 读写 | 自定义文本第2行 | "" | 最多32字符 |

说明：
- 当 `display_mode=1` 且 `display_on=1` 时，OLED 显示当前滤波后的重量与单位。
- 当 `display_mode=2` 时，OLED 显示 `line1_text` 与 `line2_text`；若为空则显示默认占位。
- `weight` 始终以滤波值上报；单位切换仅影响显示与上报语义，不改变内部换算精度。

## 设备方法（动作）

动作通过 MQTT `method=action` 下发，设备在 `on_action` 中处理。

### 1) `tare` — 去皮
- 作用：以当前传感器输出作为零点（更新内部 `offset`）。
- 参数：无
- 示例：
```json
{
  "method": "action",
  "action": "tare"
}
```

### 2) `calibrate` — 校准
- 作用：使用已知砝码重量进行校准（更新 `calval` 与比例系数）。
- 参数：`known_weight_g` (int)：砝码重量（克）
- 示例：
```json
{
  "method": "action",
  "action": "calibrate",
  "known_weight_g": 100
}
```

### 3) `display_text` — 显示自定义文本
- 作用：立即切换到自定义文本显示，并刷新 OLED。
- 参数：`line1` (string)、`line2` (string)、`duration_ms` (int, 可选，显示时长，默认持续显示)、`invert` (int, 可选，0/1 反色)
- 示例：
```json
{
  "method": "action",
  "action": "display_text",
  "line1": "Hello",
  "line2": "World",
  "duration_ms": 3000,
  "invert": 0
}
```

### 4) `clear_display` — 清屏
- 作用：清空 OLED 并保持当前显示模式；若 `display_mode=1`，清屏后下一周期会重新显示重量。
- 参数：无
- 示例：
```json
{
  "method": "action",
  "action": "clear_display"
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
  "unit": "kg",
  "msg_id": 1002
}
```

- 读取属性（Get）：
```json
{
  "method": "get",
  "key": "weight",
  "msg_id": 1003
}
```

- 设备定期上报：按 `report_delay_ms` 周期上报可读属性（含 `weight`）。设备启动后也会进行一次整体属性上报与心跳。

## 行为与约束

- 采样滤波：采用滑动去极值平均（见 `mpyDemo/DZC01/hx711.py` 的 `_filter`），避免抖动。
- 单位切换：`unit` 仅影响显示与上报，不影响内部精度与范围控制。
- 电池测量：遵循“开关开启才能测电池”的约束；相关引脚需按硬件设计使能后采样。
- OLED 亮度：使用 `display_contrast` 调整（SSD1306 `SET_CONTRAST`），建议在 100–255 范围使用。

## 与新增设备流程的对应关系

- 在 `components/base_device/include/base_device.h` 中新增 `DEVICE_DZC01` 与 `CONFIG_DEVICE_DZC01` 条目。
- 在 `components/base_device/CMakeLists.txt` 中按配置追加 `DZC01.c`。
- 在 `components/base_device/Kconfig` 的 `choice DEVICE_SELECTION` 中新增 `DEVICE_DZC01` 选项与帮助信息。
- 在 `components/base_device/DZC01.c` 中实现上述属性与动作处理逻辑，并将 `device_properties[]` 按本文档顺序组织。

---

本文档旨在简洁定义 DZC01 的属性、动作与消息语义，确保与现有设备生态一致并便于后续固件与测试集成。