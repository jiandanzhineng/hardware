# BLE OTA 升级指南

本文档详细说明如何通过蓝牙（BLE）对设备进行 OTA 固件升级。

## 1. 服务与特性定义

OTA 功能作为一个独立的 GATT Primary Service 存在，UUID 定义如下：

- **OTA Service UUID**: `0x8010`

该服务包含两个特性（Characteristics）：

| 特性名称 | UUID | 属性 | 描述 |
| :--- | :--- | :--- | :--- |
| **Command** | `0x8011` | Write, Notify | 用于发送控制命令（开始、结束、中止）并接收状态通知 |
| **Data** | `0x8012` | Write (No Response) | 用于传输固件数据块 |

## 2. 升级流程

整个升级过程分为三个阶段：**开始 (Start)** -> **数据传输 (Data Transfer)** -> **结束 (End)**。

### 2.1 准备工作

1. 连接设备的 BLE。
2. 发现 OTA 服务 (`0x8010`)。
3. 在 **Command 特性 (`0x8011`)** 上启用通知 (Enable Notifications/Indication)，以便接收设备的状态反馈。

### 2.2 开始升级 (Start)

向 **Command 特性 (`0x8011`)** 写入“开始”命令。

**数据格式 (5字节)**：
```
[CMD_START (1 byte)] + [Image Size (4 bytes, Little Endian)]
```
- `CMD_START`: `0x01`
- `Image Size`: 固件文件的总字节数

**示例**：固件大小为 1024 字节 (`0x00000400`)
发送：`01 00 04 00 00`

**设备响应**（通过 Notify）：
- `0x00` (STATUS_ACK): 准备就绪，可以开始发送数据。
- `0x03` (STATUS_FAIL): 启动失败（如无法分配分区）。

### 2.3 数据传输 (Data Transfer)

收到 `STATUS_ACK` 后，将固件文件切分为小块（建议 MTU 大小，如 200-500 字节），依次写入 **Data 特性 (`0x8012`)**。

- 使用 `Write Without Response` 以提高传输速度。
- 设备会自动计算接收到的数据的 CRC32 校验和。
- 传输过程中设备不会发送每包确认，除非发生严重错误。

### 2.4 结束升级 (End)

所有数据发送完毕后，向 **Command 特性 (`0x8011`)** 写入“结束”命令。

**数据格式 (5字节)**：
```
[CMD_END (1 byte)] + [CRC32 (4 bytes, Little Endian)]
```
- `CMD_END`: `0x02`
- `CRC32`: 整个固件文件的 CRC32 校验值（标准 CRC32 算法）

**示例**：CRC32 为 `0xAABBCCDD`
发送：`02 DD CC BB AA`

**设备响应**（通过 Notify）：
- `0x01` (STATUS_SUCCESS): 校验成功，设备将重启并应用新固件。
- `0x02` (STATUS_CRC_ERROR): 校验失败，升级取消。
- `0x03` (STATUS_FAIL): 写入或验证失败。

### 2.5 异常中止 (Abort)

在任何时候，如果需要取消升级，可以向 **Command 特性 (`0x8011`)** 写入“中止”命令。

**数据格式 (1字节)**：
```
[CMD_ABORT (1 byte)]
```
- `CMD_ABORT`: `0x03`

发送：`03`

---

## 3. 状态码汇总

设备通过 Notify 返回的状态码如下：

| 值 | 名称 | 描述 |
| :--- | :--- | :--- |
| `0x00` | `STATUS_ACK` | 命令接收成功/准备就绪 |
| `0x01` | `STATUS_SUCCESS` | 升级成功 |
| `0x02` | `STATUS_CRC_ERROR` | CRC 校验错误 |
| `0x03` | `STATUS_FAIL` | 通用错误（分区错误、写入失败等） |

## 4. Python 脚本示例

可以使用 `bleak` 库编写简单的升级脚本：

```python
import asyncio
from bleak import BleakClient
import struct
import zlib

OTA_SVC_UUID = "00008010-0000-1000-8000-00805f9b34fb"
OTA_CMD_UUID = "00008011-0000-1000-8000-00805f9b34fb"
OTA_DATA_UUID = "00008012-0000-1000-8000-00805f9b34fb"

async def ota_update(address, firmware_path):
    with open(firmware_path, "rb") as f:
        firmware = f.read()
    
    size = len(firmware)
    crc = zlib.crc32(firmware)
    
    async with BleakClient(address) as client:
        # 1. 订阅通知
        await client.start_notify(OTA_CMD_UUID, lambda s, d: print(f"Status: {d.hex()}"))
        
        # 2. 发送 Start 命令
        print(f"Starting OTA... Size: {size}")
        await client.write_gatt_char(OTA_CMD_UUID, struct.pack("<BI", 0x01, size), response=True)
        await asyncio.sleep(1) # 等待 ACK
        
        # 3. 发送数据
        chunk_size = 500
        for i in range(0, size, chunk_size):
            chunk = firmware[i:i+chunk_size]
            await client.write_gatt_char(OTA_DATA_UUID, chunk, response=False)
            print(f"Sent {i}/{size}", end="\r")
            
        # 4. 发送 End 命令
        print("\nSending End command...")
        await client.write_gatt_char(OTA_CMD_UUID, struct.pack("<BI", 0x02, crc), response=True)
        
        # 等待最终结果
        await asyncio.sleep(5)
```
