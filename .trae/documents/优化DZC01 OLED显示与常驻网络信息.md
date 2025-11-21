## 目标

* 放大并居中重量显示，去掉左上角小字的视觉问题

* 底部常驻显示网络状态（WIFI与设备就绪），两种显示模式均适用

* 仅修改 `components/base_device/DZC01.c`

## 技术实现

### 字体放大与居中

* 在现有 8x8 字库基础上新增 2x 像素缩放渲染：`oled_draw_char_x2` 与 `oled_draw_text_center_x2`

* 每个 8x8 字符通过水平、垂直各放大 2 倍生成 16x16 显示，写入连续两页（page）

* 新增居中绘制：根据字符串长度计算起始列 `(128 - len * 16) / 2`，避免左上角拥挤

### 显示布局调整

* 模式1（`display_mode==1`）：

  * 清屏后在中间区域（page 1\~2）用 16px 大字居中显示重量，如 `"123 g"`

  * 底部 page 3 用 8px 小字显示网络状态条

* 模式2（`display_mode==2`）：

  * 保持两行自定义小字（page 0、1）

  * 底部 page 3 常驻网络状态条和当前重量

### 网络状态获取

* 在本文件新增 `static bool net_ready` 标记，在 `on_device_first_ready` 置位（`components/base_device/DZC01.c:542`）

* Wi‑Fi连接检测：在本文件 `#include "esp_wifi.h"`，调用 `esp_wifi_sta_get_ap_info`，按返回值判断是否连接AP

* 文案逻辑：

  * `NET OK`：Wi‑Fi已连接且 `net_ready==true`

  * `WIFI OK`：仅Wi‑Fi已连接

  * `WIFI ?`：未连接

* 在 `display_task`（`components/base_device/DZC01.c:365`）每 200ms 刷新底部状态

## 具体改动点

* 添加 `net_ready` 及 Wi‑Fi检测函数到 `DZC01.c`

* 添加 `oled_draw_char_x2`、`oled_draw_text_center_x2` 用于 16x16 大字渲染

* 改写 `display_task` 的绘制逻辑，实现新布局与常驻网络信息

* 将 `on_device_first_ready` 改为仅置位 `net_ready` 并保留/简化提示（`components/base_device/DZC01.c:542`）

## 验证

* 上电后：重量以大号居中显示，底部为 `WIFI ?` 或 `WIFI OK`

* MQTT连接并触发 `on_device_first_ready` 后：底部变为 `NET OK`

* 断开/重连Wi‑Fi：底部状态随之变更

## 说明

* 不新增其他文件，不更改仓库内任何除 `DZC01.c` 外的代码

* 保持代码简洁、少注释，遵循现有风格

