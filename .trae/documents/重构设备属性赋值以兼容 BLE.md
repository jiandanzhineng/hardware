## 目标
- 在 `e:\develop\smart\hard\project_td\components\base_device` 内，将设备运行时的属性赋值改用 `device_update_property_int/float/string`，以在 BLE 模式下触发通知。
- 保留 MQTT 来源的赋值逻辑（`set_property(...)`、各设备 `on_set_property(...)`、MQTT `on_action(...)`）。

## 不改动的 MQTT 路径
- `base_device.c:306–341` 的 `set_property(...)`，包含直接赋值到 `property->value.*`。
- 各设备 `on_set_property(...)` 与 MQTT `on_action(...)` 内的赋值，例如：
  - `dianji.c:183–191`、`dianji.c:193–209`
  - `TD01.c:63–68`
  - `qtz.c` 的带宽设置回调
  - `DZC01.c:642–667`
  - `pj01.c:80–102`
  - `qiya.c:58–63`
  - `zidongsuo.c:95–108`

## 按文件修改
- base_device.c
  - 将电池周期更新改为调用更新函数：
    - 位置：`base_device.c:142–151` 内 `sleep_check_task`
    - 替换：`battery_property.value.int_value = BatteryVoltagePer;` → `device_update_property_int("battery", BatteryVoltagePer);`

- dianji.c
  - 震动结束时的内部状态更新：
    - 位置：`dianji.c:211–225` 内 `stop_shock_task`
    - 替换：`shock_property.value.int_value = 0;` → `device_update_property_int("shock", 0);`
  - NVS 读取后的安全模式赋值：
    - 位置：`dianji.c:693–712` 内 `nvs_dianji_read`
    - 替换：`safe_property.value.int_value = safe_value;` → `device_update_property_int("safe", safe_value);`
  - 已符合要求：电池上报处已使用 `device_update_property_int`（`dianji.c:491–509`）。

- qtz.c
  - 距离读取任务：
    - 位置：`qtz.c:528–546` 内 `check_distance_task`
    - 替换：`distance_property.value.float_value = (float)distance;` → `device_update_property_float("distance", (float)distance);`
  - NVS 读取后的带宽属性写回：
    - 位置：`qtz.c:662–665`（`on_device_init` 尾部）
    - 替换：`low_band_property.value.int_value = low_band;` → `device_update_property_int("low_band", low_band);`
      `high_band_property.value.int_value = high_band;` → `device_update_property_int("high_band", high_band);`
  - 按钮事件属性值更新：
    - 位置：`qtz.c:739–769`（按钮 press/release 回调）
    - 替换：`buttonX_property.value.int_value = 0/1;` → `device_update_property_int("buttonX", 0/1);`
    - 保留 `get_property("buttonX", 0);` 以便在 MQTT 模式下主动上报。

- DZC01.c
  - 称重任务：
    - 位置：`DZC01.c:419–427` 内 `weight_task`
    - 替换：`weight_property.value.int_value = iw;` → `device_update_property_int("weight", iw);`
  - 首次就绪时行文本设置：
    - 位置：`DZC01.c:624–628`
    - 替换：`line1_text_property.value.string_value = "Connected";` → `device_update_property_string("line1_text", "Connected");`
    - 保留随后的 `get_property("line1_text", 0);`。

- pj01.c
  - 仅在 `on_set_property(...)`（MQTT）中赋值，无需改动（`pj01.c:80–102`）。

- qiya.c
  - 传感器数据读取后的属性更新：
    - 位置：`qiya.c:214–250` 内 `read_pressure_data`
    - 替换：
      `pressure_property.value.float_value = current_pressure;` → `device_update_property_float("pressure", current_pressure);`
      `temperature_property.value.float_value = current_temperature;` → `device_update_property_float("temperature", current_temperature);`
  - 保留 `report_pressure_task` 的 MQTT 更新数据包（`qiya.c:252–270`）。

- zidongsuo.c
  - 电池周期更新：
    - 位置：`zidongsuo.c:224–257` 内 `battery_task`
    - 替换：`battery_property.value.int_value = battery_percentage;` → `device_update_property_int("battery", battery_percentage);`
  - 紧急模式触发开锁：
    - 位置：`zidongsuo.c:259–301` 内 `emergency_mode_task`
    - 替换：`open_property.value.int_value = 1;` → `device_update_property_int("open", 1);`

## 兼容性与影响
- BLE：`device_update_property_*` 在 BLE 模式下调用 `device_ble_update_property(i)`（`base_device.c:391–426`），保证属性变更即时通知。
- MQTT：保持原有赋值与主动上报逻辑不变（`set_property/get_property/mqtt_publish`），不会影响现有行为。

## 验证
- 编译并运行两种模式：
  - MQTT 模式：触发按钮与周期任务，确认 MQTT 上报不变。
  - BLE 模式：观察属性更新是否触发 BLE 通知（各属性变更点）。
- 重点验证点：`qtz` 按钮与距离、`DZC01` 称重、`zidongsuo` 电池与应急开锁、`qiya` 压力/温度。

## 备注
- 代码保持简洁，不增加额外错误处理或注释。
- 仅在运行态的本地事件/NVS读取等路径改为 `device_update_property_*`；MQTT 来源保持原逻辑。