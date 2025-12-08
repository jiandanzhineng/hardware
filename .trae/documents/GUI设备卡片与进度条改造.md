## 目标
- 将设备在GUI中以“卡片”样式展示，状态更直观。
- 为主要数值型属性增加进度条，便于快速观察变化。

## 涉及文件
- `pytest/gui_controller/device_gui_controller.py`

## 设计与实现
### 1) 卡片式设备面板
- 保留现有每设备 `ttk.LabelFrame`，增强为“卡片”：增加头部区（设备名称/ID、状态徽标）、操作区（启动/停止/删除），下方属性区。
- 头部状态徽标：使用 `ttk.Label` 显示“已停止/启动中/运行中”，并用前景色区分（红/橙/绿）。现有状态更新逻辑已具备，复用与整理布局（参考 `device_gui_controller.py:228-249`）。
- 调整布局以增加卡片感：适当增大 `padding`，分区更清晰（状态区、属性区、动作区）。

### 2) 数值属性进度条
- 在属性渲染时，对可读属性且为数值型的字段添加 `ttk.Progressbar`，与数值标签并排显示；可写属性仍保留输入框与“设置”按钮。
- 属性范围配置（简洁静态映射）：
  - `TD01.power`: 0–255
  - `PJ01.power`: 0–255；`pwm_duty`: 0–1023（只读显示）
  - `DIANJI.voltage`: 0–100；`delay`: 20–1000（仅标签）
  - `QTZ.distance`: 10–500；`low_band`: 0–200；`high_band`: 0–200
  - `QIYA.pressure`: 15–35（依据当前模拟范围）；`temperature`: 15–35（只读）
- 在 `create_device_frame(...)` 中：
  - 为每个属性创建 `value_label` 后，若在范围表中则创建 `Progressbar(maximum=range_max)`，初始值用当前属性值（低于0或高于最大值时裁剪）。
  - 存储到 `self.property_widgets[device_id][f"{prop_name}_bar"]`。
- 在 `_update_properties(...)`（`device_gui_controller.py:544-560`）：
  - 同步更新进度条：若存在 `bar` 则设置当前值（做最小裁剪）。
- 在 `set_property(...)` 与 `toggle_property(...)`：
  - 同步更新进度条值（若存在）。

### 3) 简洁与一致性
- 不引入复杂类型判断，使用一个简洁的 `PROPERTY_RANGES` 字典驱动进度条创建与更新。
- 不改动设备模拟逻辑（如心跳与线程），仅在GUI层可视化增强，遵循“简洁、少注释”。

## 修改点概览
- `create_device_frame(...)`：增加头部卡片布局与进度条控件创建。
- `_update_properties(...)`：增加进度条同步逻辑。
- `set_property(...)`、`toggle_property(...)`：写入后更新进度条。
- `PROPERTY_RANGES`：新增在控制器类中或模块级常量。

## 验证
- 启动 `python pytest/gui_controller/run_gui.py`。
- 创建 `PJ01/TD01/DIANJI/QTZ/QIYA`，观察卡片式布局与状态色。
- 设置 `PJ01.power=128`，确认进度条约50%。
- QTZ 距离模拟与报告时进度条连续变化。
- DIANJI 设置 `voltage` 时进度条随值变化。

## 交付
- 更直观的卡片式设备展示。
- 主要数值属性的进度条可视化，与现有更新逻辑无缝协同。