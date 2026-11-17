#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "board.h"
#include "lock_control.h"
#include "test_policy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_event.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

typedef struct { int raw, lo, hi, mv; bool valid; } adc_sample_t;
static adc_oneshot_unit_handle_t adc;
static adc_cali_handle_t calibration[2];
static const adc_channel_t channels[] = {SK_BAT_CHANNEL, SK_POWER_KEY_CHANNEL};
static temperature_sensor_handle_t temperature;
static bool nvs_ready, adc_ready, shutdown_requested;
static bool wifi_ready, wifi_scanning;
static bool wifi_unavailable;
static portMUX_TYPE wifi_mux = portMUX_INITIALIZER_UNLOCKED;
static bool wifi_done;
static uint32_t wifi_scan_status;
static int64_t wifi_started_us;
static int64_t armed_until_us;
static adc_sample_t battery, power_key;
static sk_debounce_t keys[4];
static sk_button_test_t test_buttons;
static uint32_t key_presses[4];
static const char *key_names[] = {"K3/KEY1", "K4/KEY2", "K1/BOOT", "K5/POWER"};
static const int key_pins[] = {SK_KEY1_GPIO, SK_KEY2_GPIO, SK_BOOT_GPIO};
static bool power_key_released, power_key_hysteresis;
static int64_t power_pressed_us;
static int led_level, led_steps;
static int64_t led_next_us;
static int pulse_before_mv, pulse_min_mv = -1;
static uint32_t last_pulses, last_cutoffs;

static void print_result(const char *name, esp_err_t err)
{
    printf("[TEST] %s %s (%s)\n", name, err == ESP_OK ? "PASS" : "FAIL", esp_err_to_name(err));
}

static void power_hold_init(void)
{
    /* Q1 has no base resistor on the supplied drawing. The weak pull-up
       limits base current; power retention must be checked on the bench. */
    ESP_ERROR_CHECK(gpio_set_level(SK_HOLD_GPIO, 1));
    gpio_config_t hold = {
        .pin_bit_mask = 1ULL << SK_HOLD_GPIO,
#if CONFIG_SK_POWER_HOLD_PUSH_PULL
        .mode = GPIO_MODE_OUTPUT,
#else
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
#endif
    };
    ESP_ERROR_CHECK(gpio_config(&hold));
}

static void inputs_led_init(void)
{
    gpio_config_t inputs = {
        .pin_bit_mask = (1ULL << SK_KEY1_GPIO) | (1ULL << SK_KEY2_GPIO) | (1ULL << SK_BOOT_GPIO),
        .mode = GPIO_MODE_INPUT, /* External 10k pull-ups; retain their test coverage. */
    };
    ESP_ERROR_CHECK(gpio_config(&inputs));
    ESP_ERROR_CHECK(gpio_set_level(SK_LED_GPIO, 0));
    gpio_config_t led = {.pin_bit_mask = 1ULL << SK_LED_GPIO, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&led));
    led_steps = 6; /* Three visible flashes, no automatic lock test. */
}

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = {.unit_id = ADC_UNIT_1};
    esp_err_t err = adc_oneshot_new_unit(&unit, &adc);
    print_result("ADC1_INIT", err);
    if (err != ESP_OK) return;
    const adc_oneshot_chan_cfg_t cfg = {.bitwidth = ADC_BITWIDTH_12, .atten = ADC_ATTEN_DB_12};
    adc_ready = true;
    for (int i = 0; i < 2; i++) {
        err = adc_oneshot_config_channel(adc, channels[i], &cfg);
        print_result(i ? "KEY_DAT_ADC_CONFIG" : "BAT_ADC_CONFIG", err);
        if (err != ESP_OK) adc_ready = false;
        adc_cali_curve_fitting_config_t cal = {
            .unit_id = ADC_UNIT_1, .chan = channels[i],
            .atten = cfg.atten, .bitwidth = cfg.bitwidth,
        };
        err = adc_cali_create_scheme_curve_fitting(&cal, &calibration[i]);
        print_result(i ? "KEY_DAT_CALIBRATION" : "BAT_CALIBRATION", err);
        if (err != ESP_OK) calibration[i] = NULL;
    }
}

