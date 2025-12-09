## 目标

* 在 `e:\develop\smart\hard\project_td\main\device_ble_service.c` 新增一个可写特征用于切换控制模式：`WiFi(0x00)` / `BLE(0x01)`。

* 默认模式为 `WiFi`（与当前逻辑一致）。写 `BLE` 时停止 WiFi 协议栈；不处理写回wifi操作。

## 设计

* 新增 16-bit UUID：`0xFF02` 作为“控制模式”特征。

* 值为 1 字节：`0x00` 表示 WiFi，`0x01` 表示 BLE。

* 属性：`WRITE`（简单直接；如需读取可后续加 `READ`）。

## 代码改动

* `device_ble_service.c`

  * 枚举索引扩展：在现有 `IDX_*` 中加入 `IDX_MODE_CHAR_DEC`、`IDX_MODE_CHAR_VAL`（保持表顺序）。参考枚举位置 `main/device_ble_service.c:17-22`。

  * 常量：新增 `#define MODE_CHAR_UUID 0xFF02`，新增 `uint8_t mode_value[1] = {0x00}`（默认 WiFi）。

  * GATT 数据库：在 `gatt_db` 中按现有写特征的模式添加两项（声明 + 值）。参考 `main/device_ble_service.c:33-54` 的结构。

  * 事件处理：在 `ESP_GATTS_WRITE_EVT` 中判断 `param->write.handle` 是否为“控制模式”值句柄；

    * 若写入 `0x01`：调用 `esp_wifi_stop()`；

    * 若写入 `0x00`：调用 `esp_wifi_start()`；

    * 更新 `mode_value[0]` 并打印日志。参考处理位置 `main/device_ble_service.c:56-92`。

  * 头文件：新增 `#include "esp_wifi.h"`。

## 运行时行为

* 默认启动后保持当前 WiFi 逻辑（`initialise_wifi()` 已 `esp_wifi_start()`）。

* 写入 BLE 模式后仅停止 WiFi（保留 BLE GATT 服务用于控制）。

* 不做持久化（NVS）与复杂错误处理，保持简洁。

## 相关代码位置（参考）

* `main/blufi_example_main.c`

  * WiFi 启动：`esp_wifi_start()` 在 `initialise_wifi()`（约 `main/blufi_example_main.c:392`）。

  * WiFi 模式设置：`example_event_callback()` 处理 `ESP_BLUFI_EVENT_SET_WIFI_OPMODE`（约 407+）。

  * BLUFI 初始化：`esp_blufi_host_and_cb_init(&example_callbacks)`（`main/blufi_example_main.c:641`）。

* 如后续需要彻底退出 BLUFI，可用 `esp_blufi_host_deinit()`（定义在 `main/blufi_init.c`）。

## 测试

* 连接 BLE，向新特征写入：

  * 写 `0x01`：观察日志 WiFi 停止，设备仅保留 BLE；

  * 写 `0x00`：观察日志 WiFi 启动并恢复原逻辑。

* 验证其他 BLE 写特征不受影响（现有 `CHAR_UUID 0xFF01`）。

## 备注

* 保持最小改动：仅增加一个特征与简洁的 `stop/start` 调用。

* 后续如需读状态或持久化，可增 `READ` 权限或加入 NVS。

