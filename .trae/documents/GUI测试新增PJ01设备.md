## 目标
- 在GUI测试中新增“PJ01 PWM电机控制器”设备，行为与文档一致。
- GUI可创建、启动、停止PJ01设备，并可设置`power(0-255)`，即时反映到PWM占空比。

## 需要修改的文件
- `pytest/gui_controller/device_gui_controller.py`
- `pytest/virtual_devices.py`

## 具体变更
### 1) GUI支持PJ01设备
- 导入`PJ01Device`：在`pytest/gui_controller/device_gui_controller.py`顶部导入列表中加入`PJ01Device`（参见当前导入行：`pytest/gui_controller/device_gui_controller.py:23`）。
- 创建按钮：在设备控制区加入`“创建PJ01设备”`按钮，调用`self.create_device("PJ01")`（参考按钮区域：`pytest/gui_controller/device_gui_controller.py:72-83`）。
- 自动创建列表：在`auto_create_devices()`的`device_types`中加入`"PJ01"`（`pytest/gui_controller/device_gui_controller.py:152-155`）。
- 创建设备分支：在`create_device()`中新增`elif device_type == "PJ01": device = PJ01Device(device_id)`（参考类型分支：`pytest/gui_controller/device_gui_controller.py:182-193`）。
- 设备动作：PJ01暂无特定动作，无需在`create_device_frame()`中新增。

### 2) PJ01虚拟设备属性与行为对齐文档
- 依据文档“PJ01 PWM电机控制器”，属性为`power(0-255)`，占空比映射：`duty = 1023 - power * 4`（`docs/device_api_reference.md:318-336`）。
- 在`pytest/virtual_devices.py`的`PJ01Device`中：
  - 将可写属性调整为`power`（当前为`pwm_duty`，参考类定义：`pytest/virtual_devices.py:341-356`）。
  - 在`_on_property_changed()`中处理`power`：计算占空比`duty = clamp(1023 - int(power)*4, 0, 1023)`并更新`current_pwm_duty`（参考方法位置：`pytest/virtual_devices.py:368-376`）。
  - 保持无电池：继续删除`battery`属性（`pytest/virtual_devices.py:352-355`）。
  - 保留PWM线程逻辑不变（`pytest/virtual_devices.py:389-414`）。
- 说明：GUI的数值输入白名单已包含`power`（`pytest/gui_controller/device_gui_controller.py:274-289`），无需改动即可出现输入与“设置”按钮。

## 验证步骤
- 启动GUI：运行`python pytest/gui_controller/run_gui.py`。
- 在顶部点击`创建PJ01设备`，界面出现`PJ01 - pj01XXXaabbcc`卡片，状态“已停止”。
- 点击`启动`后，查看日志：应出现`PJ01设备启动`与心跳上报。
- 在属性区给`power`输入`128`点击`设置`：日志应打印`PWM duty set to 1023-128*4=511`（约50%）；属性显示更新。
- 设置`power`为`255/0`验证占空比为`0/1023`。
- `停止/删除`按钮可正常停止或移除设备。

## 兼容性与注意事项
- 仅对GUI和虚拟设备层变更，不影响固件；固件已按`power`实现（`components/base_device/pj01.c`，搜索结果已确认）。
- 若后续需要在MQTT联调中展示占空比，可考虑将`pwm_duty`作为只读辅助属性上报，但本次按文档保持简洁。

## 交付结果
- 新增PJ01设备的GUI创建入口与自动创建。
- PJ01虚拟设备属性切换为`power(0-255)`并映射到PWM占空比，与文档一致。