static adc_sample_t read_adc(int index)
{
    adc_sample_t s = {.raw = -1, .lo = 4095, .hi = 0, .mv = -1};
    if (!adc_ready) return s;
    int value, total = 0;
    /* Discard one reading after multiplexing, then average eight. */
    if (adc_oneshot_read(adc, channels[index], &value) != ESP_OK) return s;
    for (int i = 0; i < 8; i++) {
        if (adc_oneshot_read(adc, channels[index], &value) != ESP_OK) return s;
        if (value < s.lo) s.lo = value;
        if (value > s.hi) s.hi = value;
        total += value;
    }
    s.raw = (total + 4) / 8;
    if (calibration[index] && adc_cali_raw_to_voltage(calibration[index], s.raw, &s.mv) == ESP_OK)
        s.valid = s.hi < 4090;
    return s;
}

static bool battery_ok(void)
{
    return sk_battery_allows_pulse(battery.valid, sk_battery_mv(battery.mv));
}

static void identity(void)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    const esp_app_desc_t *app = esp_app_get_description();
    uint8_t mac[6] = {0};
    esp_err_t mac_err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t flash_size = 0;
    esp_err_t flash_err = esp_flash_get_size(NULL, &flash_size);
    printf("[SYS] board=SK_V1.0 chip=ESP32-C3 cores=%d rev=%d idf=%s app=%s reset_reason=%d\n",
           info.cores, info.revision, esp_get_idf_version(), app->version, esp_reset_reason());
    printf("[SYS] mac=%02x:%02x:%02x:%02x:%02x:%02x status=%s flash_bytes=%" PRIu32 " status=%s\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], esp_err_to_name(mac_err),
           flash_size, esp_err_to_name(flash_err));
    printf("[PIN] BAT=GPIO0 ADC1_CH0 x2; HOLD=GPIO1; POWER=GPIO2 ADC1_CH2; LOCK=GPIO3; LED=GPIO4; KEY1=GPIO8; BOOT=GPIO9; KEY2=GPIO10\n");
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    printf("[SERIAL] USB Serial/JTAG GPIO18=D- GPIO19=D+; terminal=115200 8N1\n");
#else
    printf("[SERIAL] UART0 GPIO21=TX GPIO20=RX; terminal=115200 8N1, 3.3V adapter\n");
#endif
    printf("[LIMIT] lock_on=UNLOCK; request=20..1000ms default=100ms cooldown=10000ms; no automatic pulses\n");
    printf("[HW] VLOCK comes from VBAT (not regulated 5V). No lock feedback/current/coil-temperature ADC.\n");
    printf("[HW] Original V1.0 needs GPIO3 level isolation and inductive suppression; GPIO1 needs base resistor.\n");
#if CONFIG_SK_LOCK_OUTPUT_ENABLED
    printf("[LOCK] ENABLED: button pulses or serial arm/pulse; boot cooldown=10000ms; hardware unverified.\n");
    printf("[KEY_MAP] K3/KEY1=%ums K4/KEY2=%ums K1/BOOT=%ums; buttons need no serial arm; release all before next press\n",
           SK_KEY1_PULSE_MS, SK_KEY2_PULSE_MS, SK_BOOT_PULSE_MS);
#else
    printf("[LOCK] DISABLED by build configuration; GPIO3 high-Z.\n");
#endif
#if !CONFIG_SK_POWER_HOLD_PUSH_PULL
    printf("[POWER] Weak internal pull-up hold; verify retention. Do not drive Q1 base push-pull without resistor.\n");
#endif
}

