# IoT虚拟设备GUI控制器

这个文件夹包含了用于控制和监控IoT虚拟设备的图形用户界面。

## 文件结构

```
gui_controller/
├── device_gui_controller.py    # 主GUI控制器
├── run_gui.py                  # 启动脚本
├── 启动GUI控制器.bat           # Windows批处理启动文件
├── GUI_使用说明.md             # 详细使用说明
└── README.md                   # 本文件
```

## 快速开始

### 方法1: 使用批处理文件（推荐Windows用户）
双击 `启动GUI控制器.bat` 文件

### 方法2: 使用Python脚本
```bash
cd gui_controller
python run_gui.py
```

### 方法3: 直接运行GUI控制器
```bash
cd gui_controller
python device_gui_controller.py
```

## 主要功能

- **设备管理**: 创建、启动、停止、删除虚拟设备
- **属性控制**: 实时查看和修改设备属性
- **动作模拟**: 模拟设备按键、开关等操作
- **日志监控**: 实时查看设备运行日志
- **MQTT通信**: 通过MQTT协议与设备通信

## 支持的设备类型

1. **TD01** - 单路调光插座
2. **DIANJI** - 电击设备
3. **ZIDONGSUO** - 自动锁设备
4. **QTZ** - 激光距离传感器+双按钮

## 依赖要求

- Python 3.7+
- tkinter（通常包含在Python标准库中）
- paho-mqtt>=1.6.0

## 故障排除

### 设备启动卡死问题
已修复：设备启动现在在后台线程中进行，不会阻塞GUI界面。

### MQTT连接问题
确保MQTT代理（如mosquitto）正在运行：
```bash
# 安装mosquitto
sudo apt-get install mosquitto mosquitto-clients  # Linux
brew install mosquitto                            # macOS

# 启动mosquitto
mosquitto -v
```

### 导入错误
确保virtual_devices.py文件在上级目录中，GUI控制器会自动添加路径。

## 更多信息

详细的使用说明请参考 [GUI_使用说明.md](GUI_使用说明.md)