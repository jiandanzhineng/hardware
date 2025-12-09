## 目标
- 将 KEY2 的上报统一为“属性更新”，按下=1、释放=0；同时应用于 BLE 与 Wi‑Fi 模式。
- 取消 KEY2 的 `key_clicked` 事件上报，改为与 `qtz.c` 相同的按钮属性模型。

## 属性定义
- 新增 `device_property_t button2_property`：
  - 名称：`button2`
  - 类型：int（0=释放，1=按下）
  - 读写：只读（readable=true, writeable=false）
  - 初始值：0
- 将 `button2_property` 添加到 `device_properties[]`（`components/base_device/DZC01.c:46`），保持现有属性顺序，仅在末尾补充。

## 初始化与注册
- 在 `on_device_init` 初始化 `button2_property`（`components/base_device/DZC01.c:562` 附近）。
- 在 `init_keys()` 为 KEY2 注册：
  - `BUTTON_PRESS_DOWN` → `device_update_property_int("button2", 1); get_property("button2", 0);`
  - `BUTTON_PRESS_UP`   → `device_update_property_int("button2", 0); get_property("button2", 0);`
- 取消 KEY2 的 `BUTTON_SINGLE_CLICK` 注册与 `key2_click_cb` 行为（`components/base_device/DZC01.c:491–497`）。
- 参考实现：`components/base_device/qtz.c:739–769`。

## 上报行为
- BLE：`device_update_property_int()` 将触发 `device_ble_update_property()`（`components/base_device/base_device.c:401–415`）。
- Wi‑Fi：`get_property()` 立即发布 `method=update` 的单属性消息（`components/base_device/base_device.c:343–381`）。
- 全量上报逻辑保持不变（`components/base_device/base_device.c:162–194`）。

## 兼容性
- 不影响 KEY1（休眠）、KEY3（去皮）、KEY4（校准）现有逻辑。
- 去除 KEY2 事件后，云端需从事件订阅迁移为属性订阅（若有）。

## 验证
- BLE：按/放 KEY2，观察 `button2` 属性在客户端 0↔1 变化。
- Wi‑Fi：收到 `method=update` 的单键属性更新；不再出现 `action:key_clicked`。
- 设备其他功能（称重显示、周期上报、休眠）保持正常。

## 需要确认
- 属性名使用 `button2` 是否满足你的规范？如需改为 `key2` 或同时支持多键（`button1/2/3/4`），我可按你的偏好调整。