static void selftest(void)
{
    sk_lock_off();
    armed_until_us = 0;
    identity();
    print_result("HEAP_INTEGRITY", heap_caps_check_integrity_all(true) ? ESP_OK : ESP_FAIL);
    const esp_partition_t *part = esp_ota_get_running_partition();
    uint8_t digest[32];
    esp_err_t err = part ? esp_partition_get_sha256(part, digest) : ESP_FAIL;
    print_result("APP_FLASH_SHA256_READ", err);
    if (err == ESP_OK) {
        printf("[FLASH] app_sha256=");
        for (int i = 0; i < 32; i++) printf("%02x", digest[i]);
        printf("\n");
    }
    if (!nvs_ready) { printf("[TEST] NVS_RW SKIP initialization failed; no automatic erase\n"); return; }
    nvs_handle_t handle;
    err = nvs_open("sk_diag", NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        uint32_t saved = 0, actual = 0, expected = esp_random();
        esp_err_t old = nvs_get_u32(handle, "probe", &saved);
        if (old != ESP_OK && old != ESP_ERR_NVS_NOT_FOUND) err = old;
        if (err == ESP_OK) err = nvs_set_u32(handle, "probe", expected);
        if (err == ESP_OK) err = nvs_commit(handle);
        if (err == ESP_OK) err = nvs_get_u32(handle, "probe", &actual);
        if (err == ESP_OK && actual != expected) err = ESP_FAIL;
        esp_err_t restore = ESP_OK;
        if (old == ESP_OK) restore = nvs_set_u32(handle, "probe", saved);
        else if (old == ESP_ERR_NVS_NOT_FOUND) {
            restore = nvs_erase_key(handle, "probe");
            if (restore == ESP_ERR_NVS_NOT_FOUND) restore = ESP_OK;
        }
        if (restore == ESP_OK) restore = nvs_commit(handle);
        if (err == ESP_OK) err = restore;
        nvs_close(handle);
    }
    print_result("NVS_RW_RESTORE", err);
    printf("[TEST] LED/KEY/POWER/USB/LOCK require physical observation; charge/5V/3V3/VLOCK/current require a meter\n");
}

static void report(void)
{
    sk_lock_status_t lock = sk_lock_status();
    const int mv = sk_battery_mv(battery.mv);
    const char *bat_status = !battery.valid ? "INVALID" : mv < SK_BAT_MIN_MV ? "LOW" :
                            mv > SK_BAT_MAX_MV ? "HIGH" : "IN_RANGE";
    printf("[ADC] raw=%d min=%d max=%d adc_mv=%d battery_mv=%d calibration=%s status=%s\n",
           battery.raw, battery.lo, battery.hi, battery.mv, mv, calibration[0] ? "EFUSE" : "UNAVAILABLE", bat_status);
    printf("[KEY] K3=%d/%" PRIu32 " K4=%d/%" PRIu32 " BOOT=%d/%" PRIu32
           " POWER=%d/%" PRIu32 " key_raw=%d key_mv=%d valid=%d (pressed/count)\n",
           keys[0].stable, key_presses[0], keys[1].stable, key_presses[1],
           keys[2].stable, key_presses[2], keys[3].stable, key_presses[3],
           power_key.raw, power_key.mv, power_key.valid);
    printf("[LOCK] enabled=%d commanded_on=%d fault=%d armed=%d cooldown_ms=%" PRIu32
           " pulses=%" PRIu32 " timer_cutoffs=%" PRIu32 " feedback=UNAVAILABLE\n",
           lock.enabled, lock.active, lock.fault, armed_until_us > esp_timer_get_time(),
           lock.cooldown_ms, lock.pulses, lock.cutoffs);
    float celsius;
    if (temperature && temperature_sensor_get_celsius(temperature, &celsius) == ESP_OK)
        printf("[TEMP] chip_celsius=%.1f (not coil or battery temperature)\n", (double)celsius);
    else printf("[TEMP] INVALID\n");
    printf("[SYS] uptime_ms=%" PRId64 " heap_free=%" PRIu32 " heap_min=%" PRIu32 " led=%d\n",
           esp_timer_get_time() / 1000, esp_get_free_heap_size(), esp_get_minimum_free_heap_size(), led_level);
}

static void poweroff(void)
{
    sk_lock_off();
    armed_until_us = 0;
    shutdown_requested = true;
    led_steps = led_level = 0;
    gpio_set_level(SK_LED_GPIO, 0);
    printf("[POWER] lock command OFF; dropping PWR_HOLD. Release K5; verify rails fall with a meter.\n");
    fflush(stdout);
    gpio_set_level(SK_HOLD_GPIO, 0);
    /* If K5/back-power keeps the MCU alive, remain in the OFF state. */
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id;
    wifi_event_sta_scan_done_t *event = data;
    portENTER_CRITICAL(&wifi_mux);
    wifi_scan_status = event->status;
    wifi_done = true;
    portEXIT_CRITICAL(&wifi_mux);
}

