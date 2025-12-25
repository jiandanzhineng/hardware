# 手动 OTA 升级方案

本方案已更新。根据最新需求，设备不再自动定期检查更新，而是完全由 MQTT 指令触发，并且支持在指令中指定固件 URL。

## 1. 总体架构

方案采用 **MQTT 通知触发** + **HTTP Pull** 的方式：
1. **指令触发**：服务器通过 MQTT 向设备发送升级指令，指令中包含固件的下载地址（URL）。
2. **被动执行**：设备收到指令后，解析 URL，立即启动下载任务。
3. **安全升级**：使用 `esp_https_ota` 组件进行下载与写入，支持断点续传（视服务器支持情况）和回滚机制。
4. **进度上报**：设备通过 MQTT 实时上报升级进度（Start, Downloading %, Success/Fail）。

## 2. MQTT 指令协议

### 2.1 触发升级 (Server -> Device)
**Topic**: `/drecv/{mac}`
**Payload**:
```json
{
    "method": "ota_update",
    "url": "http://192.168.1.100/firmware.bin"
}
```
*   `method`: 固定为 `"ota_update"`。
*   `url`: 固件文件的完整 HTTP/HTTPS 地址。

### 2.2 状态上报 (Device -> Server)
**Topic**: `/dpub/{mac}`
**Payload**:
```json
{
    "method": "ota_status",
    "status": "downloading",
    "progress": 45,
    "msg": "Optional message"
}
```
*   `status`: 状态字符串，包括 `"start"`, `"downloading"`, `"success"`, `"failed"`。
*   `progress`: 进度百分比 (0-100)。

## 3. 设备端实现

### 3.1 组件 `ota_update`

- **ota_update.h**: 提供 `ota_perform_update(const char *url)` 接口。
- **ota_update.c**:
    - 实现 `ota_perform_update`，创建一个后台任务。
    - 任务中调用 `esp_https_ota` 直接下载指定 URL 的固件。
    - 下载过程中通过 MQTT 上报进度。

### 3.2 业务集成

- **main/blufi_example_main.c**:
    - 不再调用 `ota_service_init()`（已移除），因为不需要后台定时任务。
    
- **components/base_device/base_device.c**:
    - 在 MQTT 消息处理函数 `on_mqtt_msg_process` 中监听 `ota_update` 方法。
    - 解析 `url` 字段。
    - 调用 `ota_perform_update(url)`。

### 3.3 分区表配置 (Partition Table)

由于固件体积（约1.4MB）超过了默认分区表每个 App 分区 1MB 的限制，必须使用自定义分区表。

- **partitions.csv**:
    - 移除了 Factory 分区（为 OTA 腾出空间）。
    - 配置了两个较大的 OTA 分区 (`ota_0`, `ota_1`)，每个约 1.9MB。
    
- **sdkconfig.defaults**:
    - `CONFIG_PARTITION_TABLE_CUSTOM=y`
    - `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`

## 4. 验证与测试

1. **部署固件**：将编译好的 `blufi_demo.bin` 放到 HTTP 服务器。
2. **发送指令**：
    使用 MQTT 工具发送：
    ```json
    {
        "method": "ota_update",
        "url": "http://your-server-ip/blufi_demo.bin"
    }
    ```
3. **观察日志**：
    - 设备应收到指令并打印 `Starting OTA update from ...`。
    - 随后开始下载并打印进度。
    - 下载完成后自动重启。
