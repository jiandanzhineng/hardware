## 修改点概览
- 将 `pj01.c` 的强度控制从 `pwm_duty` 改为 `power`（0–255）。
- 保持当前“反向”占空比映射：`power` 越大，占空比越小。
- 不改动任何 GPIO/LEDC 驱动代码，仅改 property 与映射。

## 属性与数组
- 使用 `power_property` 替代 `pwm_duty_property`。
- `init_property()`：`name="power"`，`value_type=PROPERTY_TYPE_INT`，`readable=true`，`writeable=true`，`min=0`，`max=255`，`value.int_value=0`。
- `device_properties[]` 改为包含 `&power_property`（PJ01 无电池属性）。

## 映射规则（10-bit 反向映射）
- 输入范围：`power ∈ [0,255]`。
- 阈值：`power<2 → duty=1023`（最亮）；`power>254 → duty=0`（最暗/完全关闭）。
- 线性反向：其他情况 `duty = 1023 - power * 4`（`1023/255≈4`）。
- 调用保持：`update_pwm_duty(duty)`。

## 代码位点
- `e:\develop\smart\hard\project_td\components\base_device\pj01.c:44` 将 `pwm_duty_property` 改为 `power_property`。
- `e:\develop\smart\hard\project_td\components\base_device\pj01.c:49-55` 数组改为包含 `&power_property`。
- `e:\develop\smart\hard\project_td\components\base_device\pj01.c:80-96` `on_set_property` 改为处理 `"power"`，按上述反向映射计算 `actual_duty` 并调用 `update_pwm_duty(actual_duty)`；日志同步更新。
- `e:\develop\smart\hard\project_td\components\base_device\pj01.c:171-183` `init_property()` 初始化 `power_property`（替代 `pwm_duty_property`），`max=255`、`min=0`。
- 其余驱动配置（LEDC/GPIO）不变。

## 验证建议
- 下发 `power=0,1,2,254,255`，确认 0/1/2 → 亮度最大（占空比接近 1023），254/255 → 最暗（占空比接近 0）。
- 观察日志与实际亮度，确保与当前反向逻辑一致。