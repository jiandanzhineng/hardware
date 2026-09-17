# 开发笔记

- ESP-IDF：`C:\Users\46907\esp\v5.2.3\esp-idf\export.ps1`
- RT01 单独编译（不影响当前 sdkconfig）：
  `idf.py -B build_rt01 -D SDKCONFIG=sdkconfig.rt01 build`
  设备类型在 menuconfig → Device Selection → RT01
- 板测依赖：IDF Python 已装 `bleak`；RT01 裸板脚本 `docs/temp/rt01_board_test.py`，默认 COM17
- DAN01 独立编译：`idf.py -B build_dan01 -D SDKCONFIG=sdkconfig.dan01 -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32c3;sdkconfig.defaults.dan01" build`
- 2026-09-05 DAN01 开发板识别为 `USB-SERIAL CH340K (COM16)`。
- DAN01 串口板测：`python docs/temp/dan01_board_test.py --port COM16`。
- 2026-09-17 按 SCH V1.2 改引脚后，COM16 板测通过：身份 `f85b1b4d9564`，AS201 有数据，电池约 94%。
- DAN01 V1.2：AS201 TX=GPIO18 RX=GPIO19 RST=GPIO4；KEY=GPIO5；BAT_ADC=GPIO0 BAT_EN=GPIO1；LED=GPIO10。须 `CONFIG_ESP_CONSOLE_SECONDARY_NONE`。

## 2026-09-05 DAN01 电池复查

- 只读串口查询使用 `C:\Users\46907\.espressif\python_env\idf5.2_py3.11_env\Scripts\python.exe`，已装 pyserial 3.5；115200 baud，打开前置 DTR/RTS 为 false。
- 本次 COM16 身份为 `f85b1b4f50cc`，固件 `v1.1.40`，启动日志 ELF SHA256 前缀 `62cb606b9`。用户提供的旧测量截图设备为 `f85b1b4eca20`，不能混用两块板的万用表数据。
- 连续观察 37.5 秒并重启复查 29.6 秒，共捕获 7 次电池采样；ADC1 Channel 2 主采样原始值 3529~3544，日志 ADC 电压 2487~2497 mV，按约 3.69 倍换算为 9177~9213 mV，电量均为 100%。该百分比不能证明电池电压正常；本次没有测得电池端实际电压。
- 重启后及后续采样均见 `GPIO[2]| InputEn: 0| OutputEn: 0| OpenDrain: 0| Pullup: 0| Pulldown: 0`。这是配置时的驱动日志，并非对运行期间寄存器的连续监测。
- ESP-IDF 5.2.3 的 `adc_oneshot_config_channel()` 经 `s_adc_io_init()` 将采样 GPIO 设为 `GPIO_MODE_DISABLE`，关闭上下拉；ESP32-C3 的 ADC1 Channel 2 对应 GPIO2。
- 主工作区 `72d2587` 未配置 DAN01 电池引脚，公共检测被 `BATTERY_CLOSE_EN` 禁用；本地 `build_dan01/under_silicon.elf` SHA256 前缀 `cda84b218`，也不匹配板上诊断固件。本次没有修改或烧录固件。
- 当前公共电池代码的审查发现：`device_init()` 创建采样任务早于 `device_first_ready()` 配置 BAT_EN 输出；ADC 校准失败时 `battery_adc_get_value()` 不写输出仍返回成功，调用方会读取未初始化的电量变量。两项均不能直接解释截图中的 GPIO2 与分压节点电压差，也不能据此认定板上诊断固件包含相同问题。
- ZIDONGSUO 使用独立实现：GPIO1 使能、ADC1 Channel 0、12-bit/11 dB、使能后等待 10 ms、27k/10k 对应 3.7 倍分压、约每 30 秒采样；连续 3 次电量低于 20% 触发应急开锁。它与公共 Battery 实现不是同一条采样路径。

## 2026-09-06 ZIDONGSUO 电池实测

- COM16 (`USB-SERIAL CH340K`)，串口身份 `8856a6fca7a4`，型号 `ZIDONGSUO`，固件 `v1.1.40`。
- 115200 baud，DTR/RTS 预置 false；发送 `@DEBUG IDENTIFY`、`@DEBUG START` 和两次只读 `get battery`，观察 69.14 秒，没有复位、烧录或发送开关锁命令。
- 设备日志时间 150653 ms / 180663 ms 分别输出 `Battery Voltage: 4.21 V` / `4.22 V`，均为 `Battery Percentage: 100%`；两次属性查询也均返回 100。采样间隔 30.01 秒，期间未捕获崩溃或断言失败日志。
- 读数稳定且接近单节锂电满电电压，没有 DAN01 约 9.2 V 的异常。本次没有这块板电池两端的万用表对照值，尚不能确认绝对测量误差；现有固件日志不输出 ADC 原始值或分压点校准电压。
- 当前源码中自动锁先完成 BAT_EN/ADC 初始化再创建采样任务；电压按 27k/10k 分压乘以 3.7 还原，4.2 V 及以上电量映射为 100%。本次未将板上完整固件哈希与源码构建结果比对。

## 2026-09-06 电池实现对比

- 用户确认两型号电池硬件一致，记录于 `docs/draft/ADR-DAN01与ZIDONGSUO电池硬件一致.md`。
- 以下比较自动锁独立实现与当前公共 Battery 实现，不能视为 DAN01 板上诊断固件的完整差异；当前 DAN01 源码仍未配置电池引脚，公共检测未启用。
- 自动锁：ADC 12-bit/11 dB、ADC 与校准句柄初始化一次并复用、先初始化 BAT_EN 输出再启动采样任务；每次拉高 BAT_EN 后等待 10 ms，单次读取后拉低；30 秒周期，校准电压乘 3.7。
- 公共 Battery：ADC 12-bit/6 dB，每次采样创建并销毁 ADC/校准句柄；拉高 BAT_EN 后执行初始化，无显式稳定等待；调用任务每 10 秒采样，先减 12 mV 再乘 3.69。使能脚经 `gpio_reset_pin()` 后设为输出，没有像自动锁那样显式关闭上下拉；这与 ADC 采样脚是不同引脚。
- 在相同校准 ADC 输入 1.14 V 时，自动锁公式为 4.218 V，公共公式为 4.16232 V，相差 55.68 mV，不能解释约 9.2 V。自动锁实测 4.21~4.22 V 按源码公式反推 ADC 电压约 1.138~1.141 V（非直接实测），DAN01 诊断日志的 ADC 电压为约 2.49 V，主要异常出现在分压倍乘之前。
- 两者都使用 Curve Fitting 校准，但失败处理不同：自动锁初始化校准失败会触发 `ESP_ERROR_CHECK`，并非自动进入近似换算；公共实现校准失败仍返回成功却未填写电量。电量曲线也不同，例如 3.70 V 时自动锁为 20%，公共曲线为 50%；这不影响电压换算值。
