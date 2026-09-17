#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "base_device.h"
#include "cJSON.h"
#include "device_common.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DAN01";

#define AS201_UART          UART_NUM_1
#define AS201_TX_GPIO       GPIO_NUM_18
#define AS201_RX_GPIO       GPIO_NUM_19
#define AS201_RST_GPIO      GPIO_NUM_4
#define DAN01_KEY_GPIO      GPIO_NUM_5
#define AS201_BAUD_RATE     115200
#define AS201_RX_BUFFER     1024
#define AS201_FRAME_MAX     64
#define DAN01_TASK_PERIOD_MS 10
#define DAN01_DEBOUNCE_MS   30

device_property_t report_delay_ms_property;
device_property_t accel_property;
device_property_t gyro_property;
device_property_t mag_property;
device_property_t quat_property;
device_property_t temperature_property;
device_property_t height_property;
device_property_t button0_property;
device_property_t mag_accuracy_property;

extern device_property_t device_type_property;
extern device_property_t sleep_time_property;
extern device_property_t battery_property;

device_property_t *device_properties[] = {
    &device_type_property,
    &sleep_time_property,
    &battery_property,
    &report_delay_ms_property,
    &accel_property,
    &gyro_property,
    &mag_property,
    &quat_property,
    &temperature_property,
    &height_property,
    &button0_property,
    &mag_accuracy_property,
};
DEVICE_PROPERTIES_NUM_DEFINE(device_properties);

static uint8_t s_rx_frame[AS201_FRAME_MAX];
static size_t s_rx_len;
static bool s_has_sample;
static bool s_force_report;
static int s_last_raw_button = 1;
static int s_stable_button = 1;
static int64_t s_button_change_ms;
static int64_t s_last_report_ms;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static int16_t read_i16_le(const uint8_t *data)
{
    return (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0]
        | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16)
        | ((uint32_t)data[3] << 24);
}

static void bytes_to_hex(const uint8_t *data, size_t len, char *output)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        output[i * 2] = hex[data[i] >> 4];
        output[i * 2 + 1] = hex[data[i] & 0x0f];
    }
    output[len * 2] = '\0';
}

static void init_string_property(device_property_t *property, const char *name)
{
    property->readable = true;
    property->writeable = false;
    strlcpy(property->name, name, sizeof(property->name));
    property->value_type = PROPERTY_TYPE_STRING;
    property->value.string_value[0] = '\0';
}

static void init_properties(void)
{
    report_delay_ms_property.readable = true;
    report_delay_ms_property.writeable = true;
    strlcpy(report_delay_ms_property.name, "report_delay_ms",
            sizeof(report_delay_ms_property.name));
    report_delay_ms_property.value_type = PROPERTY_TYPE_INT;
    report_delay_ms_property.value.int_value = 200;
    report_delay_ms_property.min = 50;
    report_delay_ms_property.max = 10000;

    init_string_property(&accel_property, "accel");
    init_string_property(&gyro_property, "gyro");
    init_string_property(&mag_property, "mag");
    init_string_property(&quat_property, "quat");

    temperature_property.readable = true;
    temperature_property.writeable = false;
    strlcpy(temperature_property.name, "temperature",
            sizeof(temperature_property.name));
    temperature_property.value_type = PROPERTY_TYPE_FLOAT;
    temperature_property.value.float_value = 0.0f;

    height_property.readable = true;
    height_property.writeable = false;
    strlcpy(height_property.name, "height", sizeof(height_property.name));
    height_property.value_type = PROPERTY_TYPE_FLOAT;
    height_property.value.float_value = 0.0f;

    button0_property.readable = true;
    button0_property.writeable = false;
    strlcpy(button0_property.name, "button0", sizeof(button0_property.name));
    button0_property.value_type = PROPERTY_TYPE_INT;
    button0_property.value.int_value = 0;
    button0_property.min = 0;
    button0_property.max = 1;

    mag_accuracy_property.readable = true;
    mag_accuracy_property.writeable = false;
    strlcpy(mag_accuracy_property.name, "mag_accuracy",
            sizeof(mag_accuracy_property.name));
    mag_accuracy_property.value_type = PROPERTY_TYPE_INT;
    mag_accuracy_property.value.int_value = 0;
    mag_accuracy_property.min = 0;
    mag_accuracy_property.max = 3;
}

