import asyncio
import struct
from bleak import BleakClient, BleakScanner

OTA_CMD_UUID = "00008011-0000-1000-8000-00805f9b34fb"
DEVICE_NAME = "BLUFI_DEVICE"

async def main():
    print(f"Scanning for {DEVICE_NAME}...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name == DEVICE_NAME
    )
    
    if not device:
        print("Device not found")
        return

    print(f"Connecting to {device.address}...")
    async with BleakClient(device, timeout=20.0) as client:
        print("Connected.")
        
        event = asyncio.Event()
        
        def callback(sender, data):
            print(f"Notify: {data.hex()}")
            event.set()

        await client.start_notify(OTA_CMD_UUID, callback)
        
        # Test: 1 byte (ABORT)
        print("\nSending 1 byte (CMD_ABORT)...")
        try:
            await client.write_gatt_char(OTA_CMD_UUID, b'\x03', response=True)
            print("Write Success. Waiting for notification...")
            try:
                await asyncio.wait_for(event.wait(), timeout=5.0)
                print("Notification received!")
            except asyncio.TimeoutError:
                print("Timeout waiting for notification.")
        except Exception as e:
            print(f"Write Failed: {e}")

if __name__ == "__main__":
    asyncio.run(main())
