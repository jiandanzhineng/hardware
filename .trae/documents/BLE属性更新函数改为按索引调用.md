## 目标
- 将 `device_ble_update_property` 的输入由 `device_property_t *` 改为属性索引 `int index`
- 保持现有 BLE 属性缓冲与通知逻辑不变，去掉不必要的指针比较循环

## 修改点
- 文件 `main/device_ble_service.h`
  - 将声明改为：`void device_ble_update_property(int index);`
- 文件 `main/device_ble_service.c`
  - 将实现改为按索引直接更新：
```c
void device_ble_update_property(int i){
    if (i < 0 || i >= device_properties_num) return;
    device_property_t *p = device_properties[i];
    if (p->value_type == PROPERTY_TYPE_INT){
        int v = p->value.int_value;
        prop_value_buf[i][0] = (uint8_t)(v & 0xFF);
        prop_value_buf[i][1] = (uint8_t)((v >> 8) & 0xFF);
        prop_value_buf[i][2] = (uint8_t)((v >> 16) & 0xFF);
        prop_value_buf[i][3] = (uint8_t)((v >> 24) & 0xFF);
        prop_value_len[i] = 4;
    } else if (p->value_type == PROPERTY_TYPE_FLOAT){
        float f = p->value.float_value;
        memcpy(prop_value_buf[i], &f, 4);
        prop_value_len[i] = 4;
    } else {
        uint16_t l = strnlen(p->value.string_value, PROPERTY_VALUE_MAX);
        memcpy(prop_value_buf[i], p->value.string_value, l);
        prop_value_len[i] = l;
    }
    if (prop_value_index[i] >= 0){
        esp_ble_gatts_set_attr_value(handle_table[prop_value_index[i]], prop_value_len[i], prop_value_buf[i]);
        if (prop_notify_enabled[i] && conn_id_last >= 0){
            esp_ble_gatts_send_indicate(s_gatts_if, conn_id_last, handle_table[prop_value_index[i]], prop_value_len[i], prop_value_buf[i], false);
        }
    }
}
```
- 文件 `components/base_device/base_device.c`
  - 更新三处调用（行约 392、399、407）：在 `MODE_BLE` 下查找索引后调用新接口
```c
static int device_property_index(device_property_t *p){
    for (int i = 0; i < device_properties_num; i++) if (device_properties[i] == p) return i;
    return -1;
}

void device_update_property_int(device_property_t *p, int v){
    p->value.int_value = v;
    if(g_device_mode == MODE_BLE){
        int i = device_property_index(p);
        if (i >= 0) device_ble_update_property(i);
    }
}

void device_update_property_float(device_property_t *p, float v){
    p->value.float_value = v;
    if(g_device_mode == MODE_BLE){
        int i = device_property_index(p);
        if (i >= 0) device_ble_update_property(i);
    }
}

void device_update_property_string(device_property_t *p, const char *v){
    strncpy(p->value.string_value, v, PROPERTY_VALUE_MAX - 1);
    p->value.string_value[PROPERTY_VALUE_MAX - 1] = 0;
    if(g_device_mode == MODE_BLE){
        int i = device_property_index(p);
        if (i >= 0) device_ble_update_property(i);
    }
}
```

## 验证
- 编译项目确保无报错（头文件声明与实现一致、调用方已更新）
- 连接 BLE，写入属性后观察通知是否正常，检查整数/浮点/字符串三类属性的更新与上报

## 影响范围
- 仅 `device_ble_service.h/.c` 与 `base_device.c` 三处调用，其他代码无需改动

## 交付
- 精简改动、无额外注释或复杂错误处理，符合当前代码风格