## 现状检查
- 已包含 `base_device.h`：`components/base_device/qtz.c`, `DZC01.c`, `base_device.c`, `dianji.c`, `pj01.c`, 以及 `components/device_ble_service/device_ble_service.c`、`components/smqtt/include/smqtt.h`
- 未包含：`components/base_device/TD01.c`(1–8 行已有其它 include)、`components/base_device/qiya.c`(1–9 行已有其它 include)、`components/base_device/zidongsuo.c`(2–12 行已有其它 include)
- 关键接口存在于 `components/base_device/include/base_device.h:85` → `void mqtt_publish(cJSON *root);`

## 修改方案
- 在以下文件顶部加入一行：`#include "base_device.h"`
  - `components/base_device/TD01.c` 放在现有 `#include "device_common.h"` 之后
  - `components/base_device/qiya.c` 放在现有 `#include "device_common.h"` 之后，并删除多余的 `extern void get_property(...)`、`extern void mqtt_publish(...)` 声明以统一由头文件提供
  - `components/base_device/zidongsuo.c` 放在现有 `#include "device_common.h"` 之后
- 不改动业务逻辑，确保设备宏与 `menuconfig` 的 `CONFIG_DEVICE_*` 保持一致；只做最小必要的引用统一

## 编译与验证
- 重新编译组件：执行 `idf.py build`，确保新增引用可解析（`CMakeLists.txt` 已设置 `INCLUDE_DIRS "include"`，见 `components/base_device/CMakeLists.txt:32-35`）
- 运行设备固件，验证：
  - 设备初始化与属性上报正常
  - 调用 `mqtt_publish` 正常（如 `TD01` 的按键事件可改用该封装后仍能发布）

## 风险与说明
- `base_device.h` 要求已选择设备类型，否则触发编译错误（`#error`，见 `components/base_device/include/base_device.h:72-74`）；当前通过 `CONFIG_DEVICE_*` 已选择并按源文件集成，风险可控
- 与文件内自定义宏（如 `LED_PIN`）不冲突；若后续需要统一改用 `CONNECTED_LED` 再做独立重构