static void publish_key_action(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "method", "action");
    cJSON_AddStringToObject(root, "action", "key_clicked");
    device_publish_event(root);
}

static void poll_button(void)
{
    int raw = gpio_get_level(DAN01_KEY_GPIO);
    int64_t timestamp = now_ms();
    if (raw != s_last_raw_button) {
        s_last_raw_button = raw;
        s_button_change_ms = timestamp;
    }

    if (raw == s_stable_button || timestamp - s_button_change_ms < DAN01_DEBOUNCE_MS) {
        return;
    }

    s_stable_button = raw;
    button0_property.value.int_value = raw == 0 ? 1 : 0;
    device_update_property_int("button0", button0_property.value.int_value);
    s_force_report = true;
    if (raw == 0) publish_key_action();
}

static void parse_data_frame(const uint8_t *frame, size_t total)
{
    if (total < 50 || frame[3] != 0x00) return;

    const uint8_t type = frame[4];
    const uint8_t *data = frame + 5;
    bytes_to_hex(data, 6, accel_property.value.string_value);
    bytes_to_hex(data + 6, 6, gyro_property.value.string_value);
    bytes_to_hex(data + 18, 6, mag_property.value.string_value);
    bytes_to_hex(data + 24, 8, quat_property.value.string_value);
    temperature_property.value.float_value = read_i16_le(data + 32) * 0.01f;
    height_property.value.float_value = read_u32_le(data + 38) * 0.0010728836f;
    mag_accuracy_property.value.int_value = (type >> 2) & 0x03;
    s_has_sample = true;
}

static void parse_frame(const uint8_t *frame, size_t total)
{
    uint8_t checksum = 0;
    for (size_t i = 3; i < total - 3; i++) checksum += frame[i];
    if (frame[total - 2] != 0xfc || frame[total - 1] != 0xfd
        || checksum != frame[total - 3]) {
        ESP_LOGW(TAG, "Discarding invalid AS201 frame");
        return;
    }

    if (frame[3] == 0x10 && total >= 13) {
        ESP_LOGI(TAG, "AS201 version V%u.%u", frame[5], frame[4]);
        return;
    }
    parse_data_frame(frame, total);
}

static void feed_byte(uint8_t byte)
{
    if (s_rx_len == 0) {
        if (byte == 0xfa) s_rx_frame[s_rx_len++] = byte;
        return;
    }
    if (s_rx_len == 1) {
        if (byte == 0xfb) {
            s_rx_frame[s_rx_len++] = byte;
        } else if (byte != 0xfa) {
            s_rx_len = 0;
        }
        return;
    }

    if (s_rx_len >= sizeof(s_rx_frame)) {
        s_rx_len = 0;
        return;
    }
    s_rx_frame[s_rx_len++] = byte;
    if (s_rx_len < 3) return;

    size_t total = (size_t)s_rx_frame[2] + 5;
    if (total < 7 || total > sizeof(s_rx_frame)) {
        s_rx_len = 0;
        return;
    }
    if (s_rx_len == total) {
        parse_frame(s_rx_frame, total);
        s_rx_len = 0;
    }
}

static void publish_sensor_update(void)
{
    device_update_property_string("accel", accel_property.value.string_value);
    device_update_property_string("gyro", gyro_property.value.string_value);
    device_update_property_string("mag", mag_property.value.string_value);
    device_update_property_string("quat", quat_property.value.string_value);
    device_update_property_float("temperature", temperature_property.value.float_value);
    device_update_property_float("height", height_property.value.float_value);
    device_update_property_int("button0", button0_property.value.int_value);
    device_update_property_int("mag_accuracy", mag_accuracy_property.value.int_value);

    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "method", "update");
    cJSON_AddStringToObject(root, "accel", accel_property.value.string_value);
    cJSON_AddStringToObject(root, "gyro", gyro_property.value.string_value);
    cJSON_AddStringToObject(root, "mag", mag_property.value.string_value);
    cJSON_AddStringToObject(root, "quat", quat_property.value.string_value);
    cJSON_AddNumberToObject(root, "temperature", temperature_property.value.float_value);
    cJSON_AddNumberToObject(root, "height", height_property.value.float_value);
    cJSON_AddNumberToObject(root, "button0", button0_property.value.int_value);
    cJSON_AddNumberToObject(root, "mag_accuracy", mag_accuracy_property.value.int_value);
    device_publish_message(root);
}

