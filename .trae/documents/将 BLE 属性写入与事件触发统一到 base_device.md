## 目标
- 更新 BLE 写入路径，统一通过 base_device 完成属性值的落库与事件回调。
- 减少重复逻辑：移除 device_ble_service.c 中直接调用 `on_set_property(...)`，改为调用新方法。
- 让 `set_property(...)` 也复用同一核心方法，保持 MQTT 与 BLE 行为一致。

## 改动点
- 在 `components/base_device/base_device.c` 新增方法：`apply_property_update(char *property_name, cJSON *property_value, int msg_id)`。
  - 按 `property_name` 找到 `device_property_t`。
  - 根据 `value_type` 更新 `property->value`（int/float/string）。
  - 调用 `on_set_property(property_name, property_value, msg_id)`。
- 在 `components/base_device/include/base_device.h` 声明新方法原型，供 BLE 使用。
- 重构 `set_property(...)`：删除内部直接调用 `on_set_property(...)` 的逻辑，改为调用 `apply_property_update(...)` 完成更新与事件触发。

## 迁移调用
- 在 `main/device_ble_service.c`，将以下位置的调用替换为新方法：
  - `main/device_ble_service.c:287`、`302`、`312`：用 `apply_property_update(p->name, val, 0)` 替换 `on_set_property(p->name, val, 0)`。
  - 同时移除对 `p->value.*` 的直接赋值，让属性值由 base_device 统一更新；保留 `prop_value_buf[i]`/`prop_value_len[i]` 的维护以及通知发送逻辑。
- `for (int i = 0; i < device_properties_num; i++)` 循环内（`main/device_ble_service.c:268` 起），不再手动更新 `p->value`，只计算 `v/f/string` 用于 BLE 通知缓冲，属性落库通过 `apply_property_update(...)` 完成。

## 验证
- 编译并运行，确保：
  - MQTT `set`/`update` 路径仍能更新属性并触发回调（`components/base_device/base_device.c:298` 起）。
  - BLE 写入路径更新后，属性内部值与通知缓冲一致，且事件回调从 base_device 统一触发。

## 影响与兼容
- 设备侧自定义 `on_set_property(...)` 的实现不变（位于各设备文件），只改变触发入口。
- 现有对 `set_property(...)` 的调用无需改动，行为保持一致但由新核心方法承载。