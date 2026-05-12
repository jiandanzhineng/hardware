# 通过 BLE 操作设备（用户指南）

- 面向使用者：只讲如何使用，不讲内部代码实现
- 设备属性与范围请参考 `docs/device_api_reference.md`

**1. 如何连接设备**

- 安装依赖：`pip install bleak`
- 使用设备名或地址连接；设备提供自定义服务 `0x00FF`

```python
import asyncio
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "BLUFI"

async def main():
    devices = await BleakScanner.discover()
    target = next(d for d in devices if (d.name or "") and DEVICE_NAME in d.name)
    async with BleakClient(target.address) as client:
        print("connected", client.is_connected)

asyncio.run(main())
```

**2. BLE 通用特征**

- 服务 UUID：`0x00FF`
- `0xFF01`：设备到主机消息通道，主机可读取最近一条消息，也可订阅通知
- `0xFF02`：模式切换通道，写入 `0x01` 表示进入 BLE 模式，写入 `0x00` 表示回到 WiFi 模式
- `0xFF03`：主机到设备命令通道，写入 UTF-8 JSON 命令

**3. 连接上切换为 BLE 模式**

- 写入模式特征 `0xFF02`，值 `0x01` 表示进入 BLE 模式，`0x00` 表示回到 WiFi 模式

```python
import asyncio
from bleak import BleakScanner, BleakClient

MODE_UUID = "0000ff02-0000-1000-8000-00805f9b34fb"
DEVICE_NAME = "BLUFI"

async def main():
    devices = await BleakScanner.discover()
    target = next(d for d in devices if (d.name or "") and DEVICE_NAME in d.name)
    async with BleakClient(target.address) as client:
        await client.write_gatt_char(MODE_UUID, bytes([1]), response=True)
        print("BLE mode on")

asyncio.run(main())
```

**4. 发送通用 blink 动作**

- 向命令特征 `0xFF03` 写入 JSON：`{"method":"action","action":"blink"}`

```python
import asyncio
from bleak import BleakScanner, BleakClient

COMMAND_UUID = "0000ff03-0000-1000-8000-00805f9b34fb"
DEVICE_NAME = "BLUFI"

async def main():
    devices = await BleakScanner.discover()
    target = next(d for d in devices if (d.name or "") and DEVICE_NAME in d.name)
    async with BleakClient(target.address) as client:
        payload = b'{"method":"action","action":"blink"}'
        await client.write_gatt_char(COMMAND_UUID, payload, response=True)

asyncio.run(main())
```

**5. 接收设备发送的信息**

- 订阅 `0xFF01` 可接收设备主动发送的信息；也可以直接读取最近一条信息

```python
import asyncio
from bleak import BleakScanner, BleakClient

MESSAGE_UUID = "0000ff01-0000-1000-8000-00805f9b34fb"
DEVICE_NAME = "BLUFI"

def on_message(_, data):
    print(data.decode("utf-8", errors="replace"))

async def main():
    devices = await BleakScanner.discover()
    target = next(d for d in devices if (d.name or "") and DEVICE_NAME in d.name)
    async with BleakClient(target.address) as client:
        await client.start_notify(MESSAGE_UUID, on_message)
        latest = await client.read_gatt_char(MESSAGE_UUID)
        if latest:
            print("latest", latest.decode("utf-8", errors="replace"))
        await asyncio.sleep(30)

asyncio.run(main())
```

**6. 如何使用设备（以 DIANJI 为例）**

- 思路：通过“用户描述”找到属性特征（例如 `voltage`、`shock`），对其值特征执行读/写；如需接收变化，订阅该特征通知
- 其他设备与更具体的属性说明请查看 `docs/device_api_reference.md`

```python
import asyncio, struct
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "BLUFI"

async def build_attr_map(client):
    m = {}
    for s in client.services:
        for c in s.characteristics:
            for d in c.descriptors:
                if d.uuid.endswith("2901"):
                    name = (await client.read_gatt_descriptor(d.handle)).decode().strip()
                    m[name] = c.uuid
    return m

async def main():
    devices = await BleakScanner.discover()
    target = next(d for d in devices if (d.name or "") and DEVICE_NAME in d.name)
    async with BleakClient(target.address) as client:
        attr = await build_attr_map(client)
        voltage_uuid = attr["voltage"]
        shock_uuid = attr["shock"]

        await client.write_gatt_char(voltage_uuid, struct.pack("<i", 50), response=True)
        await client.write_gatt_char(shock_uuid, bytes([1]), response=True)

        def on_notify(_, data):
            print("voltage", int.from_bytes(data, "little"))

        await client.start_notify(voltage_uuid, on_notify)
        data = await client.read_gatt_char(voltage_uuid)
        print("current", int.from_bytes(data, "little"))

asyncio.run(main())
```

**查看更具体的属性**

- 在项目目录打开 `e:\develop\smart\hard\project_td\docs\device_api_reference.md`
- 找到对应设备类型（如 DIANJI、TD01 等）的“设备属性”表；按属性名在 BLE 中通过“用户描述”定位特征，然后按类型编码进行读/写

**提示**

- 常用 16-bit UUID：服务 `0x00FF`，设备消息 `0xFF01`，模式特征 `0xFF02`，命令写入 `0xFF03`
- `int` 使用小端编码；`float` 为 4 字节二进制；`string` 为原文字节（≤32）
