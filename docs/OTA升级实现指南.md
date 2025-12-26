# OTA升级实现指南（ESP32C3 / ESP-IDF 5.x）

本文面向本项目 `under_silicon`，指导如何为设备增加可靠、安全、可观测的 OTA（Over‑The‑Air）升级能力，并与现有 BLUFI 配网与 MQTT 通信框架整合。

## 背景与现状

- 设备通过 BLUFI 配网（`main/`）接入 Wi‑Fi，联网后启动 MQTT（`components/smqtt`），订阅 `/all` 与 `/drecv/{mac}`，上报到 `/dpub/{mac}`。
- 设备属性、心跳与动作处理集中在 `components/base_device`，每 10s 上报一次心跳（`report_all_properties()`）。
- CI 会构建并发布固件（见 `.github/workflows/build-firmware.yml`）。当前 Release 仅上传了 `*_merged.bin`（合并镜像）。OTA 应使用「应用镜像」`under_silicon.bin`，而非合并镜像。

## 整体方案

采用 HTTPS OTA（`esp_https_ota`）从服务器下载「应用镜像」并写入 OTA 分区，支持：

1. 双分区与回滚：启用 `ota_0`/`ota_1`，打开 Bootloader 回滚；新镜像首次启动后稳定运行，主动标记为有效。
2. MQTT 触发与进度上报：在 `/drecv/{mac}` 下发 JSON 命令触发 OTA，设备在 `/dpub/{mac}` 实时上报进度与结果。
3. 版本治理：设备上报 `firmware_version`，云端根据版本策略决定是否下发升级。
4. 安全校验：HTTPS/TLS 校验证书，且可选对镜像 `sha256` 做完整性校验。

## 准备与配置

### 1) 分区与 Bootloader 配置（menuconfig）

- `Partition Table ->` 选择 `Two OTA definitions`（或自定义包含 `factory + ota_0 + ota_1`）。
- `Bootloader -> Security -> Enable app rollback`：`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`。
- `Application -> Support invalid app checks`：`CONFIG_APP_UPDATE_CHECK_APP_VALIDITY=y`。
- 根据芯片 Flash 大小调整 `Serial flasher config -> Flash size`（常见 4MB/8MB）。

> 提示：如没有自定义分区表文件，使用 ESP‑IDF 提供的双 OTA 默认表即可。

### 2) 使能 HTTPS OTA

- `Component config -> ESP HTTPS OTA` 启用；如用证书包，开启 `MBEDTLS_CERTIFICATE_BUNDLE`。

### 3) 固件版本植入与发布

- 在 `CMakeLists.txt` 设置版本（推荐）：

  ```cmake
  # 项目根 CMakeLists.txt
  project(under_silicon VERSION 1.2.3)
  ```

  或 CI 传入：`-DPROJECT_VER=${{ github.ref_name }}`（从 tag 推导）。

- 更新 Release 附件，确保上传 `build/under_silicon.bin`（应用镜像），并建议为不同设备类型生成独立命名：`under_silicon_<DEVICE>.bin`。

> 现有 CI 仅上传了 `*_merged.bin`，它包含 bootloader/分区表/应用镜像，不适用于 OTA。请在 Release 步骤中同时上传 `build/under_silicon.bin`。

## 设备端实现步骤

### 1) 新增 OTA 组件示例（`components/ota_update/ota_update.c`）

```c
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "mqtt_client.h"

extern esp_mqtt_client_handle_t smqtt_client;
extern char publish_topic[32];
static const char *TAG = "ota";

static void publish_ota_status(const char *phase, int percent, const char *msg) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "method", "ota_status");
    cJSON_AddStringToObject(root, "phase", phase);
    cJSON_AddNumberToObject(root, "percent", percent);
    if (msg) cJSON_AddStringToObject(root, "message", msg);
    char *json = cJSON_PrintUnformatted(root);
    esp_mqtt_client_publish(smqtt_client, publish_topic, json, 0, 1, 0);
    cJSON_free(json); cJSON_Delete(root);
}

void ota_start_https(const char *url, const char *server_cert_pem /*可为NULL*/) {
    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
        .cert_pem = server_cert_pem,
    };
    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };

    publish_ota_status("start", 0, url);
    esp_https_ota_handle_t handle = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_cfg, &handle);
    if (ret != ESP_OK) { publish_ota_status("failed", 0, "begin failed"); return; }

    int total = esp_https_ota_get_image_len(handle);
    int last_pct = -1;
    while (1) {
        ret = esp_https_ota_perform(handle);
        if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            int read = esp_https_ota_get_image_len_read(handle);
            int pct = (total > 0) ? (read * 100 / total) : 0;
            if (pct >= last_pct + 5) { publish_ota_status("downloading", pct, NULL); last_pct = pct; }
            continue;
        }
        break;
    }

    if (ret == ESP_OK && esp_https_ota_is_complete_data_received(handle)) {
        ret = esp_https_ota_finish(handle);
        if (ret == ESP_OK) {
            publish_ota_status("finished", 100, "reboot");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        } else {
            publish_ota_status("failed", last_pct, "finish failed");
        }
    } else {
        esp_https_ota_abort(handle);
        publish_ota_status("failed", last_pct, "perform/recv failed");
    }
}

// 开机后验证：在设备稳定运行一定时间后调用，取消回滚标记
void ota_mark_app_valid(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *app = esp_ota_get_app_description();
    ESP_LOGI(TAG, "running part: %s, ver: %s", running->label, app->version);
    esp_ota_mark_app_valid_cancel_rollback();
}
```

