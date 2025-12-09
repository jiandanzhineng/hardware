#include "base_device.h"
#include "device_common.h"
#include "esp_log.h"
#include "iot_button.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include <rom/ets_sys.h>
#include <limits.h>
#include "esp_sleep.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_wifi.h"

static const char *TAG = "DZC01";

// Keys (low level active)
#define KEY1_GPIO 3
#define KEY2_GPIO 2
#define KEY3_GPIO 19
#define KEY4_GPIO 18

// HX711 pins (A channel, gain=128)
#define HX_SCK_GPIO 0
#define HX_DT_GPIO  1

// OLED I2C pins (ESP32-C3 hardware I2C0)
#define OLED_SCL_GPIO 6
#define OLED_SDA_GPIO 7
#define OLED_I2C_PORT I2C_NUM_0
#define OLED_ADDR     0x3C

// Properties
device_property_t weight_property;
device_property_t report_delay_ms_property;
device_property_t display_mode_property;
device_property_t display_on_property;
device_property_t display_contrast_property;
device_property_t line1_text_property;
device_property_t line2_text_property;
device_property_t button2_property;

extern device_property_t device_type_property;
extern device_property_t sleep_time_property;
extern device_property_t battery_property;

device_property_t *device_properties[] = {
    &device_type_property,
    &sleep_time_property,
    &battery_property,
    &weight_property,
    &report_delay_ms_property,
    &display_mode_property,
    &display_on_property,
    &display_contrast_property,
    &line1_text_property,
    &line2_text_property,
    &button2_property,
};
int device_properties_num = sizeof(device_properties) / sizeof(device_properties[0]);

extern void get_property(char *property_name, int msg_id);
extern void mqtt_publish(cJSON *root);

// ---------------- HX711 simple driver ----------------
static float hx_offset = 352703.0f;     // default from mpy demo
static float hx_calval = 249201.5f;     // default from mpy demo (100g)
static float hx_known_weight = 500.0f;  // grams for calval
static inline void hx_delay_short(void) { ets_delay_us(5); }