static void wifi_start_scan(void)
{
    sk_lock_off();
    armed_until_us = 0;
    if (wifi_scanning) { printf("[WIFI] BUSY\n"); return; }
    if (wifi_unavailable) { printf("[WIFI] initialization previously failed; reboot before retry\n"); return; }
    esp_err_t err = ESP_OK;
    if (!wifi_ready) {
        if (!nvs_ready) { printf("[WIFI] SKIP NVS unavailable\n"); return; }
        if ((err = esp_netif_init()) != ESP_OK ||
            (err = esp_event_loop_create_default()) != ESP_OK) goto fail;
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if ((err = esp_wifi_init(&cfg)) != ESP_OK ||
            (err = esp_wifi_set_storage(WIFI_STORAGE_RAM)) != ESP_OK ||
            (err = esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, wifi_event, NULL)) != ESP_OK ||
            (err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) goto fail;
        wifi_ready = true;
    }
    if ((err = esp_wifi_start()) != ESP_OK) goto fail;
    wifi_scan_config_t scan = {.scan_type = WIFI_SCAN_TYPE_PASSIVE};
    scan.scan_time.passive = 120;
    portENTER_CRITICAL(&wifi_mux);
    wifi_done = false;
    portEXIT_CRITICAL(&wifi_mux);
    if ((err = esp_wifi_scan_start(&scan, false)) != ESP_OK) { esp_wifi_stop(); goto fail; }
    wifi_scanning = true;
    wifi_started_us = esp_timer_get_time();
    printf("[WIFI] passive scan started; no association; lock disarmed\n");
    return;
fail:
    wifi_unavailable = true;
    print_result("WIFI_SCAN_START", err);
}

static void wifi_poll(void)
{
    if (!wifi_scanning) return;
    portENTER_CRITICAL(&wifi_mux);
    bool done = wifi_done;
    uint32_t status = wifi_scan_status;
    portEXIT_CRITICAL(&wifi_mux);
    if (!done && esp_timer_get_time() - wifi_started_us < 10000000) return;
    if (done && status == 0) {
        uint16_t found = 0, count = 8;
        wifi_ap_record_t records[8];
        esp_err_t err = esp_wifi_scan_get_ap_num(&found);
        if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&count, records);
        print_result("WIFI_SCAN", err);
        printf("[WIFI] AP_count=%u (zero APs alone does not prove an RF fault)\n", found);
        if (err == ESP_OK) for (int i = 0; i < count; i++)
            printf("[WIFI] AP%d channel=%u rssi=%d dBm\n", i + 1, records[i].primary, records[i].rssi);
    } else {
        printf("[WIFI] FAIL status=%" PRIu32 " timeout=%d\n", status, !done);
        esp_wifi_scan_stop();
    }
    esp_wifi_clear_ap_list();
    esp_wifi_stop();
    wifi_scanning = false;
}

static void help(void)
{
    printf("[HELP] help | status | selftest | led on | led off | led test | wifi | arm | pulse [20..1000] | off | poweroff | reboot\n");
    printf("[HELP] Serial arm lasts 30s and permits ONE pulse; default pulse 100ms, cooldown 10s.\n");
    printf("[HELP] Buttons: K3=100ms K4=500ms BOOT=1000ms, no arm needed; release all then press ONE key; no repeat/queue.\n");
    printf("[HELP] K5: release once after boot, then hold >=3s to power off. No startup pulses.\n");
}

static void request_pulse(uint32_t ms, const char *source)
{
    armed_until_us = 0;
    battery = read_adc(0); /* Both input paths use a fresh calibrated sample. */
    sk_lock_status_t s = sk_lock_status();
    if (!s.enabled || s.fault || s.active || s.cooldown_ms || !battery_ok() || wifi_scanning || shutdown_requested) {
        printf("[LOCK] PULSE REJECTED source=%s ms=%" PRIu32 " enabled=%d fault=%d active=%d cooldown_ms=%" PRIu32
               " battery_ok=%d wifi=%d shutdown=%d; request discarded\n", source, ms,
               s.enabled, s.fault, s.active, s.cooldown_ms, battery_ok(), wifi_scanning, shutdown_requested);
        return;
    }
    esp_err_t err = sk_lock_pulse(ms);
    if (err == ESP_OK) {
        pulse_before_mv = sk_battery_mv(battery.mv);
        pulse_min_mv = -1;
    }
    printf("[LOCK] source=%s pulse_ms=%" PRIu32 " result=%s; physical unlock unverified\n", source, ms, esp_err_to_name(err));
}

