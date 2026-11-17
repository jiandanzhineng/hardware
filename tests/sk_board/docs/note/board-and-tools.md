# SK V1.0 原理图与工具

- 来源：用户提供的单页《SK控制板.pdf》，图号 SK控制板_V1.0.SchDoc，日期 2026/11/17，ESP32-C3FH4。
- 源文件 SHA256：`def46c6b19ff32ada861231f54d16257c9418ff8831acde9ff8cacfc7edfa760`。
- GPIO0=ADC_BAT（ADC1_CH0，R4/R7 各 10k，乘 2）；GPIO1=PWR_HOLD；GPIO2=KEY_DAT（ADC1_CH2，R15/R16 各 10k）；GPIO3=LOCK；GPIO4=LED_STAT；GPIO8=KEY-1；GPIO9=BOOT；GPIO10=KEY-2；GPIO18/19=USB D-/D+；UART0 TX/RX=GPIO21/20。
- J2 锁正极来自 M1 切换的 VCC_BAT，没有升压/稳压；单节锂电满电约 4.2V，实际负载电压还取决于电池、线材与 M1 压降。线圈额定电压/电流未提供。
- 图中 M1 栅极经 R2=100k 上拉至电池、直接连接 GPIO3，未见独立续流/钳位器件；GPIO3 不能承受电池高于 3.3V 时的上拉，3.3V 高电平也不能保证 PMOS 可靠关闭。默认禁止锁输出，不能把高阻态当作电气隔离。
- 图中 GPIO1 直接接 S8050 基极，R9 是 K5 支路的电阻，不是 GPIO1 的串联基极电阻。默认开漏释放+内部弱上拉限制基极电流，能否自保持需要实测；修正基极电阻后可配置推挽。
- KEY_DAT 按下约 VBAT/2，不保证达到 GPIO 数字输入 VIH，因此用校准 ADC、400/800mV 回差和 30ms 去抖；未取得校准时不提供电源键自动关机。
- ADC_BAT 的分压一直接电池，MCU 断电时需检查 ADC 引脚回灌及实际关机电流。原理图没有 USB/3V3/锁输出电压 ADC、电流采样、锁位置反馈、线圈温度传感器或 MCU 充电状态输入。

## 工具链

- PowerShell 激活：`C:\Users\46907\esp\v5.2.3\esp-idf\export.ps1`，版本 ESP-IDF v5.2.3；在本项目目录运行 `idf.py build`。
- 激活后使用 IDF 自带 Python；不要把硬件仓库根项目的 sdkconfig 或设备类型配置复制到这里。
- 主机测试：WSL 的 `cc` + Python3，运行 `python3 tests/run_host_tests.py`；对生产锁驱动使用假 GPTimer/GPIO，不能替代示波器验证。
- 正常 USB 串口通过 ESP32-C3 原生 USB Serial/JTAG；另有 UART0 构建方法，见 README。
