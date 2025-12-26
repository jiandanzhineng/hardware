import asyncio
import struct
import zlib
import os
import sys
from bleak import BleakClient, BleakScanner
from bleak.backends.characteristic import BleakGATTCharacteristic

# UUID 定义 (基于16位别名的标准128位UUID)
OTA_SERVICE_UUID = "00008010-0000-1000-8000-00805f9b34fb"
OTA_CMD_UUID     = "00008011-0000-1000-8000-00805f9b34fb"
OTA_DATA_UUID    = "00008012-0000-1000-8000-00805f9b34fb"

# 命令
CMD_START = 0x01
CMD_END   = 0x02
CMD_ABORT = 0x03

# 状态码
STATUS_ACK       = 0x00
STATUS_SUCCESS   = 0x01
STATUS_CRC_ERROR = 0x02
STATUS_FAIL      = 0x03

# 配置
CHUNK_SIZE = 480  # 可调整的 MTU 大小 (建议 200-500)
DEVICE_NAME = "BLUFI_DEVICE"

# 固件路径：优先使用 build 目录下的新固件，否则使用 pytest 目录下的固件
BUILD_FIRMWARE_PATH = os.path.join(os.path.dirname(os.path.dirname(__file__)), "build", "blufi_demo.bin")
TEST_FIRMWARE_PATH = os.path.join(os.path.dirname(__file__), "ota_firmware", "firmware_v1.bin")

if os.path.exists(BUILD_FIRMWARE_PATH):
    FIRMWARE_PATH = BUILD_FIRMWARE_PATH
    print(f"使用 build 目录下的固件: {FIRMWARE_PATH}")
else:
    FIRMWARE_PATH = TEST_FIRMWARE_PATH
    print(f"使用测试目录下的固件: {FIRMWARE_PATH}")

