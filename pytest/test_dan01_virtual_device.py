import math

from device_manager import DeviceManager
from virtual_devices import DAN01Device


def test_dan01_is_registered():
    device = DeviceManager().create_device("dan01", "dan01-test")
    assert isinstance(device, DAN01Device)
    assert device.device_type == "DAN01"


def test_dan01_sample_uses_documented_hex_layout():
    sample = DAN01Device._build_attitude_sample(1.25)
    assert len(sample["accel"]) == 12
    assert len(sample["gyro"]) == 12
    assert len(sample["mag"]) == 12
    assert len(sample["quat"]) == 16
    assert all(char in "0123456789abcdef" for char in sample["quat"])

    raw = bytes.fromhex(sample["quat"])
    q = [int.from_bytes(raw[i:i + 2], "little", signed=True) / 32768 for i in range(0, 8, 2)]
    assert math.isclose(sum(value * value for value in q), 1.0, abs_tol=0.001)


def test_dan01_report_delay_and_calibration_actions():
    device = DAN01Device("dan01-test")
    device._on_property_changed("report_delay_ms", 1, -1)
    assert device.properties["report_delay_ms"]["value"] == 50
    device._on_action({"method": "action", "action": "mag_cal_start"})
    assert device.mag_calibrating is True
    device._on_action({"method": "action", "action": "mag_cal_end"})
    assert device.mag_calibrating is False
