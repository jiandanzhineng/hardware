## 目标
- 在不引入头文件依赖的前提下，让 `base_device` 调用 BLE 的 `device_ble_update_property(int)`，避免循环引用。

## 改动点（仅 base_device）
1) 在 `components/base_device/base_device.c` 顶部加入默认空实现（弱符号）：
```
__attribute__((weak)) void device_ble_update_property(int index) {}
```
2) 取消注释并调用（保持原有模式判断）：
- `components/base_device/base_device.c:398-399`
- `components/base_device/base_device.c:411-412`
- `components/base_device/base_device.c:422-423`
示例：
```
if(g_device_mode == MODE_BLE){
    device_ble_update_property(i);
}
```
3) 保持 `// #include "device_ble_service.h"` 为注释状态，不引入头文件。

## 依赖关系
- `device_ble_service` 组件已实现强符号 `device_ble_update_property(int)`；链接时强符号覆盖弱符号，无需任何额外依赖配置。

## 验证
- 构建：`idf.py fullclean build`
- 运行：`idf.py flash monitor`
- 在 BLE 模式下更新属性，观察 GATT 通知发送日志与属性值同步。

## 说明
- 若未集成 BLE 组件，弱符号空实现生效，不影响 `base_device` 正常工作；集成 BLE 时自动被覆盖，调用生效。