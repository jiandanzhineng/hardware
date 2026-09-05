# 开发笔记

- ESP-IDF：`C:\Users\46907\esp\v5.2.3\esp-idf\export.ps1`
- RT01 单独编译（不影响当前 sdkconfig）：
  `idf.py -B build_rt01 -D SDKCONFIG=sdkconfig.rt01 build`
  设备类型在 menuconfig → Device Selection → RT01
- 板测依赖：IDF Python 已装 `bleak`；RT01 裸板脚本 `docs/temp/rt01_board_test.py`，默认 COM17
- DAN01 独立编译：`idf.py -B build_dan01 -D SDKCONFIG=sdkconfig.dan01 -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32c3;sdkconfig.defaults.dan01" build`
- 2026-09-05 DAN01 开发板识别为 `USB-SERIAL CH340K (COM16)`。
- DAN01 串口板测：`python docs/temp/dan01_board_test.py --port COM16`。