static void hx_init(void) {
    gpio_reset_pin(HX_SCK_GPIO);
    gpio_reset_pin(HX_DT_GPIO);
    gpio_set_direction(HX_SCK_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_direction(HX_DT_GPIO, GPIO_MODE_INPUT);
    gpio_pullup_en(HX_DT_GPIO);
    gpio_set_level(HX_SCK_GPIO, 0);
}

static inline int hx_is_ready(void) { return gpio_get_level(HX_DT_GPIO) == 0; }

static int32_t hx_read_raw24(void) {
    while (!hx_is_ready()) { hx_delay_short(); }
    uint32_t data = 0;
    gpio_set_level(HX_SCK_GPIO, 0); hx_delay_short();
    for (int i = 0; i < 24; i++) {
        gpio_set_level(HX_SCK_GPIO, 1); hx_delay_short();
        gpio_set_level(HX_SCK_GPIO, 0); hx_delay_short();
        data = (data << 1) | gpio_get_level(HX_DT_GPIO);
    }
    gpio_set_level(HX_SCK_GPIO, 1); hx_delay_short();
    gpio_set_level(HX_SCK_GPIO, 0); hx_delay_short();
    // convert two's complement 24-bit
    if (data & 0x800000) {
        data -= 0x1000000;
    }
    return (int32_t)data;
}

static float hx_read_filtered(void) {
    // sliding reject max/min over 10 samples
    int32_t maxv = INT32_MIN;
    int32_t minv = INT32_MAX;
    int64_t sum = 0;
    for (int i = 0; i < 5; i++) {
        int32_t v = hx_read_raw24();
        if (v > maxv) maxv = v;
        if (v < minv) minv = v;
        sum += v;
    }
    sum -= maxv; sum -= minv;
    float avg = (float)sum / 3.0f;
    ESP_LOGI(TAG, "HX711 raw: %d, filtered: %.2f", (int)avg, avg);
    return avg;
}

static float read_weight_grams(void) {
    float raw = hx_read_filtered();
    float w = ((raw - hx_offset) * hx_known_weight) / (hx_calval - hx_offset);
    if (w < 0) w = 0; 
    if (w > 5000.0f) w = 5000.0f;
    return w;
}

// ---------- NVS: save/load tare & calibration ----------
static void nvs_dzc01_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

static void nvs_dzc01_read(void) {
    nvs_handle_t h;
    if (nvs_open("dzc01_storage", NVS_READWRITE, &h) == ESP_OK) {
        size_t sz = sizeof(float);
        float v;
        if (nvs_get_blob(h, "hx_offset", &v, &sz) == ESP_OK) {
            hx_offset = v;
            ESP_LOGI(TAG, "Loaded tare offset: %.2f", hx_offset);
        }
        sz = sizeof(float);
        if (nvs_get_blob(h, "hx_calval", &v, &sz) == ESP_OK) {
            hx_calval = v;
            ESP_LOGI(TAG, "Loaded calibration value: %.2f", hx_calval);
        }
        sz = sizeof(float);
        if (nvs_get_blob(h, "hx_known", &v, &sz) == ESP_OK) {
            hx_known_weight = v;
            ESP_LOGI(TAG, "Loaded known weight: %.2f", hx_known_weight);
        }
        nvs_close(h);
    }
}

static void nvs_dzc01_save(void) {
    nvs_handle_t h;
    if (nvs_open("dzc01_storage", NVS_READWRITE, &h) == ESP_OK) {
        ESP_LOGI(TAG, "Saving tare and calibration...");
        nvs_set_blob(h, "hx_offset", &hx_offset, sizeof(float));
        ESP_LOGI(TAG, "Tare offset: %.2f", hx_offset);
        nvs_set_blob(h, "hx_calval", &hx_calval, sizeof(float));
        ESP_LOGI(TAG, "Calibration value: %.2f", hx_calval);
        nvs_set_blob(h, "hx_known", &hx_known_weight, sizeof(float));
        ESP_LOGI(TAG, "Known weight: %.2f", hx_known_weight);
        nvs_commit(h);
        nvs_close(h);
    }
}

static void hx_tare(void) { 
    ESP_LOGI(TAG, "Taring...");
    hx_offset = hx_read_filtered();
    nvs_dzc01_save(); 

    }
static void hx_calibrate_500g(void) { 
    ESP_LOGI(TAG, "Calibrating 500g...");
    hx_calval = hx_read_filtered(); 
    hx_known_weight = 500.0f; 
    nvs_dzc01_save(); 
}

// ---------------- SSD1306 minimal display ----------------
static bool oled_ready = false;
static bool net_ready = false;
static int notify_ms_left = 0;
static char notify_text[16] = "";

static bool wifi_connected(void) {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

static esp_err_t oled_write_cmd(uint8_t cmd) {
    uint8_t buf[2] = {0x80, cmd};
    return i2c_master_write_to_device(OLED_I2C_PORT, OLED_ADDR, buf, 2, 1000 / portTICK_PERIOD_MS);
}

static esp_err_t oled_write_data(const uint8_t *data, size_t len) {
    if (!data || len == 0) return ESP_OK;
    if (len > 128) len = 128;
    uint8_t buf[129];
    buf[0] = 0x40;
    memcpy(&buf[1], data, len);
    return i2c_master_write_to_device(OLED_I2C_PORT, OLED_ADDR, buf, len + 1, 1000 / portTICK_PERIOD_MS);
}

static void oled_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_SDA_GPIO,
        .scl_io_num = OLED_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    i2c_param_config(OLED_I2C_PORT, &conf);
    i2c_driver_install(OLED_I2C_PORT, conf.mode, 0, 0, 0);
    // init sequence (aligned with mpy driver)
    uint8_t init_cmds[] = {
        0xAE, // display off
        0x20, 0x00, // memory mode: horizontal
        0x40, // start line
        0xA1, // seg remap
        0xA8, 31, // mux ratio for 32 px
        0xC8, // com scan dir
        0xD3, 0x00, // display offset
        0xDA, 0x02, // com pins config for 32px
        0xD5, 0x80, // clock div
        0xD9, 0xF1, // precharge
        0xDB, 0x30, // vcomh
        0x81, 0xFF, // contrast
        0xA4, // display follows RAM
        0xA6, // normal display
        0x8D, 0x14, // charge pump on
        0xAF  // display on
    };
    for (size_t i = 0; i < sizeof(init_cmds); i++) oled_write_cmd(init_cmds[i]);
    oled_ready = true;
}

static void oled_power(bool on) { oled_write_cmd(on ? 0xAF : 0xAE); }
static void oled_contrast(uint8_t c) { oled_write_cmd(0x81); oled_write_cmd(c); }

static void oled_clear(void) {
    if (!oled_ready) return;
    // set full area
    oled_write_cmd(0x21); oled_write_cmd(0); oled_write_cmd(127);
    oled_write_cmd(0x22); oled_write_cmd(0); oled_write_cmd(3);
    // write zeros page by page
    uint8_t line[128]; memset(line, 0, sizeof(line));
    for (int page = 0; page < 4; page++) {
        oled_write_cmd(0xB0 | page);
        oled_write_data(line, sizeof(line));
    }
}

// 8x8 ASCII font (subset), unknown as space
static const uint8_t font8x8_basic[128][8] = {
    [32] = {0,0,0,0,0,0,0,0}, // space
    [45] = {0,0,0,0,0x7E,0,0,0}, // -
    [46] = {0,0,0,0,0,0,0,0x18}, // .
    [48] = {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0}, // 0
    [49] = {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0}, // 1
    [50] = {0x3C,0x66,0x06,0x1C,0x30,0x66,0x7E,0}, // 2
    [51] = {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0}, // 3
    [52] = {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0}, // 4
    [53] = {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0}, // 5
    [54] = {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0}, // 6
    [55] = {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0}, // 7
    [56] = {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0}, // 8
    [57] = {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0}, // 9
    [65] = {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0}, // A
    [66] = {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0}, // B
    [67] = {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0}, // C
    [68] = {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0}, // D
    [69] = {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0}, // E
    [70] = {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0}, // F
    [71] = {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0}, // G
    [72] = {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0}, // H
    [73] = {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0}, // I
    [74] = {0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0}, // J
    [75] = {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0}, // K
    [76] = {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0}, // L
    [77] = {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0}, // M
    [78] = {0x66,0x76,0x7E,0x6E,0x66,0x66,0x66,0}, // N
    [79] = {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0}, // O
    [80] = {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0}, // P
    [81] = {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0}, // Q
    [82] = {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0}, // R
    [83] = {0x3C,0x66,0x30,0x18,0x0C,0x66,0x3C,0}, // S
    [84] = {0x7E,0x5A,0x18,0x18,0x18,0x18,0x3C,0}, // T
    [85] = {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0}, // U
    [86] = {0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0}, // V
    [87] = {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0}, // W
    [88] = {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0}, // X
    [89] = {0x66,0x66,0x3C,0x18,0x18,0x18,0x3C,0}, // Y
    [90] = {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0}, // Z
    [97] = {0,0,0x3C,0x06,0x3E,0x66,0x3E,0}, // a
    [98] = {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0}, // b
    [99] = {0,0,0x3C,0x60,0x60,0x60,0x3C,0}, // c
    [100]= {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0}, // d
    [101]= {0,0,0x3C,0x66,0x7E,0x60,0x3C,0}, // e
    [102]= {0x0E,0x18,0x18,0x7C,0x18,0x18,0x18,0}, // f
    [103]= {0,0,0x3E,0x66,0x3E,0x06,0x3C,0}, // g
    [104]= {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0}, // h
    [105]= {0x18,0,0x38,0x18,0x18,0x18,0x3C,0}, // i
    [106]= {0x0C,0,0x1C,0x0C,0x0C,0x6C,0x38,0}, // j
    [107]= {0x60,0x60,0x6C,0x78,0x70,0x78,0x6C,0}, // k
    [108]= {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0}, // l
    [109]= {0,0,0x6C,0x7E,0x7E,0x6C,0x6C,0}, // m
    [110]= {0,0,0x7C,0x66,0x66,0x66,0x66,0}, // n
    [111]= {0,0,0x3C,0x66,0x66,0x66,0x3C,0}, // o
    [112]= {0,0,0x7C,0x66,0x66,0x7C,0x60,0x60}, // p
    [113]= {0,0,0x3E,0x66,0x66,0x3E,0x06,0x06}, // q
    [114]= {0,0,0x7C,0x66,0x60,0x60,0x60,0}, // r
    [115]= {0,0,0x3C,0x60,0x3C,0x06,0x3C,0}, // s
    [116]= {0x18,0x18,0x7E,0x18,0x18,0x18,0x0E,0}, // t
    [117]= {0,0,0x66,0x66,0x66,0x66,0x3E,0}, // u
    [118]= {0,0,0x66,0x66,0x66,0x3C,0x18,0}, // v
    [119]= {0,0,0x63,0x6B,0x7F,0x77,0x63,0}, // w
    [120]= {0,0,0x66,0x3C,0x18,0x3C,0x66,0}, // x
    [121]= {0,0,0x66,0x66,0x3E,0x06,0x3C,0}, // y
    [122]= {0,0,0x7E,0x0C,0x18,0x30,0x7E,0}, // z
};

static void oled_draw_char(int col, int page, char ch) {
    if (!oled_ready) return;
    uint8_t c = (uint8_t)ch;
    const uint8_t *g = font8x8_basic[c];
    // rotate 90° CW: rows -> columns
    uint8_t colbuf[8];
    for (int x = 0; x < 8; x++) {
        uint8_t b = 0;
        for (int y = 0; y < 8; y++) {
            if ((g[y] >> (7 - x)) & 0x01) b |= (1 << y);
        }
        colbuf[x] = b;
    }
    // set column and page
    oled_write_cmd(0x21); oled_write_cmd(col); oled_write_cmd(col + 7);
    oled_write_cmd(0x22); oled_write_cmd(page); oled_write_cmd(page);
    oled_write_data(colbuf, 8);
}

static void oled_draw_char_x2(int col, int page, char ch) {
    if (!oled_ready) return;
    uint8_t c = (uint8_t)ch;
    const uint8_t *g = font8x8_basic[c];
    uint8_t top[16];
    uint8_t bottom[16];
    for (int x = 0; x < 8; x++) {
        uint16_t u = 0;
        for (int y = 0; y < 8; y++) {
            if ((g[y] >> (7 - x)) & 0x01) {
                u |= (1 << (2 * y));
                u |= (1 << (2 * y + 1));
            }
        }
        top[2 * x] = (uint8_t)(u & 0xFF);
        top[2 * x + 1] = (uint8_t)(u & 0xFF);
        bottom[2 * x] = (uint8_t)(u >> 8);
        bottom[2 * x + 1] = (uint8_t)(u >> 8);
    }
    oled_write_cmd(0x21); oled_write_cmd(col); oled_write_cmd(col + 15);
    oled_write_cmd(0x22); oled_write_cmd(page); oled_write_cmd(page);
    oled_write_data(top, 16);
    oled_write_cmd(0x22); oled_write_cmd(page + 1); oled_write_cmd(page + 1);
    oled_write_data(bottom, 16);
}

static void oled_draw_text_center_x2(int page, const char *text) {
    if (!oled_ready || !text) return;
    int len = 0; while (text[len] && len < 8) len++;
    int col = (128 - len * 16) / 2; if (col < 0) col = 0;
    for (int i = 0; i < len; i++) {
        oled_draw_char_x2(col, page, text[i]);
        col += 16;
    }
}

static void oled_draw_text_x2_at(int page, int col, const char *text) {
    if (!oled_ready || !text) return;
    int len = 0; while (text[len] && len < 8) len++;
    for (int i = 0; i < len; i++) {
        oled_draw_char_x2(col, page, text[i]);
        col += 16;
        if (col > 112) break;
    }
}

static void oled_draw_text_line_at(int page, int col, const char *text) {
    if (!oled_ready || !text) return;
    int c = col;
    for (int i = 0; text[i] && i < 16; i++) {
        if (c > 120) break;
        oled_draw_char(c, page, text[i]);
        c += 8;
    }
}

static void oled_draw_text_line(int line, const char *text) {
    if (!oled_ready || !text) return;
    int page = line; if (page < 0) page = 0; if (page > 3) page = 3;
    int col = 0;
    for (int i = 0; text[i] && i < 16; i++) {
        oled_draw_char(col, page, text[i]);
        col += 8;
    }
}

static void oled_show_lines(const char *l1, const char *l2) {
    oled_clear();
    oled_draw_text_line(0, l1);
    oled_draw_text_line(1, l2);
}

// Periodic tasks: weight (200ms), display (200ms), report (sliced wait)
static void weight_task(void *arg) {
    while (1) {
        float w = read_weight_grams();
        if (w < 0) w = 0;
        if (w > 5000.0f) w = 5000.0f;
        int iw = (int)(w + 0.5f);
        device_update_property_int("weight", iw);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void display_task(void *arg) {
    while (1) {
        if (display_on_property.value.int_value) {
            char net[16];
            if (wifi_connected()) {
                if (net_ready) strcpy(net, "NET OK"); else strcpy(net, "WIFI OK");
            } else {
                strcpy(net, "WIFI NO");
            }
            if (notify_ms_left > 0) { notify_ms_left -= 200; if (notify_ms_left < 0) notify_ms_left = 0; }
            oled_clear();
            if (display_mode_property.value.int_value == 1) {
                char big[32];
                int w = weight_property.value.int_value;
                snprintf(big, sizeof(big), "%d g", w);
                oled_draw_text_x2_at(1, 0, big);
                oled_draw_text_line_at(0, 64, net);
                if (notify_ms_left > 0) oled_draw_text_line_at(1, 64, notify_text);
            } else if (display_mode_property.value.int_value == 2) {
                const char *l1 = line1_text_property.value.string_value[0] ? line1_text_property.value.string_value : "Line1";
                const char *l2 = line2_text_property.value.string_value[0] ? line2_text_property.value.string_value : "Line2";
                char big[32];
                int w = weight_property.value.int_value;
                snprintf(big, sizeof(big), "%d g", w);
                oled_draw_text_x2_at(1, 0, big);
                oled_draw_text_line_at(0, 64, l1);
                oled_draw_text_line_at(1, 64, l2);
                oled_draw_text_line_at(2, 64, net);
                if (notify_ms_left > 0) oled_draw_text_line_at(3, 64, notify_text);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void report_task(void *arg) {
    while (1) {
        int delay = report_delay_ms_property.value.int_value;
        if (delay < 100) delay = 100;
        if (delay > 5000) delay = 5000;
        int waited = 0;
        while (1) {
            int current = report_delay_ms_property.value.int_value;
            if (current < 100) current = 100;
            if (current > 5000) current = 5000;
            delay = current;
            if (waited >= delay) break;
            int remain = delay - waited;
            int slice = remain > 500 ? 500 : remain;
            vTaskDelay(pdMS_TO_TICKS(slice));
            waited += slice;
        }
        get_property("weight", 0);
    }
}

// wrappers for button callbacks
static void key3_tare_cb(void *arg, void *usr_data) { (void)arg; (void)usr_data; hx_tare(); strcpy(notify_text, "TARE OK"); notify_ms_left = 3000; }
static void key4_cal_cb(void *arg, void *usr_data) { (void)arg; (void)usr_data; hx_calibrate_500g(); strcpy(notify_text, "CALIB OK"); notify_ms_left = 3000; }


// Key2: send key_clicked action

static void key2_press_cb(void *arg, void *usr_data) {
    device_update_property_int("button2", 1);
    get_property("button2", 0);
}

static void key2_release_cb(void *arg, void *usr_data) {
    device_update_property_int("button2", 0);
    get_property("button2", 0);
}

// Key1: enter deep sleep (simple power key)
static void key1_click_cb(void *arg, void *usr_data) {
    on_device_before_sleep();
    gpio_set_direction(KEY1_GPIO, GPIO_MODE_INPUT);
    // 低电平唤醒（按键为低电平触发）
    esp_deep_sleep_enable_gpio_wakeup(1ULL << KEY1_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    esp_deep_sleep_start();
}

static void init_keys(void) {
    // KEY1
    button_config_t k1 = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,
        .short_press_time = 50,
        .gpio_button_config = {
            .gpio_num = KEY1_GPIO,
            .active_level = 0,
        },
    };
    button_handle_t b1 = iot_button_create(&k1);
    if (b1) iot_button_register_cb(b1, BUTTON_SINGLE_CLICK, key1_click_cb, NULL);

    // KEY2
    button_config_t k2 = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,
        .short_press_time = 50,
        .gpio_button_config = {
            .gpio_num = KEY2_GPIO,
            .active_level = 0,
        },
    };
    button_handle_t b2 = iot_button_create(&k2);
    if (b2) {
        iot_button_register_cb(b2, BUTTON_PRESS_DOWN, key2_press_cb, NULL);
        iot_button_register_cb(b2, BUTTON_PRESS_UP, key2_release_cb, NULL);
    }

    // KEY3: tare (clear to zero)
    button_config_t k3 = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,
        .short_press_time = 50,
        .gpio_button_config = {
            .gpio_num = KEY3_GPIO,
            .active_level = 0,
        },
    };
    button_handle_t b3 = iot_button_create(&k3);
    if (b3) iot_button_register_cb(b3, BUTTON_SINGLE_CLICK, key3_tare_cb, NULL);

    // KEY4: calibrate with 500g
    button_config_t k4 = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,
        .short_press_time = 50,
        .gpio_button_config = {
            .gpio_num = KEY4_GPIO,
            .active_level = 0,
        },
    };
    button_handle_t b4 = iot_button_create(&k4);
    if (b4) iot_button_register_cb(b4, BUTTON_SINGLE_CLICK, key4_cal_cb, NULL);
}

void on_device_init(void) {
    ESP_LOGI(TAG, "on_device_init");

    // Initialize DZC01 properties
    weight_property.readable = true;
    weight_property.writeable = false;
    strcpy(weight_property.name, "weight");
    weight_property.value_type = PROPERTY_TYPE_INT;
    weight_property.value.int_value = 0;

    report_delay_ms_property.readable = true;
    report_delay_ms_property.writeable = true;
    strcpy(report_delay_ms_property.name, "report_delay_ms");
    report_delay_ms_property.value_type = PROPERTY_TYPE_INT;
    report_delay_ms_property.value.int_value = 5000;

    display_mode_property.readable = true;
    display_mode_property.writeable = true;
    strcpy(display_mode_property.name, "display_mode");
    display_mode_property.value_type = PROPERTY_TYPE_INT;
    display_mode_property.value.int_value = 1; // 1=show weight & battery

    display_on_property.readable = true;
    display_on_property.writeable = true;
    strcpy(display_on_property.name, "display_on");
    display_on_property.value_type = PROPERTY_TYPE_INT;
    display_on_property.value.int_value = 1;

    display_contrast_property.readable = true;
    display_contrast_property.writeable = true;
    strcpy(display_contrast_property.name, "display_contrast");
    display_contrast_property.value_type = PROPERTY_TYPE_INT;
    display_contrast_property.value.int_value = 255;

    line1_text_property.readable = true;
    line1_text_property.writeable = true;
    strcpy(line1_text_property.name, "line1_text");
    line1_text_property.value_type = PROPERTY_TYPE_STRING;
    line1_text_property.value.string_value[0] = '\0';

    line2_text_property.readable = true;
    line2_text_property.writeable = true;
    strcpy(line2_text_property.name, "line2_text");
    line2_text_property.value_type = PROPERTY_TYPE_STRING;
    line2_text_property.value.string_value[0] = '\0';

    button2_property.readable = true;
    button2_property.writeable = false;
    strcpy(button2_property.name, "button2");
    button2_property.value_type = PROPERTY_TYPE_INT;
    button2_property.value.int_value = 0;

    // NVS & HX711
    nvs_dzc01_init();
    nvs_dzc01_read();
    hx_init();

    // init OLED and show boot message
    oled_init();
    oled_power(true);
    oled_contrast(display_contrast_property.value.int_value);

    init_keys();
    xTaskCreate(weight_task, "dzc01_weight_task", 4096, NULL, 10, NULL);
    xTaskCreate(display_task, "dzc01_display_task", 4096, NULL, 9, NULL);
    xTaskCreate(report_task, "dzc01_report_task", 4096, NULL, 8, NULL);
}

void on_device_first_ready(void) {
    device_update_property_string("line1_text", "Connected");
    get_property("line1_text", 0);
    net_ready = true;
}

void on_mqtt_msg_process(char *topic, cJSON *root) {
    // No additional processing
}

void on_action(cJSON *root) {
    // No custom actions
}

void on_device_before_sleep(void) {
    oled_power(false);
}

void on_set_property(char *property_name, cJSON *property_value, int msg_id) {
    // Minimal clamping for a few properties
    if (strcmp(property_name, "report_delay_ms") == 0) {
        int v = report_delay_ms_property.value.int_value;
        if (v < 100) { v = 100; }
        if (v > 5000) { v = 5000; }
        report_delay_ms_property.value.int_value = v;
    } else if (strcmp(property_name, "display_contrast") == 0) {
        int v = display_contrast_property.value.int_value;
        if (v < 0) { v = 0; }
        if (v > 255) { v = 255; }
        display_contrast_property.value.int_value = v;
        oled_contrast((uint8_t)v);
    } else if (strcmp(property_name, "display_on") == 0) {
        int v = display_on_property.value.int_value;
        if (v < 0) { v = 0; }
        if (v > 1) { v = 1; }
        display_on_property.value.int_value = v;
        oled_power(v);
    } else if (strcmp(property_name, "display_mode") == 0) {
        int v = display_mode_property.value.int_value;
        if (v < 0) { v = 0; }
        if (v > 2) { v = 2; }
        display_mode_property.value.int_value = v;
    }
}
