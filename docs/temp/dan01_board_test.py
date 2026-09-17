#!/usr/bin/env python3
"""DAN01 serial smoke test for a connected development board."""

import argparse
import json
import math
import re
import time

import serial


IDENTITY_RE = re.compile(r"^@DEBUG (?:IDENT|READY) (\{.*\})$")


def decode_i16_hex(value, count, scale):
    if not isinstance(value, str) or len(value) != count * 4:
        raise AssertionError(f"invalid HEX vector length: {value!r}")
    raw = bytes.fromhex(value)
    return [
        int.from_bytes(raw[index:index + 2], "little", signed=True) * scale
        for index in range(0, len(raw), 2)
    ]


def read_lines(port, seconds):
    deadline = time.monotonic() + seconds
    lines = []
    while time.monotonic() < deadline:
        line = port.readline().decode("utf-8", errors="replace").strip()
        if line:
            lines.append(line)
    return lines


def write_line(port, value):
    port.write((value + "\n").encode("utf-8"))
    port.flush()


def main():
    parser = argparse.ArgumentParser(description="Validate DAN01 over the serial debug protocol")
    parser.add_argument("--port", default="COM16")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=6.0)
    args = parser.parse_args()

    with serial.Serial(
        args.port, args.baud, timeout=0.2, dsrdtr=False, rtscts=False
    ) as port:
        port.dtr = False
        port.rts = False
        time.sleep(0.4)
        port.reset_input_buffer()
        write_line(port, "@DEBUG IDENTIFY")
        identity_lines = read_lines(port, 1.5)
        write_line(port, "@DEBUG START")
        startup_lines = read_lines(port, 1.5)
        write_line(port, '@CMD {"method":"set","key":"report_delay_ms","value":100,"msg_id":7001}')
        write_line(port, '@CMD {"method":"get","key":"report_delay_ms","msg_id":7002}')
        data_lines = read_lines(port, args.seconds)
        write_line(port, '@CMD {"method":"get","key":"battery","msg_id":7003}')
        data_lines += read_lines(port, 2.0)

    identities = []
    messages = []
    for line in identity_lines + startup_lines + data_lines:
        identity_match = IDENTITY_RE.match(line)
        if identity_match:
            identities.append(json.loads(identity_match.group(1)))
        if line.startswith("@MSG "):
            messages.append(json.loads(line[5:]))

    identity = next((item for item in identities if item.get("device_type") == "DAN01"), None)
    if not identity:
        raise AssertionError("DAN01 identity frame not received")
    if not re.fullmatch(r"[0-9a-f]{12}", identity.get("device_id", "")):
        raise AssertionError(f"invalid device_id: {identity.get('device_id')!r}")

    delay_response = next(
        (item for item in messages if item.get("msg_id") == 7002 and item.get("key") == "report_delay_ms"),
        None,
    )
    if not delay_response or delay_response.get("value") != 100:
        raise AssertionError("report_delay_ms set/get did not round-trip")

    updates = [
        item for item in messages
        if item.get("method") in ("update", "report") and isinstance(item.get("quat"), str)
        and len(item["quat"]) == 16
    ]
    if len(updates) < 3:
        raise AssertionError(f"expected at least 3 sensor updates, received {len(updates)}")

    distinct_quaternions = {item["quat"] for item in updates}
    if len(distinct_quaternions) < 2:
        raise AssertionError("quaternion did not change while sampling")

    latest = updates[-1]
    accel = decode_i16_hex(latest.get("accel"), 3, 0.00478515625)
    gyro = decode_i16_hex(latest.get("gyro"), 3, 0.0625)
    mag = decode_i16_hex(latest.get("mag"), 3, 0.006103515625)
    quat = decode_i16_hex(latest.get("quat"), 4, 0.000030517578125)
    quat_norm = math.sqrt(sum(value * value for value in quat))
    if not 0.9 <= quat_norm <= 1.1:
        raise AssertionError(f"quaternion norm out of range: {quat_norm}")
    if not -45 <= float(latest.get("temperature")) <= 85:
        raise AssertionError(f"temperature out of range: {latest.get('temperature')}")
    if not -500 <= float(latest.get("height")) <= 9000:
        raise AssertionError(f"height out of range: {latest.get('height')}")

    battery_response = next(
        (item for item in messages if item.get("msg_id") == 7003 and item.get("key") == "battery"),
        None,
    )
    if battery_response is None or not isinstance(battery_response.get("value"), int):
        raise AssertionError("battery get did not return an int")
    battery = battery_response["value"]
    if not 0 <= battery <= 100:
        raise AssertionError(f"battery out of range: {battery}")

    print(json.dumps({
        "ok": True,
        "port": args.port,
        "identity": identity,
        "sensor_updates": len(updates),
        "distinct_quaternions": len(distinct_quaternions),
        "quat_norm": round(quat_norm, 5),
        "accel": [round(value, 3) for value in accel],
        "gyro": [round(value, 3) for value in gyro],
        "mag": [round(value, 3) for value in mag],
        "temperature": latest["temperature"],
        "height": latest["height"],
        "mag_accuracy": latest.get("mag_accuracy"),
        "battery": battery,
        "button0": latest.get("button0"),
    }, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
