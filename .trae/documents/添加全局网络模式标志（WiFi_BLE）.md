## 目标
- 提供一个全局变量，任何文件可直接读取/修改当前网络模式（WiFi/BLE）。
- 默认开机为 `WiFi` 模式，BLE 写入切换时同步更新。

## 放置位置
- `components/base_device/include/device_common.h`：声明模式枚举与全局变量（`extern`）。
- `main/blufi_example_main.c`：定义全局变量并初始化为 `WiFi`。

## 具体改动
- `components/base_device/include/device_common.h`
  - 添加：`typedef enum { MODE_WIFI = 0, MODE_BLE = 1 } device_mode_t;`
  - 添加：`extern volatile device_mode_t g_device_mode;`
- `main/blufi_example_main.c`
  - 文件顶部定义：`volatile device_mode_t g_device_mode = MODE_WIFI;`
- `main/device_ble_service.c`
  - BLE 分支：在 `esp_wifi_stop()` 附近直接写 `g_device_mode = MODE_BLE;`（参考 `main/device_ble_service.c:256-257`）。
  - WiFi 分支：在 `esp_wifi_start()` 附近直接写 `g_device_mode = MODE_WIFI;`。

## 使用方法
- 其他文件：`#include "device_common.h"`，直接读写 `g_device_mode`（例如：`if (g_device_mode == MODE_BLE) { ... }`）。

## 验证
- 编译通过；写入 BLE “模式”特征后日志 `Mode: BLE/WiFi` 与 `g_device_mode` 值一致。
- 默认启动后 `g_device_mode == MODE_WIFI`。

## 原则
- 极简实现，不添加函数或多余注释，仅使用一个枚举与一个全局变量。