将该组件加入 `idf_component_register` 并在 `main` 中于设备稳定后调用 `ota_mark_app_valid()`（例如 `device_first_ready()` 启动后，延迟 30–60s 再调用）。

### 2) 解析 MQTT OTA 命令

在 `components/base_device/base_device.c` 的 `mqtt_msg_process()` 增加一个分支：

```c
else if (strcmp(method, "ota") == 0) {
    const char *url = cJSON_GetObjectItem(root, "url")->valuestring;
    const char *sha256 = cJSON_GetObjectItem(root, "sha256") ? cJSON_GetObjectItem(root, "sha256")->valuestring : NULL;
    const char *version = cJSON_GetObjectItem(root, "version") ? cJSON_GetObjectItem(root, "version")->valuestring : NULL;
    // 可根据 version/force 做策略判断
    ota_start_https(url, /*server_cert_pem*/ NULL);
}
```

> 证书策略：
> - 使用 GitHub Releases 等公开 HTTPS，可启用 `esp_crt_bundle_attach`（`MBEDTLS_CERTIFICATE_BUNDLE`），无需手动嵌入 CA。
> - 使用私有服务器时，建议将服务器根证书 PEM 字符串内嵌并传入。

### 3) 版本与心跳

- 在属性上报中增加 `firmware_version`，来源于 `esp_ota_get_app_description()->version`，便于云端判断是否需要升级。

```c
const esp_app_desc_t *app = esp_ota_get_app_description();
cJSON_AddStringToObject(root, "firmware_version", app->version);
```

### 4) 首次启动后的有效性标记

- 开机后延迟一段时间（例如 30–60s），设备运行稳定再调用 `esp_ota_mark_app_valid_cancel_rollback()`，否则 Bootloader 会在异常时自动回滚到上一版本。

## MQTT 命令与状态示例

### 1) 触发 OTA

```json
{
  "method": "ota",
  "url": "https://download.example.com/firmware/under_silicon_QTZ.bin",
  "version": "1.2.3",
  "sha256": "bdc3e1...", 
  "force": 0,
  "msg_id": 3001
}
```

### 2) 进度/结果上报（设备 -> `/dpub/{mac}`）

```json
{ "method": "ota_status", "phase": "start", "percent": 0 }
{ "method": "ota_status", "phase": "downloading", "percent": 5 }
...
{ "method": "ota_status", "phase": "finished", "percent": 100, "message": "reboot" }
{ "method": "ota_status", "phase": "failed", "percent": 40, "message": "perform/recv failed" }
```

## CI 与发布建议

- 保持现有矩阵构建（各设备类型），但在 Release 附件中增加 `build/under_silicon.bin`（应用镜像）。
- 建议命名：`under_silicon_<DEVICE>.bin`，并在元数据中写入 `PROJECT_VER`（取自 tag）。
- 以 GitHub Releases 为 OTA 服务器时，URL 示例：
  `https://github.com/<owner>/<repo>/releases/download/<tag>/under_silicon_QTZ.bin`

## 安全与可靠性建议

- 使用证书包或根证书固定（Pinning），避免中间人攻击。
- 可选镜像 `sha256` 校验：下载完成后对镜像分区计算 SHA256 与云端提供值比对。
- 断点续传：`esp_https_ota` 不支持断点续传，建议保证稳定网络或在网关侧提供较近的下载源。
- 低电量保护：根据 `battery_property` 在低电量情况下拒绝 OTA（例如 < 25%）。

## 测试流程

1. 在 `menuconfig` 启用双 OTA 与回滚，编译并烧录 `factory`。
2. 通过 BLUFI 配网，设备联上 Wi‑Fi 并启动 MQTT。
3. 发布一个旧版本（例如 v1.2.2），设备心跳上报 `firmware_version`。
4. 发布新版本 v1.2.3（上传 `under_silicon.bin` 到服务器），向 `/drecv/{mac}` 下发 OTA 命令。
5. 观察 `/dpub/{mac}` 的 `ota_status` 流程，设备自动重启。
6. 新版本启动后运行 30–60s，调用 `ota_mark_app_valid()`，心跳中看到 `firmware_version=1.2.3`。

## 变更清单（代码改动概览）

- 新增：`components/ota_update/ota_update.c`（HTTPS OTA 与进度上报）。
- 修改：`components/base_device/base_device.c` 的 `mqtt_msg_process()`，增加 `method=ota` 分支。
- 修改：属性心跳，增加 `firmware_version` 字段。
- 修改：在设备启动流程（如 `device_first_ready()`）中延迟调用 `ota_mark_app_valid()`。
- CI：Release 附件增加 `build/under_silicon.bin`（应用镜像）。

---

如需我直接为项目加入上述代码骨架与 CI 改动，可告知优先设备类型与目标托管方式（GitHub Release 或自建 HTTP 服务器）。