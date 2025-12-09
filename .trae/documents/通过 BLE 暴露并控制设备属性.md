## 增补点
- 所有可读属性：增加 `Notify` 能力，并添加 CCCD 描述符（`ESP_GATT_UUID_CHAR_CLIENT_CONFIG`）。
- `gatt_db` 总条目数：按属性读写能力动态计算，严格受 `device_properties_num` 影响。

## 条目数计算
- 基础固定 5 项：服务+现有两特征（声明+值×2）。
- 每个属性：
  - 最少 2 项：特征声明 + 特征值（当 `readable==0` 且 `writeable==1` 或 `readable==1` 且 `writeable==0/1`）。
  - 若 `readable==1`：再加 1 项 CCCD（客户端配置描述符，用于开启通知）。
- 总数：`total = 5 + Σ(2 + (readable?1:0))`，其中 Σ 遍历 `device_properties_num`。

## 设计细节
- UUID：属性特征使用 16-bit 连续 UUID `0xFF10 + i`；CCCD 使用标准 16-bit `0x2902`。
- 特征属性位：
  - `readable==1` → `ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY`
  - `writeable==1` → 加 `ESP_GATT_CHAR_PROP_BIT_WRITE`
- 权限：
  - 值：按 `readable/writeable` 组合 `ESP_GATT_PERM_READ/WRITE`
  - CCCD：`ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE`
- 值缓冲：为每个属性分配缓冲并初始化为当前值；作为 `AUTO_RSP` 的读响应源。
- 句柄映射：记录属性“值句柄”和“CCCD句柄”（可读属性），并维护 `notify_enabled[i]` 状态。

## 事件处理
- `ESP_GATTS_WRITE_EVT`：
  - 写到属性“值句柄”：解析 ASCII（int/float）或原样（string），更新属性与缓冲，调用 `on_set_property(...)`；若 `notify_enabled[i]` 为真，`esp_ble_gatts_send_indicate(...)` 主动通知最新值。
  - 写到属性“CCCD句柄”：解析 2 字节（0x0001 开启、0x0000 关闭），更新 `notify_enabled[i]`。
  - 保留 `MODE_CHAR_UUID` 逻辑不变。

## 代码改动范围
- 仅改 `main/device_ble_service.c:39` 附近逻辑：将静态 `gatt_db` 改为运行时构建，增加属性特征与 CCCD；保存句柄与通知状态；集成写事件处理。
- 引入 `device_common.h`，并声明 `extern device_property_t *device_properties[]; extern int device_properties_num;`。

## 验证
- 编译通过；连接 BLE：服务下出现 `device_properties_num` 对应数量的属性特征；
- 读：可读属性能读出当前值；
- 通知：手机端写 CCCD=0x0001 后，再写属性值可收到 Notify；
- 写：写入后日志显示更新，`report_all_properties()` 中值一致。

## 备注
- 按需可将 16-bit 连续 UUID 升级为 128-bit Vendor UUID；现方案更简洁易用。