# 固件自动构建工作流

这个GitHub Actions工作流会自动为4种不同的设备类型构建固件。

## 支持的设备类型

1. **TD01** - CONFIG_DEVICE_TD01=y
2. **DIANJI** - CONFIG_DEVICE_DIANJI=y  
3. **QTZ** - CONFIG_DEVICE_QTZ=y
4. **ZIDONGSUO** - CONFIG_DEVICE_ZIDONGSUO=y

## 触发条件

工作流会在以下情况下自动运行：
- 推送到 `main` 或 `master` 分支
- 创建针对 `main` 或 `master` 分支的Pull Request
- 手动触发（通过GitHub网页界面）

## 手动触发构建

1. 进入GitHub仓库页面
2. 点击 "Actions" 标签
3. 选择 "Build Firmware for All Device Types" 工作流
4. 点击 "Run workflow" 按钮
5. 选择分支并点击 "Run workflow"

## 构建产物

每次构建会生成以下文件：

### 合并的固件文件（推荐使用）
- `blufi_demo_TD01_merged.bin` - TD01设备完整固件
- `blufi_demo_DIANJI_merged.bin` - DIANJI设备完整固件
- `blufi_demo_QTZ_merged.bin` - QTZ设备完整固件
- `blufi_demo_ZIDONGSUO_merged.bin` - ZIDONGSUO设备完整固件

### 单独的组件文件
- `blufi_demo.bin` - 应用程序
- `bootloader.bin` - 引导程序
- `partition-table.bin` - 分区表

## 烧写说明

### 使用合并的固件文件（推荐）
```bash
esptool.py --chip esp32c3 --port COM_PORT write_flash 0x0 blufi_demo_DEVICE_TYPE_merged.bin
```

### 使用单独的组件文件
```bash
esptool.py --chip esp32c3 --port COM_PORT write_flash \
  0x0 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 blufi_demo.bin
```

## 下载构建产物

1. 进入GitHub仓库的 "Actions" 页面
2. 点击最新的构建任务
3. 在 "Artifacts" 部分下载对应设备类型的固件文件

## 发布版本

当推送带有标签的提交到主分支时，工作流会自动创建GitHub Release并上传所有固件文件。

```bash
git tag v1.0.0
git push origin v1.0.0
```

## 配置说明

工作流使用以下配置：
- **ESP-IDF版本**: v5.1.2
- **目标芯片**: ESP32-C3
- **Flash模式**: DIO
- **Flash频率**: 80MHz
- **Flash大小**: 2MB

## 故障排除

如果构建失败，请检查：
1. 代码是否能在本地正常编译
2. 所有必要的依赖是否已正确配置
3. sdkconfig配置是否正确
4. ESP-IDF版本是否兼容

查看构建日志获取详细的错误信息。