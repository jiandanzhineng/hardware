# 手动 OTA 升级方案

当前项目的 OTA 不是自动轮询升级，而是由 MQTT 指令手动触发。设备收到固件 URL 后，使用 `esp_https_ota` 下载应用镜像，写入备用 OTA 分区，完成后重启。

## 1. MQTT 协议

### 触发升级

服务端向设备订阅的主题发送：

- Topic: `/drecv/{mac}`
- Payload:

```json
{
  "method": "ota_update",
  "url": "http://your-server/under_silicon.bin"
}
```

字段说明：

- `method`: 固定为 `ota_update`
- `url`: 应用固件 `.bin` 的完整 HTTP/HTTPS 地址，不是 merged bin

### 状态上报

设备向发布主题上报：

- Topic: `/dpub/{mac}`
- Payload:

```json
{
  "method": "ota_status",
  "status": "downloading",
  "progress": 40,
  "msg": "Optional message"
}
```

状态值：

- `start`: 开始 OTA
- `downloading`: 下载中，当前代码每 10% 上报一次
- `success`: 写入完成，即将重启
- `failed`: OTA 失败

## 2. 设备端流程

1. `components/smqtt/smqtt.c` 初始化 MQTT topic：
   - 发布: `/dpub/{mac}`
   - 订阅: `/drecv/{mac}` 和 `/all`
2. `components/base_device/base_device.c` 的 `mqtt_msg_process()` 解析 MQTT 消息。
3. 当 `method` 为 `ota_update` 时，读取 `url` 字段并调用 `ota_perform_update(url)`。
4. `components/ota_update/ota_update.c` 创建后台 FreeRTOS 任务 `ota_task`。
5. OTA 任务调用 `esp_https_ota_begin()`、`esp_https_ota_perform()`、`esp_https_ota_finish()` 完成下载和写入。
6. 成功后上报 `success`，延迟 1 秒调用 `esp_restart()` 重启。

当前实现会读取新固件的 `esp_app_desc_t.version` 并打印日志，但不会做版本比较或 URL 合法性校验。

## 3. 分区与回滚

项目使用自定义分区表 `partitions.csv`：

- `otadata`
- `ota_0`: `0x1E0000`
- `ota_1`: `0x1E0000`

`sdkconfig.defaults` 开启了：

```ini
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y
CONFIG_ESP_HTTPS_OTA_ALLOW_HTTP=y
```

升级后首次启动时，`device_first_ready()` 会调用 `ota_mark_app_valid()`。如果当前分区处于 `ESP_OTA_IMG_PENDING_VERIFY`，则调用 `esp_ota_mark_app_valid_cancel_rollback()` 标记固件有效并取消回滚。

## 4. 测试步骤

1. 编译生成应用镜像，例如 `build/under_silicon.bin`。
2. 将该 `.bin` 放到 HTTP/HTTPS 服务器。
3. 通过 MQTT 向 `/drecv/{mac}` 下发：

```json
{
  "method": "ota_update",
  "url": "http://your-server/under_silicon.bin"
}
```

4. 观察设备日志和 `/dpub/{mac}` 的 `ota_status` 上报。
5. 看到 `success` 后设备会自动重启，重启后心跳 `report` 中的 `ver` 字段应变为新固件版本。