static void command(char *line)
{
    char *save, *cmd = strtok_r(line, " \t", &save);
    if (!cmd) return;
    char *arg = strtok_r(NULL, " \t", &save);
    if (strtok_r(NULL, " \t", &save)) { printf("[CMD] ERROR too many arguments\n"); return; }
    if (!strcmp(cmd, "off") && !arg) {
        sk_lock_off(); armed_until_us = 0; printf("[LOCK] OFF requested; arming cancelled\n"); return;
    }
    if (shutdown_requested) { printf("[POWER] shutdown latched; power-cycle to resume\n"); return; }
    if (!strcmp(cmd, "help") && !arg) help();
    else if (!strcmp(cmd, "status") && !arg) report();
    else if (!strcmp(cmd, "selftest") && !arg) selftest();
    else if (!strcmp(cmd, "wifi") && !arg) wifi_start_scan();
    else if (!strcmp(cmd, "poweroff") && !arg) poweroff();
    else if (!strcmp(cmd, "reboot") && !arg) {
        sk_lock_off(); armed_until_us = 0; printf("[SYS] reboot (lock OFF)\n"); esp_restart();
    } else if (!strcmp(cmd, "led") && arg) {
        if (!strcmp(arg, "test")) { led_steps = 6; led_next_us = 0; led_level = 0; }
        else if (!strcmp(arg, "on") || !strcmp(arg, "off")) {
            led_steps = 0; led_level = !strcmp(arg, "on"); gpio_set_level(SK_LED_GPIO, led_level);
        } else { printf("[LED] ERROR use on/off/test\n"); return; }
        printf("[LED] command accepted; visually verify D3\n");
    } else if (!strcmp(cmd, "arm") && !arg) {
        battery = read_adc(0); /* Commands before this one may have taken time. */
        sk_lock_status_t s = sk_lock_status();
        if (!s.enabled || s.fault || s.active || s.cooldown_ms || !battery_ok() || wifi_scanning) {
            armed_until_us = 0;
            printf("[LOCK] ARM REJECTED enabled=%d fault=%d active=%d cooldown=%" PRIu32 " battery_ok=%d wifi=%d\n",
                   s.enabled, s.fault, s.active, s.cooldown_ms, battery_ok(), wifi_scanning);
        } else {
            armed_until_us = esp_timer_get_time() + SK_ARM_WINDOW_US;
            printf("[LOCK] ARMED for one pulse within 30s\n");
        }
    } else if (!strcmp(cmd, "pulse")) {
        uint32_t ms = SK_DEFAULT_PULSE_MS;
        bool valid = !arg || sk_parse_duration(arg, &ms);
        bool armed = armed_until_us > esp_timer_get_time();
        armed_until_us = 0; /* Even a rejected request consumes the arm. */
        if (!valid || !armed) {
            printf("[LOCK] PULSE REJECTED duration_valid=%d armed=%d\n", valid, armed);
            return;
        }
        request_pulse(ms, "SERIAL");
    } else printf("[CMD] ERROR unknown command or arguments; type help\n");
}

static void console_poll(void)
{
    static char line[96];
    static size_t used;
    static bool overflow;
    char input[64];
    int n = read(STDIN_FILENO, input, sizeof(input));
    for (int i = 0; i < n; i++) {
        const char c = input[i];
        if (c == '\r' || c == '\n') {
            if (overflow) printf("[CMD] ERROR line too long, whole line discarded\n");
            else { line[used] = 0; command(line); }
            used = 0; overflow = false;
        } else if (c == '\b' || c == 127) {
            if (used && !overflow) used--;
        } else if (c >= 32 && c <= 126) {
            if (used + 1 < sizeof(line) && !overflow) line[used++] = c;
            else overflow = true;
        } else if (c != '\t') {
            /* Reject control/binary bytes, never execute a truncated prefix. */
            overflow = true;
        } else {
            if (used + 1 < sizeof(line) && !overflow) line[used++] = c;
            else overflow = true;
        }
    }
}

