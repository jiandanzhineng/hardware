"""Run production C lock logic against a fake GPTimer/GPIO on a host with cc.

Windows: wsl -e sh -lc 'cd /mnt/e/.../tests/sk_board && python3 tests/run_host_tests.py'
No boards, GPIOs or serial ports are accessed. This cannot validate ISR latency.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="sk-lock-test-") as temp:
    temp = Path(temp)
    for name in ("esp_err.h", "esp_attr.h", "driver/gpio.h", "driver/gptimer.h", "freertos/FreeRTOS.h"):
        path = temp / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("/* Supplied by -include idf_stubs.h. */\n")
    for enabled, low in ((0, 0), (1, 1), (1, 0)):
        (temp / "sdkconfig.h").write_text(
            f"#define CONFIG_SK_LOCK_OUTPUT_ENABLED {enabled}\n"
            f"#define CONFIG_SK_LOCK_ACTIVE_LOW {low}\n"
            "#define CONFIG_GPTIMER_ISR_IRAM_SAFE 1\n"
            "#define CONFIG_GPIO_CTRL_FUNC_IN_IRAM 1\n"
            "#define CONFIG_GPTIMER_CTRL_FUNC_IN_IRAM 1\n"
        )
        exe = temp / "test_lock"
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
            "-fsanitize=undefined", "-g", "-I", str(temp), "-I", str(ROOT / "main"),
            "-include", str(ROOT / "tests/idf_stubs.h"),
            str(ROOT / "tests/test_lock.c"), str(ROOT / "main/lock_control.c"),
            str(ROOT / "main/test_policy.c"), "-o", str(exe),
        ], check=True)
        for scenario in (("normal", "buttons", "alarm_failure", "read_failure", "gpio_failure") if enabled else ("disabled",)):
            subprocess.run([str(exe), scenario], check=True)
print("All 11 host scenarios passed (hardware/RTOS timing remains untested).")