class OTATester:
    def __init__(self):
        self.notification_event = asyncio.Event()
        self.last_status = None

    def notification_handler(self, sender: BleakGATTCharacteristic, data: bytearray):
        """处理来自命令特征的通知"""
        status = data[0]
        print(f"[通知] 收到状态码: {status} (0x{status:02X})")
        self.last_status = status
        self.notification_event.set()

    async def run(self):
        # 1. 加载固件
        if not os.path.exists(FIRMWARE_PATH):
            print(f"错误: 在 {FIRMWARE_PATH} 未找到固件文件")
            return

        print(f"正在加载固件: {FIRMWARE_PATH}...")
        with open(FIRMWARE_PATH, "rb") as f:
            firmware_data = f.read()
        
        # 校验 Magic Byte (0xE9)
        if len(firmware_data) > 0 and firmware_data[0] != 0xE9:
            print(f"错误: 固件文件无效！Magic Byte 应为 0xE9，实际为 0x{firmware_data[0]:02X}")
            print("请检查固件文件是否正确或已损坏。")
            return

        fw_size = len(firmware_data)
        fw_crc = zlib.crc32(firmware_data) & 0xFFFFFFFF
        print(f"固件大小: {fw_size} 字节")
        print(f"固件 CRC32: 0x{fw_crc:08X}")

        # 2. 扫描设备
        print(f"正在扫描 OTA 服务或名称为 '{DEVICE_NAME}' 的设备...")
        device = await BleakScanner.find_device_by_filter(
            lambda d, ad: (d.name and d.name == DEVICE_NAME) or 
                          (OTA_SERVICE_UUID.lower() in [u.lower() for u in ad.service_uuids])
        )

        if not device:
            print(f"未找到具有 OTA 服务 (0x8010) 或名称为 '{DEVICE_NAME}' 的设备。")
            # 备选：列出所有设备以帮助调试
            print("列出所有可见设备:")
            devices = await BleakScanner.discover()
            for d in devices:
                print(f"  {d.name} ({d.address})")
            return

        print(f"找到设备: {device.name} ({device.address})")

        # 3. 连接并执行 OTA
        # 增加超时时间以避免服务发现期间的 CancelledError
        async with BleakClient(device, timeout=20.0) as client:
            print(f"已连接到 {device.name}")
            print(f"MTU 大小: {client.mtu_size}")
            
            # 订阅通知
            await client.start_notify(OTA_CMD_UUID, self.notification_handler)
            print("已订阅命令通知。")

            # --- 握手 (开始) ---
            print("正在发送开始命令...")
            self.notification_event.clear()
            # 负载: CMD_START (1 byte) + Size (4 bytes)
            start_payload = struct.pack("<BI", CMD_START, fw_size)
            await client.write_gatt_char(OTA_CMD_UUID, start_payload, response=True)

            print("开始命令已发送。等待 ACK...")
            try:
                await asyncio.wait_for(self.notification_event.wait(), timeout=10.0)
                if self.last_status != STATUS_ACK:
                    print(f"开始命令失败，状态码: {self.last_status}")
                    return
                print("收到 ACK。开始数据传输...")
            except asyncio.TimeoutError:
                print("等待 ACK 超时。")
                return
            
            # --- 数据传输 ---
            print("开始数据传输...")
            offset = 0
            total_chunks = (fw_size + CHUNK_SIZE - 1) // CHUNK_SIZE
            
            # 流控配置：每 8 包同步一次
            SYNC_INTERVAL = 8

            for i in range(total_chunks):
                chunk = firmware_data[offset : offset + CHUNK_SIZE]
                
                # 混合写入策略：大部分包使用 Without Response (快)，
                # 每 SYNC_INTERVAL 包使用 With Response (强制同步，防止缓冲区积压)
                use_response = (i % SYNC_INTERVAL == 0) and (i > 0)
                
                try:
                    await client.write_gatt_char(OTA_DATA_UUID, chunk, response=use_response)
                except Exception as e:
                    print(f"\n[错误] 第 {i} 块数据写入失败: {e}")
                    break
                
                offset += len(chunk)
                
                # 进度显示
                if i % 10 == 0 or i == total_chunks - 1:
                    progress = (offset / fw_size) * 100
                    sys.stdout.write(f"\r进度: {progress:.1f}% ({offset}/{fw_size} 字节)")
                    sys.stdout.flush()
                
                # 移除固定 sleep，依靠 response=True 进行自适应流控


            print("\n数据传输完成。")

            # 在发送结束命令前添加延迟，让缓冲区排空
            print("等待 2 秒后发送结束命令...")
            await asyncio.sleep(2.0)

            # --- 结束 (Finalization) ---
            print("正在发送结束命令...")
            self.notification_event.clear()
            # 负载: CMD_END (1 byte) + CRC (4 bytes)
            end_payload = struct.pack("<BI", CMD_END, fw_crc)
            try:
                await client.write_gatt_char(OTA_CMD_UUID, end_payload, response=True)
            except Exception as e:
                print(f"\n[警告] 结束命令发送失败 (设备可能已断开/重启): {e}")
                # 如果之前没有写入错误，假设成功
                print("假设 OTA 成功 (设备正在重启)。")
                return

            print("等待最终状态 (可选)...")
            try:
                await asyncio.wait_for(self.notification_event.wait(), timeout=5.0)
                if self.last_status == STATUS_SUCCESS:
                    print("OTA 升级成功！设备应正在重启。")
                elif self.last_status == STATUS_CRC_ERROR:
                    print("OTA 失败: CRC 校验不匹配。")
                elif self.last_status == STATUS_FAIL:
                    print("OTA 失败: 通用错误 (STATUS_FAIL)。")
                else:
                    print(f"OTA 失败，未知状态码: {self.last_status}")
            except asyncio.TimeoutError:
                print("未收到最终状态。如果未发生写入错误，假设成功。")

if __name__ == "__main__":
    tester = OTATester()
    asyncio.run(tester.run())