static void dan01_task(void *arg)
{
    (void)arg;
    uint8_t bytes[128];
    while (true) {
        int count = uart_read_bytes(AS201_UART, bytes, sizeof(bytes), 0);
        for (int i = 0; i < count; i++) feed_byte(bytes[i]);
        poll_button();

        int delay_ms = report_delay_ms_property.value.int_value;
        int64_t timestamp = now_ms();
        if ((s_has_sample && timestamp - s_last_report_ms >= delay_ms) || s_force_report) {
            publish_sensor_update();
            s_last_report_ms = timestamp;
            s_force_report = false;
        }
        vTaskDelay(pdMS_TO_TICKS(DAN01_TASK_PERIOD_MS));
    }
}

static esp_err_t init_as201(void)
{
    gpio_config_t output = {
        .pin_bit_mask = 1ULL << AS201_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output), TAG, "reset gpio config failed");
    gpio_set_level(AS201_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(AS201_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(300));

    uart_config_t uart_config = {
        .baud_rate = AS201_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(uart_param_config(AS201_UART, &uart_config), TAG,
                        "AS201 uart config failed");
    ESP_RETURN_ON_ERROR(
        uart_set_pin(AS201_UART, AS201_TX_GPIO, AS201_RX_GPIO,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
        TAG, "AS201 uart pin config failed");
    ESP_RETURN_ON_ERROR(
        uart_driver_install(AS201_UART, AS201_RX_BUFFER, 0, 0, NULL, 0),
        TAG, "AS201 uart driver install failed");

    static const uint8_t enable_report[] = {
        0xfa, 0xfb, 0x03, 0x1a, 0x01, 0x1b, 0xfc, 0xfd,
    };
    static const uint8_t query_version[] = {
        0xfa, 0xfb, 0x03, 0x10, 0x01, 0x11, 0xfc, 0xfd,
    };
    uart_write_bytes(AS201_UART, enable_report, sizeof(enable_report));
    uart_write_bytes(AS201_UART, query_version, sizeof(query_version));
    return ESP_OK;
}

static void send_as201_command(uint8_t command)
{
    uint8_t frame[] = {0xfa, 0xfb, 0x02, command, command, 0xfc, 0xfd};
    int written = uart_write_bytes(AS201_UART, frame, sizeof(frame));
    if (written != (int)sizeof(frame)) {
        ESP_LOGE(TAG, "Failed to send AS201 command 0x%02x", command);
    }
}

void on_device_init(void)
{
    init_properties();
    gpio_config_t input = {
        .pin_bit_mask = 1ULL << DAN01_KEY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&input));
    s_last_raw_button = gpio_get_level(DAN01_KEY_GPIO);
    s_stable_button = s_last_raw_button;
    button0_property.value.int_value = s_stable_button == 0 ? 1 : 0;
    s_button_change_ms = now_ms();
}

esp_err_t on_device_first_ready(void)
{
    ESP_RETURN_ON_ERROR(init_as201(), TAG, "AS201 initialization failed");
    if (xTaskCreate(dan01_task, "dan01_task", 4096, NULL, 10, NULL) != pdPASS) {
        uart_driver_delete(AS201_UART);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "DAN01 sensor task started");
    return ESP_OK;
}

void on_set_property(char *property_name, cJSON *property_value, int msg_id)
{
    (void)property_value;
    (void)msg_id;
    if (strcmp(property_name, "report_delay_ms") == 0) {
        int value = report_delay_ms_property.value.int_value;
        if (value < 50) value = 50;
        if (value > 10000) value = 10000;
        report_delay_ms_property.value.int_value = value;
        device_update_property_int("report_delay_ms", value);
    }
}

void on_action(cJSON *root)
{
    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action)) return;
    if (strcmp(action->valuestring, "mag_cal_start") == 0) {
        send_as201_command(0x1d);
    } else if (strcmp(action->valuestring, "mag_cal_end") == 0) {
        send_as201_command(0x1e);
    }
}

void on_mqtt_msg_process(char *topic, cJSON *root)
{
    (void)topic;
    (void)root;
}

void on_device_before_sleep(void)
{
    gpio_set_level(AS201_RST_GPIO, 0);
}