static void poll_keys(int64_t now)
{
    uint8_t pressed_mask = 0;
    for (int i = 0; i < 4; i++) {
        bool pressed;
        if (i < 3) pressed = gpio_get_level(key_pins[i]) == 0;
        else {
            if (!power_key.valid) { power_pressed_us = 0; continue; }
            power_key_hysteresis = sk_power_key_pressed(power_key_hysteresis, power_key.mv);
            pressed = power_key_hysteresis;
        }
        if (i < 3 && pressed) pressed_mask |= 1U << i;
        if (sk_debounce_update(&keys[i], pressed, now)) {
            if (keys[i].stable) key_presses[i]++;
            printf("[KEY_EVENT] %s %s count=%" PRIu32 "\n", key_names[i],
                   keys[i].stable ? "PRESSED" : "RELEASED", key_presses[i]);
        }
    }
    if (power_key.valid && !keys[3].stable && !power_key_hysteresis) {
        power_key_released = true;
        power_pressed_us = 0;
    }
    if (power_key.valid && keys[3].stable && power_key_released) {
        if (!power_pressed_us) power_pressed_us = now;
        if (now - power_pressed_us >= 3000000) poweroff();
    }
    uint32_t ms = sk_button_pulse_update(&test_buttons, pressed_mask, now);
    if (ms && !shutdown_requested) {
        const char *source = pressed_mask == 1 ? key_names[0] : pressed_mask == 2 ? key_names[1] : key_names[2];
        request_pulse(ms, source);
    }
}

void app_main(void)
{
    /* Keep hazardous outputs quiet before lengthy diagnostics. */
    ESP_ERROR_CHECK(sk_lock_init());
    power_hold_init();
    inputs_led_init();
    setvbuf(stdout, NULL, _IONBF, 0);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
    ESP_ERROR_CHECK(esp_register_shutdown_handler(sk_lock_off));
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    adc_init();
    temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    esp_err_t err = temperature_sensor_install(&temp_cfg, &temperature);
    if (err == ESP_OK) err = temperature_sensor_enable(temperature);
    if (err != ESP_OK) temperature = NULL;
    print_result("CHIP_TEMPERATURE_INIT", err);
    err = nvs_flash_init();
    nvs_ready = err == ESP_OK;
    print_result("NVS_INIT", err);
    selftest();
    help();
    int64_t next_report = 0;
    for (;;) {
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        int64_t now = esp_timer_get_time();
        if (!shutdown_requested) {
            battery = read_adc(0);
            power_key = read_adc(1);
            sk_lock_status_t s = sk_lock_status();
            if (s.active) {
                if (battery.valid && (pulse_min_mv < 0 || sk_battery_mv(battery.mv) < pulse_min_mv))
                    pulse_min_mv = sk_battery_mv(battery.mv);
                if (!battery_ok()) {
                    sk_lock_off(); armed_until_us = 0;
                    printf("[LOCK] ABORT: invalid/low/high battery\n");
                }
            }
            if (!s.active && last_pulses != s.pulses) {
                printf("[LOCK_RESULT] commanded OFF pre_mv=%d sampled_on_min_mv=%d after_mv=%d (min=-1 means no sample)\n",
                       pulse_before_mv, pulse_min_mv, sk_battery_mv(battery.mv));
                last_pulses = s.pulses;
            }
            if (s.cutoffs != last_cutoffs) {
                printf("[LOCK] timer cutoff count=%" PRIu32 "\n", s.cutoffs);
                last_cutoffs = s.cutoffs;
            }
            poll_keys(now);
            if (led_steps && now >= led_next_us) {
                led_level = !led_level;
                gpio_set_level(SK_LED_GPIO, led_level);
                led_steps--;
                led_next_us = now + 200000;
            }
        }
        wifi_poll();
        console_poll();
        if (now >= next_report) { report(); next_report = now + 1000000; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
