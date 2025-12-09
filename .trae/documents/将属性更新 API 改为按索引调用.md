## 目标
- 修改 `base_device.h` 中 3 个接口为按属性名字符串入参：
  - `void device_update_property_int(const char *name, int v);`
  - `void device_update_property_float(const char *name, float v);`
  - `void device_update_property_string(const char *name, const char *v);`
- 在实现中按名称查找属性索引，更新值并在 BLE 模式下触发 `device_ble_update_property(index)`。

## 修改点
- 文件 `components/base_device/include/base_device.h`
  - 将第 90–92 行函数声明改为按名称入参。
- 文件 `components/base_device/base_device.c`
  - 重写上述 3 个函数：
    - 遍历 `device_properties`，用 `strcmp(device_properties[i]->name, name)` 找到索引 `i`。
    - 更新对应类型的 `value` 字段。
    - 在 `g_device_mode == MODE_BLE` 时调用 `device_ble_update_property(i)`。
- 文件 `components/base_device/dianji.c`
  - 更新调用：`device_update_property_int("battery", bat_value);`

## 验证
- 全局检索确保无旧签名引用残留。
- 编译通过；运行时写入 `battery` 值可被 BLE 通知更新。

## 影响范围
- 仅头文件声明、实现文件的三处函数和一处调用调整；不影响其它逻辑。

## 交付
- 代码简洁，按现有风格，无多余错误处理与注释。