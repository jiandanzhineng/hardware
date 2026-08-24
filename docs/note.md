# 开发笔记

- ESP-IDF：`C:\Users\46907\esp\v5.2.3\esp-idf\export.ps1`
- RT01 单独编译（不影响当前 sdkconfig）：
  `idf.py -B build_rt01 -D SDKCONFIG=sdkconfig.rt01 build`
  设备类型在 menuconfig → Device Selection → RT01
