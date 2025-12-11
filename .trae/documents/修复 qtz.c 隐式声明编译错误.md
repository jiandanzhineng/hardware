## 原因
- `qtz.c` 未包含声明 `device_update_property_int/float` 的头文件，导致隐式函数声明被视为错误（`-Werror=implicit-function-declaration`）。
- 证据：
  - 调用位置：`components/base_device/qtz.c:538` 与 `components/base_device/qtz.c:663`。
  - 正确原型所在：`components/base_device/include/base_device.h:90-92`。
  - 当前包含列表缺少 `base_device.h`：`components/base_device/qtz.c:1-11`。

## 修复方案
- 在 `qtz.c` 顶部添加 `#include "base_device.h"`，让编译器看到函数原型。
- 不做其他改动；保持现有调用不变。

## 具体改动
- 文件：`components/base_device/qtz.c`
- 在现有 `#include "device_common.h"` 之后，插入：
  - `#include "base_device.h"`

## 验证
- 重新构建当前目标：确保 `qtz.c` 无隐式声明错误；链接阶段应通过（实现位于 `components/base_device/base_device.c`）。
- 快速自检点：
  - 调用点仍然有效：`qtz.c:538`、`qtz.c:663`。
  - 原型来源：`base_device.h:90-92`。

## 备注
- 若其他设备模块也直接调用 `device_update_property_*`，建议同样包含 `base_device.h`，以避免在不同编译单元/配置下出现相同告警/错误。