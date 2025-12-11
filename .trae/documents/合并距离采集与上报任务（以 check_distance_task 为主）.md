## 目标

* 将 `report_distance_task` 与 `check_distance_task` 合并为单一循环任务。

* 每个周期：先采集距离 → 更新属性并发送消息 → 进行“分片延时”，保证整体周期为 `report_delay_ms_property`，延时包含采集和发送所耗时间。

## 变更点

* 保留并改造 `static void check_distance_task(void)`，内联 `report_distance_task` 的上报逻辑。

* 删除 `static void report_distance_task(void)` 及其在 `on_device_first_ready()` 中的 `xTaskCreate` 启动。

## 实现步骤

1. 修改 `check_distance_task` 主循环：

   * `TickType_t start = xTaskGetTickCount();`

   * 采集：`uint16_t distance = vl6180x_read_range_single_millimeters();`

   * 若未超时：

     * `device_update_property_float("distance", (float)distance);`

     * `average_length_mm = distance_property.value.float_value;`

     * `update_in_state();`

   * 发送属性消息：`get_property("distance", 0);`

   * 计算剩余延时：

     * `TickType_t period = pdMS_TO_TICKS(report_delay_ms_property.value.int_value);`

     * `TickType_t elapsed = xTaskGetTickCount() - start;`

     * `TickType_t remain = (elapsed < period) ? (period - elapsed) : 0;`

   * 分片延时：

     * `const TickType_t slice = pdMS_TO_TICKS(100);`

     * `while (remain > 0) { TickType_t step = (remain > slice) ? slice : remain; vTaskDelay(step); remain -= step; }`
2. 移除 `report_distance_task` 的定义与声明。
3. 在 `on_device_first_ready()` 中删除 `xTaskCreate(report_distance_task, ...)`，仅保留 `xTaskCreate(check_distance_task, ...)`。

## 注意

* 保持代码简洁，无额外冗长错误处理与注释。

* 现有签名为 `static void fn(void)` 与 `xTaskCreate` 的函数原型略有差异，若当前工程已稳定编译，可暂不改动；如需规范可后续统一改为 `void (*)(void *)`。

## 验证

* 编译运行后，通过设置 `report_delay_ms`（MQTT/属性更新）为如 `1000ms`，观察日志/消息：每个周期包含一次采集与一次 `get_property("distance")` 上报，周期总间隔约 1000ms（包含采集/发送用时）。

