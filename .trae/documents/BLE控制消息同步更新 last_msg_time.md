## 目标

* 收到 BLE 控制消息时也更新 `last_msg_time`，与 MQTT 行为一致，避免被判定为“长时间无消息”而休眠。

## 影响范围

* `e:\develop\smart\hard\project_td\components\base_device\base_device.c:295`

* `e:\develop\smart\hard\project_td\components\base_device\base_device.c:28`（全局变量声明）

* `e:\develop\smart\hard\project_td\components\base_device\include\base_device.h`

* `e:\develop\smart\hard\project_td\main\device_ble_service.c:238-329`（GATT 写事件处理）

* 可选：`e:\develop\smart\hard\project_td\main\blufi_example_main.c:565-568`（BLUFI 自定义数据）

## 实施步骤

1. 在 `base_device.c` 新增 `void update_last_msg_time(void)`，内部执行 `last_msg_time = esp_timer_get_time() / 1000000;`；在 `base_device.h` 声明该函数。
2. 将 `mqtt_msg_process` 尾部的直接赋值（`base_device.c:295`）改为调用 `update_last_msg_time()`，统一入口。
3. 在 `gatts_profile_event_handler` 的 `ESP_GATTS_WRITE_EVT` 分支（`device_ble_service.c:238-329`）完成任何有效写入后调用 `update_last_msg_time()`，确保 BLE 控制写入也刷新时间。

## 验证

* 运行后通过 BLE 写入任意 `writeable` 属性，查看 `sleep_check_task` 日志（`base_device.c:143-145`）中“no message time”是否重置。

* 构建无链接错误，符号可见（`base_device.h` 暴露 `update_last_msg_time`）。

## 代码定位参考

* MQTT 更新时间行：`components/base_device/base_device.c:295`

* BLE 写入处理：`main/device_ble_service.c:238-329`

