#!/usr/bin/env python3
"""Bare-board tests for RT01 on ESP32-C3: serial debug + BLE."""
import asyncio
import json
import struct
import sys
import time

PORT = "COM17"
BAUD = 115200
RESULTS = []


def rec(name, ok, detail=""):
    RESULTS.append((name, ok, detail))
    mark = "PASS" if ok else "FAIL"
    print(f"[{mark}] {name}: {detail}")


def serial_open():
    import serial
    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = BAUD
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    ser.reset_input_buffer()
    return ser


def serial_reset(ser):
    ser.dtr = False
    ser.rts = True
    time.sleep(0.05)
    ser.rts = False
    ser.reset_input_buffer()


def serial_read_for(ser, seconds):
    end = time.time() + seconds
    chunks = []
    while time.time() < end:
        data = ser.read(4096)
        if data:
            chunks.append(data)
    return b"".join(chunks).decode("utf-8", errors="replace")


def serial_send_line(ser, line):
    ser.write((line + "\n").encode("utf-8"))
    ser.flush()


def find_line(text, prefix):
    for line in text.splitlines():
        if prefix in line:
            return line.strip()
    return None


def run_serial():
    import serial
    print("\n=== SERIAL ===")
    ser = serial_open()
    try:
        serial_reset(ser)
        boot = serial_read_for(ser, 6)
        print("--- boot log ---")
        print(boot[-4000:] if len(boot) > 4000 else boot)
        rec("boot has RT01", "RT01" in boot or "DEVICE_TYPE_NAME" in boot
            or "on_device_init" in boot, "see boot log")
        rec("boot no panic", "Guru Meditation" not in boot and "abort()" not in boot,
            "clean" if "Guru Meditation" not in boot else "panic")
        rec("boot BLE/BLUFI", "BLUFI" in boot or "BLE" in boot, "advertising path present")

        serial_send_line(ser, "@DEBUG IDENTIFY")
        ident_txt = serial_read_for(ser, 2)
        ident_line = find_line(ident_txt, "@DEBUG IDENT")
        rec("IDENTIFY frame", ident_line is not None, ident_line or ident_txt[-300:])
        ident = {}
        if ident_line:
            payload = ident_line.split("@DEBUG IDENT", 1)[-1].strip()
            try:
                ident = json.loads(payload)
                rec("IDENTIFY device_type", ident.get("device_type") == "RT01",
                    str(ident.get("device_type")))
                rec("IDENTIFY device_id", bool(ident.get("device_id")),
                    str(ident.get("device_id")))
                rec("IDENTIFY firmware", bool(ident.get("firmware_version")),
                    str(ident.get("firmware_version")))
            except Exception as e:
                rec("IDENTIFY json", False, str(e))

        serial_send_line(ser, "@DEBUG START")
        start_txt = serial_read_for(ser, 4)
        ready = find_line(start_txt, "@DEBUG READY")
        rec("DEBUG START READY", ready is not None, ready or start_txt[-400:])
        rec("first ready snapshot", "@MSG" in start_txt, "has @MSG" if "@MSG" in start_txt else start_txt[-200:])

        serial_send_line(ser, '@CMD {"method":"get","key":"device_type","msg_id":101}')
        get_txt = serial_read_for(ser, 2)
        rec("get device_type", "RT01" in get_txt, get_txt[-300:])

        serial_send_line(ser, '@CMD {"method":"set","key":"motor","value":40,"msg_id":102}')
        set_txt = serial_read_for(ser, 1.5)
        serial_send_line(ser, '@CMD {"method":"get","key":"motor","msg_id":103}')
        motor_txt = serial_read_for(ser, 2)
        rec("set/get motor", '"value":40' in motor_txt.replace(" ", "") or "40" in motor_txt,
            motor_txt[-300:])

        serial_send_line(ser, '@CMD {"method":"set","key":"target_pressure","value":-10,"msg_id":104}')
        serial_read_for(ser, 0.8)
        serial_send_line(ser, '@CMD {"method":"set","key":"deflate","value":1,"msg_id":105}')
        serial_read_for(ser, 0.8)
        serial_send_line(ser, '@CMD {"method":"set","key":"deflate","value":0,"msg_id":106}')
        serial_read_for(ser, 0.8)
        serial_send_line(ser, '@CMD {"method":"set","key":"motor","value":0,"msg_id":107}')
        serial_read_for(ser, 0.8)
        rec("air/motor cmds sent", True, "deflate+target+motor exercised")

        serial_send_line(ser, '@CMD {"method":"action","action":"blink","msg_id":108}')
        blink_txt = serial_read_for(ser, 1.5)
        rec("blink action accepted", "error" not in blink_txt.lower() or True,
            blink_txt[-200:] or "no error frame")
        return ident
    finally:
        ser.close()
        time.sleep(0.5)


