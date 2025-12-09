## 可能原因
- 写入数据长度超过特征值的最大长度，当前 `IDX_CHAR_VAL` 的 `max_length` 只有 1，栈自动拒绝写入，应用收不到 `ESP_GATTS_WRITE_EVT`。
- 特征仅声明了 `WRITE`，客户端使用了 `Write Without Response`，属性不匹配导致失败。
- 写入到了不同的句柄（声明/其他特征），代码未打印通用日志，看不到事件。

## 修改方案
- 扩大可写特征值缓冲区与最大长度：将 `char_value` 从 1 字节改为例如 256 字节，并让 `gatt_db[IDX_CHAR_VAL]` 的 `max_length` 使用新长度。
  - 位置：`main/device_ble_service.c:35, 55-59`。
- 允许 `Write Without Response`：将 `char_prop_write` 改为同时包含 `ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR`。
  - 位置：`main/device_ble_service.c:32, 47-52`。
- 增加通用写入事件日志：在 `ESP_GATTS_WRITE_EVT` 开始处打印 `handle` 与 `len`，便于确认是否到达但句柄不匹配。
  - 位置：`main/device_ble_service.c:91-116`。
- 可选：在 `device_ble_service_init` 设置本地 MTU 为 247，以提升单次可写长度（不影响基本功能）。
  - 位置：`main/device_ble_service.c:124-127`。

## 实现要点（简洁）
- `static uint8_t char_value[256];`
- `static const uint8_t char_prop_write = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;`
- 在 `ESP_GATTS_WRITE_EVT` 最前添加：`ESP_LOGI(TAG, "WRITE_EVT handle=0x%04x len=%d", param->write.handle, param->write.len);`
- 可选：`esp_ble_gatt_set_local_mtu(247);`

## 验证
- 用 nRF Connect 连接后，向特征 `0xFF01` 写入字符串（>1 字节）。
- 期望日志：
  - `WRITE_EVT handle=... len=...`
  - `Received N bytes:` 与十六进制内容
  - `String: ...`
- 向 `0xFF02` 写入 `0x01/0x00`，期望看到 `Mode: BLE/WiFi` 日志并对应启动/停止 WiFi。

## 影响面
- 仅 BLE GATT 服务，内存增加 ~256B；逻辑简单，不改动外部接口。