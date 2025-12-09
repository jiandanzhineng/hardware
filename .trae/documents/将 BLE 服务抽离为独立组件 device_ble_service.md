## 目标
- 把 `main/device_ble_service.c` 与 `main/device_ble_service.h` 抽离为独立组件，简化 `main` 依赖并提升复用性。
- 保持现有功能与 API 不变：`device_ble_service_init()`、`device_ble_update_property()`。

## 当前使用点
- 入口调用：`app_main()` 中调用 `device_ble_service_init()`，见 `main/blufi_example_main.c:649`。
- 具体实现：`device_ble_service_init()` 定义于 `main/device_ble_service.c:343`。

## 新组件结构
- 新建目录：`components/device_ble_service/`
- 文件布局：
  - `components/device_ble_service/device_ble_service.c`（从 `main` 迁移）
  - `components/device_ble_service/include/device_ble_service.h`（从 `main` 迁移）
  - `components/device_ble_service/CMakeLists.txt`

## 新组件 CMakeLists.txt
- 内容（简洁依赖，与现有代码一致）：
```
idf_component_register(
  SRCS "device_ble_service.c"
  INCLUDE_DIRS "include"
  REQUIRES "base_device" "esp_wifi" "bt" "json"
)
```
- 说明：
  - `base_device` 提供 `device_common.h`、设备属性与回调。
  - `bt`/`esp_wifi` 提供 GATT/Wi-Fi API。
  - `json` 提供 `cJSON`。

## 调整 main 组件
- 修改 `main/CMakeLists.txt`：
  - 从 `SRCS` 移除 `"device_ble_service.c"`。
  - 在 `REQUIRES` 增加 `"device_ble_service"` 以获得头文件与链接。
- 示例改动：
```
- idf_component_register(SRCS "blufi_example_main.c"
-                             "blufi_security.c"
-                             "blufi_init.c"
-                             "device_ble_service.c"
-                     INCLUDE_DIRS "."
-                     REQUIRES  "esp_wifi" "nvs_flash" "bt" "json" "smqtt" "mqtt" "esp_netif" "base_device" "driver")
+ idf_component_register(SRCS "blufi_example_main.c"
+                             "blufi_security.c"
+                             "blufi_init.c"
+                     INCLUDE_DIRS "."
+                     REQUIRES  "esp_wifi" "nvs_flash" "bt" "json" "smqtt" "mqtt" "esp_netif" "base_device" "driver" "device_ble_service")
```
- 其它代码不需改动，`#include "device_ble_service.h"` 保持不变（头文件将由新组件的 `INCLUDE_DIRS` 暴露）。

## 兼容与依赖
- BLE 服务依赖的外部符号（如 `device_properties[]`、`on_set_property()`、`g_device_mode` 等）由 `base_device`/`device_common.h` 提供，无需额外改动。
- GATT/Wi-Fi API 依赖在新组件 `REQUIRES` 中指明即可。

## 验证步骤
- 构建：在项目根执行 `idf.py fullclean build` 确认无编译/链接错误。
- 运行：`idf.py flash monitor`，观察日志：
  - `ESP_GATTS_REG_EVT`、`ESP_GATTS_CREAT_ATTR_TAB_EVT` 正常（`device_ble_service.c:219`）。
  - 收到写事件与属性通知逻辑正常（`device_ble_service.c:241`）。
- 功能检查：
  - `app_main()` 启动后 BLE 服务仍可创建、属性读写/通知正常。

## 交付结果
- 新组件 `device_ble_service` 提供 BLE GATT 服务，`main` 更轻量、依赖更清晰，后续可在其它工程重用该组件。
