## 目标

* 提供以 `device_property_t*` 与具体值为参数的便捷封装，设备端调用后更新属性并在 BLE 模式同步到 Characteristic。

## 变更概述

* 基于属性指针进行更新，避免字符串查找；按类型分别封装 `int/float/string`。

* 在 BLE 服务中新增对外函数，按属性对象刷新 GATT 值并按需发送通知。

## 文件改动

* `components\base_device\base_device.c`

  * 新增：

    * `void device_update_property_int(device_property_t *p, int v)`

    * `void device_update_property_float(device_property_t *p, float v)`

    * `void device_update_property_string(device_property_t *p, const char *v)`

  * 每个函数：

    * 写入 `p->value`。

    * 若 `g_device_mode == MODE_BLE`，调用 `device_ble_update_property(p)`。

* `components\base_device\include\base_device.h`

  * 增加上述三个函数声明。

* `main\device_ble_service.h`

  * 增加 `void device_ble_update_property(device_property_t *p);` 声明（包含 `device_common.h`）。

* `main\device_ble_service.c`

  * 实现 `device_ble_update_property(device_property_t *p)`：

    * 在 `device_properties[]` 中定位指针相等的索引 `i`。

    * 将 `p->value` 序列化到 `prop_value_buf[i]`，设置 `prop_value_len[i]`。

    * `esp_ble_gatts_set_attr_value(handle_table[prop_value_index[i]], prop_value_len[i], prop_value_buf[i])` 刷新 GATT。

    * 若已使能通知并连接，`esp_ble_gatts_send_indicate(s_gatts_if, conn_id_last, handle_table[prop_value_index[i]], prop_value_len[i], prop_value_buf[i], false)`。

## 使用示例

* 将 `e:\develop\smart\hard\project_td\components\base_device\dianji.c:503`：

  * 从 `battery_property.value.int_value = bat_value;`

  * 改为 `device_update_property_int(&battery_property, bat_value);`

## 验证要点

* BLE 模式下修改后，手机端能读到最新值；如启用通知可收到 Indicate。

* WiFi 模式下行为不变；MQTT 心跳与上报逻辑保持。

