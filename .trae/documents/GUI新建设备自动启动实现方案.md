## 目标
- 在 GUI 中点击“创建××设备”后，该设备自动启动，无需再手动点“启动”。

## 现状
- 按钮触发 `create_device(device_type)`（e:\develop\smart\hard\project_td\pytest\gui_controller\device_gui_controller.py:73-82）。
- 设备的启动逻辑为 `start_device(device_id)`（e:\develop\smart\hard\project_td\pytest\gui_controller\device_gui_controller.py:316-336）。
- 初始化阶段的自动创建使用 `auto_create_devices()` 并在 1000ms 后启动（e:\develop\smart\hard\project_td\pytest\gui_controller\device_gui_controller.py:152-172）。

## 代码改动
1. 修改 `create_device` 增加参数并默认自动启动（保持 UI 简洁）：
   - 签名改为 `def create_device(self, device_type: str, auto_start: bool = True)`（e:\develop\smart\hard\project_td\pytest\gui_controller\device_gui_controller.py:174）。
   - 在创建设备并完成 UI 布局后（约 e:\develop\smart\hard\project_td\pytest\gui_controller\device_gui_controller.py:199-201），加：
     - `if auto_start: self.root.after(300, lambda did=device_id: self.start_device(did))`
     - 采用 `after` 轻微延迟，确保控件创建完毕且不阻塞主线程。

2. 保持顶部“创建设备”按钮不变（默认 `auto_start=True`），即可达到“新建设备自动启动”。

3. 调整初始化自动创建，避免重复启动：
   - 在 `auto_create_devices()` 调用 `self.create_device(device_type, auto_start=False)`（e:\develop\smart\hard\project_td\pytest\gui_controller\device_gui_controller.py:159）。
   - 保留现有 1000ms 延迟启动逻辑（e:\develop\smart\hard\project_td\pytest\gui_controller\device_gui_controller.py:166-168）。

## 验证
- 启动 GUI，点击任一“创建××设备”按钮：
  - 设备卡片出现；状态先变为“启动中...”，随后为“运行中”。
- 重启应用观察初始化自动创建：不出现重复启动日志，设备正常进入运行。
- 日志面板出现启动记录，符合预期。