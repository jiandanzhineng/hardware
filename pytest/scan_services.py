import asyncio
from bleak import BleakScanner, BleakClient

DEVICE_NAME = "BLUFI_DEVICE"

async def main():
    print(f"正在扫描名称为 '{DEVICE_NAME}' 的设备...")
    # 扫描并查找目标设备
    device = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.name and d.name == DEVICE_NAME
    )

    if not device:
        print(f"未找到设备 '{DEVICE_NAME}'")
        return

    print(f"找到设备: {device.name} [{device.address}]")
    print("正在连接...")
    
    try:
        async with BleakClient(device) as client:
            print(f"已连接: {client.is_connected}")
            
            print("\n" + "=" * 50)
            print("服务与特性列表")
            print("=" * 50)
            
            for service in client.services:
                print(f"\n[Service] {service.uuid} ({service.description})")
                
                for char in service.characteristics:
                    props = ", ".join(char.properties)
                    print(f"  └── [Char] {char.uuid} ({char.description})")
                    print(f"      Properties: {props}")
                    print(f"      Handle: {char.handle}")
                    
            print("\n" + "=" * 50)
            print("扫描完成")
            
    except Exception as e:
        print(f"连接或读取过程中出错: {e}")

if __name__ == "__main__":
    asyncio.run(main())
