## 目标
- 修复整型与浮点属性写入的数值解析错误。
- 整型按小端二进制解析；浮点仅支持 IEEE‑11073 32 位 FLOAT 格式，不再兼容字符。

## 修改点
- 整型属性（`PROPERTY_TYPE_INT`）：小端 1~4 字节二进制到 `int`。
```c
int v = 0;
for (int b = 0; b < param->write.len && b < 4; b++) {
    v |= ((int)param->write.value[b]) << (8 * b);
}
esp_log_buffer_hex(TAG, param->write.value, param->write.len);
ESP_LOGI(TAG, "Set %s to %d", p->name, v);
```
- 浮点属性（`PROPERTY_TYPE_FLOAT`）：解析 IEEE‑11073 FLOAT32（24 位有符号尾数 + 8 位有符号指数，整体小端）。
```c
int32_t mantissa = (int32_t)(param->write.value[0] | (param->write.value[1] << 8) | (param->write.value[2] << 16));
if (mantissa & 0x800000) mantissa |= ~0xFFFFFF; // 24 位符号扩展
int8_t exponent = (int8_t)param->write.value[3];
float f = (float)mantissa;
while (exponent > 0) { f *= 10.0f; exponent--; }
while (exponent < 0) { f /= 10.0f; exponent++; }
esp_log_buffer_hex(TAG, param->write.value, param->write.len);
ESP_LOGI(TAG, "Set %s to %f", p->name, f);
```
- 去除 `atoi/atof` 路径；整型与浮点均以二进制写入为准，代码简洁。
- 后续回写到 `prop_value_buf[i][0..3]` 保持小端。

## 验证
- `voltage` 写入单字节 `0x01` → `Set voltage to 1`。
- 整数 42：`2A 00 00 00` → `Set voltage to 42`。
- 浮点 12.5（11073）：`7D 00 00 FF`（mantissa=125, exponent=-1）→ `Set <name> to 12.500000`。

## 影响范围
- 仅属性写入解析逻辑，统一与 GATT 的二进制表示；不再支持浮点字符输入。