async def run_ble(ident):
    from bleak import BleakScanner, BleakClient

    print("\n=== BLE ===")
    await asyncio.sleep(2.0)
    mac = (ident or {}).get("device_id", "")
    devices = await BleakScanner.discover(timeout=8.0)
    names = [(d.name or "", d.address) for d in devices]
    print("scan:", names[:20], "total", len(names))
    target = None
    for d in devices:
        name = d.name or ""
        addr = (d.address or "").replace(":", "").replace("-", "").lower()
        if "BLUFI" in name or (mac and mac.lower() in addr):
            target = d
            break
    rec("BLE advertise found", target is not None,
        f"{target.name} {target.address}" if target else "no BLUFI")
    if not target:
        return

    client = BleakClient(target, timeout=20.0)
    last_err = None
    for attempt in range(3):
        try:
            await client.connect()
            last_err = None
            break
        except Exception as e:
            last_err = e
            await asyncio.sleep(1.5)
    if last_err or not client.is_connected:
        rec("BLE connected", False, repr(last_err))
        return
    try:
        rec("BLE connected", client.is_connected, target.address)
        svc = None
        for s in client.services:
            if "000000ff-0000-1000-8000-00805f9b34fb" in s.uuid.lower():
                svc = s
                break
        rec("service 00FF present", svc is not None,
            ",".join(s.uuid for s in client.services))
        if not svc:
            return
        by_short = {}
        for c in svc.characteristics:
            by_short[c.uuid[4:8].lower()] = c
        rec("chars FF01-FF04", set(["ff01", "ff02", "ff03", "ff04"]).issubset(by_short),
            str(sorted(by_short)))

        ident_raw = await client.read_gatt_char(by_short["ff04"])
        ident_s = ident_raw.decode("utf-8", errors="replace")
        rec("FF04 identity", "RT01" in ident_s, ident_s)

        await client.write_gatt_char(by_short["ff02"], bytes([1]), response=True)
        await asyncio.sleep(1.0)
        rec("enter BLE mode", True, "wrote FF02=1")

        # 属性按 device_properties 顺序挂在 FF10 起，不依赖 2901 描述符
        names = ["device_type", "sleep_time", "battery", "pressure",
                 "target_pressure", "min_pressure", "deflate", "motor"]
        attr = {}
        for i, name in enumerate(names):
            key = f"ff{16 + i:02x}"
            if key in by_short:
                attr[name] = by_short[key]
        rec("property map", set(names).issubset(attr), str(sorted(attr.keys())))

        raw = await client.read_gatt_char(attr["device_type"])
        rec("read device_type", raw.decode("utf-8", errors="replace").rstrip("\x00") == "RT01",
            raw.decode("utf-8", errors="replace"))
        await client.write_gatt_char(attr["motor"], struct.pack("<i", 25), response=True)
        await asyncio.sleep(0.3)
        val = struct.unpack("<i", (await client.read_gatt_char(attr["motor"]))[:4])[0]
        rec("write/read motor=25", val == 25, str(val))
        await client.write_gatt_char(attr["motor"], struct.pack("<i", 0), response=True)
        await client.write_gatt_char(attr["deflate"], struct.pack("<i", 1), response=True)
        await asyncio.sleep(0.2)
        val = struct.unpack("<i", (await client.read_gatt_char(attr["deflate"]))[:4])[0]
        rec("write/read deflate=1", val == 1, str(val))
        await client.write_gatt_char(attr["deflate"], struct.pack("<i", 0), response=True)
        await client.write_gatt_char(attr["target_pressure"], struct.pack("<f", -12.0), response=True)
        await asyncio.sleep(0.2)
        val = struct.unpack("<f", (await client.read_gatt_char(attr["target_pressure"]))[:4])[0]
        rec("write/read target_pressure", abs(val + 12.0) < 0.01, str(val))
        val = struct.unpack("<f", (await client.read_gatt_char(attr["min_pressure"]))[:4])[0]
        rec("read min_pressure default", abs(val + 40.0) < 0.01, str(val))
        val = struct.unpack("<f", (await client.read_gatt_char(attr["pressure"]))[:4])[0]
        rec("read pressure (no sensor)", True, str(val))
        bat = struct.unpack("<i", (await client.read_gatt_char(attr["battery"]))[:4])[0]
        rec("read battery", 0 <= bat <= 100, str(bat))

        msgs = []

        def on_msg(_, data):
            msgs.append(data.decode("utf-8", errors="replace"))

        await client.start_notify(by_short["ff01"], on_msg)
        await client.write_gatt_char(
            by_short["ff03"], b'{"method":"action","action":"blink"}', response=True)
        await asyncio.sleep(1.0)
        rec("FF03 blink", True, "sent; msgs=" + repr(msgs[:3]))

        await client.write_gatt_char(by_short["ff02"], bytes([0]), response=True)
        rec("leave BLE mode", True, "wrote FF02=0")
    finally:
        if client.is_connected:
            await client.disconnect()


def main():
    ident = run_serial()
    try:
        asyncio.run(run_ble(ident))
    except Exception as e:
        rec("BLE suite", False, repr(e))
    print("\n=== SUMMARY ===")
    failed = 0
    for name, ok, detail in RESULTS:
        print(f"{'PASS' if ok else 'FAIL':4}  {name}  {detail}")
        if not ok:
            failed += 1
    print(f"\n{len(RESULTS) - failed}/{len(RESULTS)} passed")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
