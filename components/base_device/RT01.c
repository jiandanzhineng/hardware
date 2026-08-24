#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "esp_log.h"
#include "iot_button.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "device_common.h"
#include "base_device.h"
#include "cJSON.h"

static const char *TAG = "RT01";

#define GPIO_KEY1        GPIO_NUM_2
#define GPIO_KEY2        GPIO_NUM_3
#define GPIO_YV          GPIO_NUM_4
#define GPIO_PUM         GPIO_NUM_5
#define GPIO_SCL         GPIO_NUM_6
#define GPIO_SDA         GPIO_NUM_7
#define GPIO_KEY3        GPIO_NUM_8
#define GPIO_INA         GPIO_NUM_18
#define GPIO_INB         GPIO_NUM_19

#define WFSENSOR_I2C_ADDR     0x6D
#define CMD_GROUP_CONVERT     0x0A
#define I2C_MASTER_NUM        I2C_NUM_0
#define I2C_MASTER_FREQ_HZ    100000

#define MOTOR_TIMER           LEDC_TIMER_0
#define MOTOR_MODE            LEDC_LOW_SPEED_MODE
#define MOTOR_CH_A            LEDC_CHANNEL_0
#define MOTOR_CH_B            LEDC_CHANNEL_1
#define MOTOR_DUTY_RES        LEDC_TIMER_13_BIT
#define MOTOR_FREQ_HZ         5000
#define MOTOR_DUTY_MAX        8191
#define SINE_PERIOD_S         10.0f

device_property_t pressure_property;
device_property_t target_pressure_property;
device_property_t min_pressure_property;
device_property_t deflate_property;
device_property_t motor_property;
extern device_property_t device_type_property;
extern device_property_t sleep_time_property;
extern device_property_t battery_property;

device_property_t *device_properties[] = {
    &device_type_property,
    &sleep_time_property,
    &battery_property,
    &pressure_property,
    &target_pressure_property,
    &min_pressure_property,
    &deflate_property,
    &motor_property,
};
DEVICE_PROPERTIES_NUM_DEFINE(device_properties);

static volatile bool s_suck_hold = false;
static volatile bool s_sine_mode = false;
static volatile int s_motor_cycle = 0;
static int64_t s_sine_start_us = 0;
static float s_pressure = 0.0f;
static uint8_t s_sensor_data[5];

static void init_properties(void);
static void init_gpio(void);
static void init_pwm(void);
static void init_buttons(void);
static void apply_air(void);
static void apply_motor_output(int speed);
static void apply_motor(void);
static void motor_cycle_next(void);
static void clamp_pressures(void);
static void publish_key_clicked(void);
static esp_err_t i2c_master_init(void);
static void pressure_sensor_init(void);
static void read_pressure_data(void);
static void control_task(void *arg);

static void init_properties(void)
{
    pressure_property.readable = true;
    pressure_property.writeable = false;
    strcpy(pressure_property.name, "pressure");
    pressure_property.value_type = PROPERTY_TYPE_FLOAT;
    pressure_property.value.float_value = 0;

    target_pressure_property.readable = true;
    target_pressure_property.writeable = true;
    strcpy(target_pressure_property.name, "target_pressure");
    target_pressure_property.value_type = PROPERTY_TYPE_FLOAT;
    target_pressure_property.value.float_value = 0;

    min_pressure_property.readable = true;
    min_pressure_property.writeable = true;
    strcpy(min_pressure_property.name, "min_pressure");
    min_pressure_property.value_type = PROPERTY_TYPE_FLOAT;
    min_pressure_property.value.float_value = -40;

    deflate_property.readable = true;
    deflate_property.writeable = true;
    strcpy(deflate_property.name, "deflate");
    deflate_property.value_type = PROPERTY_TYPE_INT;
    deflate_property.value.int_value = 0;
    deflate_property.min = 0;
    deflate_property.max = 1;

    motor_property.readable = true;
    motor_property.writeable = true;
    strcpy(motor_property.name, "motor");
    motor_property.value_type = PROPERTY_TYPE_INT;
    motor_property.value.int_value = 0;
    motor_property.min = -100;
    motor_property.max = 100;
}

static void init_gpio(void)
{
    gpio_config_t out_conf = {
        .pin_bit_mask = (1ULL << GPIO_YV) | (1ULL << GPIO_PUM),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_conf);
    gpio_set_level(GPIO_YV, 0);
    gpio_set_level(GPIO_PUM, 0);
}

static void init_pwm(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = MOTOR_MODE,
        .duty_resolution = MOTOR_DUTY_RES,
        .timer_num = MOTOR_TIMER,
        .freq_hz = MOTOR_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .speed_mode = MOTOR_MODE,
        .timer_sel = MOTOR_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .duty = 0,
        .hpoint = 0,
    };
    ch.channel = MOTOR_CH_A;
    ch.gpio_num = GPIO_INA;
    ledc_channel_config(&ch);
    ch.channel = MOTOR_CH_B;
    ch.gpio_num = GPIO_INB;
    ledc_channel_config(&ch);
}

static void clamp_pressures(void)
{
    float min_p = min_pressure_property.value.float_value;
    if (min_p < -40.0f) min_p = -40.0f;
    if (min_p > 0.0f) min_p = 0.0f;
    min_pressure_property.value.float_value = min_p;

    float target = target_pressure_property.value.float_value;
    if (target < min_p) target = min_p;
    if (target > 40.0f) target = 40.0f;
    target_pressure_property.value.float_value = target;
}

static void apply_air(void)
{
    bool deflate = deflate_property.value.int_value != 0;
    bool pump = false;
    if (!deflate && s_pressure > min_pressure_property.value.float_value) {
        if (s_suck_hold || s_pressure > target_pressure_property.value.float_value) {
            pump = true;
        }
    }
    gpio_set_level(GPIO_YV, deflate ? 1 : 0);
    gpio_set_level(GPIO_PUM, pump ? 1 : 0);
}

static void apply_motor_output(int speed)
{
    if (speed > 100) speed = 100;
    if (speed < -100) speed = -100;
    uint32_t duty = (uint32_t)(abs(speed) * MOTOR_DUTY_MAX / 100);
    uint32_t duty_a = 0;
    uint32_t duty_b = 0;
    if (speed > 0) duty_a = duty;
    else if (speed < 0) duty_b = duty;
    ledc_set_duty(MOTOR_MODE, MOTOR_CH_A, duty_a);
    ledc_update_duty(MOTOR_MODE, MOTOR_CH_A);
    ledc_set_duty(MOTOR_MODE, MOTOR_CH_B, duty_b);
    ledc_update_duty(MOTOR_MODE, MOTOR_CH_B);
}

static void apply_motor(void)
{
    if (s_sine_mode) {
        float t = (esp_timer_get_time() - s_sine_start_us) / 1000000.0f;
        float phase = 2.0f * (float)M_PI * t / SINE_PERIOD_S;
        int amp = abs(motor_property.value.int_value);
        apply_motor_output((int)lroundf((float)amp * sinf(phase)));
    } else {
        apply_motor_output(motor_property.value.int_value);
    }
}

static int motor_abs_or_max(void)
{
    int amp = abs(motor_property.value.int_value);
    return amp == 0 ? 100 : amp;
}

static void motor_cycle_next(void)
{
    if (s_motor_cycle == 0) {
        s_motor_cycle = 1;
        s_sine_mode = false;
        motor_property.value.int_value = motor_abs_or_max();
    } else if (s_motor_cycle == 1) {
        s_motor_cycle = 2;
        s_sine_mode = false;
        motor_property.value.int_value = -motor_abs_or_max();
    } else if (s_motor_cycle == 2) {
        s_motor_cycle = 3;
        s_sine_mode = true;
        s_sine_start_us = esp_timer_get_time();
        motor_property.value.int_value = motor_abs_or_max();
    } else {
        s_motor_cycle = 0;
        s_sine_mode = false;
        motor_property.value.int_value = 0;
    }
    device_update_property_int("motor", motor_property.value.int_value);
    apply_motor();
}

static void motor_from_remote(int speed)
{
    if (speed > 100) speed = 100;
    if (speed < -100) speed = -100;
    s_sine_mode = false;
    motor_property.value.int_value = speed;
    if (speed > 0) s_motor_cycle = 1;
    else if (speed < 0) s_motor_cycle = 2;
    else s_motor_cycle = 0;
    apply_motor();
}

static void publish_key_clicked(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    cJSON_AddStringToObject(root, "method", "action");
    cJSON_AddStringToObject(root, "action", "key_clicked");
    device_publish_event(root);
}

static void key1_down(void *arg, void *usr)
{
    (void)arg; (void)usr;
    s_suck_hold = true;
    apply_air();
    publish_key_clicked();
}

static void key1_up(void *arg, void *usr)
{
    (void)arg; (void)usr;
    s_suck_hold = false;
    apply_air();
}

static void key2_down(void *arg, void *usr)
{
    (void)arg; (void)usr;
    device_update_property_int("deflate", 1);
    apply_air();
    publish_key_clicked();
}

static void key2_up(void *arg, void *usr)
{
    (void)arg; (void)usr;
    device_update_property_int("deflate", 0);
    apply_air();
}

static void key3_click(void *arg, void *usr)
{
    (void)arg; (void)usr;
    motor_cycle_next();
    publish_key_clicked();
}

static button_handle_t make_button(int gpio)
{
    button_config_t cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = gpio,
            .active_level = 0,
        },
    };
    return iot_button_create(&cfg);
}

static void init_buttons(void)
{
    button_handle_t k1 = make_button(GPIO_KEY1);
    if (k1) {
        iot_button_register_cb(k1, BUTTON_PRESS_DOWN, key1_down, NULL);
        iot_button_register_cb(k1, BUTTON_PRESS_UP, key1_up, NULL);
    }
    button_handle_t k2 = make_button(GPIO_KEY2);
    if (k2) {
        iot_button_register_cb(k2, BUTTON_PRESS_DOWN, key2_down, NULL);
        iot_button_register_cb(k2, BUTTON_PRESS_UP, key2_up, NULL);
    }
    button_handle_t k3 = make_button(GPIO_KEY3);
    if (k3) {
        iot_button_register_cb(k3, BUTTON_SINGLE_CLICK, key3_click, NULL);
    }
}

static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = GPIO_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
}

static esp_err_t write_byte(uint8_t reg_addr, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (WFSENSOR_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t read_continuous(uint8_t reg_addr, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (WFSENSOR_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (WFSENSOR_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static uint8_t wait_finish(void)
{
    uint8_t status = 0;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (WFSENSOR_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x02, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (WFSENSOR_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &status, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return status;
}

static void pressure_sensor_init(void)
{
    if (i2c_master_init() != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed");
    }
}

static void read_pressure_data(void)
{
    if (write_byte(0x30, CMD_GROUP_CONVERT) != ESP_OK) return;
    vTaskDelay(pdMS_TO_TICKS(5));
    int timeout = 0;
    uint8_t status;
    do {
        vTaskDelay(pdMS_TO_TICKS(2));
        status = wait_finish();
        if (++timeout > 100) return;
    } while (status != 0x01);
    if (read_continuous(0x06, s_sensor_data, 5) != ESP_OK) return;

    int32_t dat = ((int32_t)s_sensor_data[0] << 16) |
                  ((int32_t)s_sensor_data[1] << 8) | s_sensor_data[2];
    float fdat = (dat >= 0x800000) ? (dat - 0x1000000) / 8388608.0f
                                   : dat / 8388608.0f;
    s_pressure = fdat * 50.0f + 5.0f;
    device_update_property_float("pressure", s_pressure);
}

static void control_task(void *arg)
{
    (void)arg;
    while (1) {
        read_pressure_data();
        apply_air();
        apply_motor();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void on_device_init(void)
{
    ESP_LOGI(TAG, "on_device_init");
    init_properties();
    init_gpio();
    init_pwm();
    init_buttons();
    pressure_sensor_init();
}

esp_err_t on_device_first_ready(void)
{
    if (xTaskCreate(control_task, "rt01_ctrl", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void on_set_property(char *property_name, cJSON *property_value, int msg_id)
{
    (void)property_value;
    (void)msg_id;
    if (strcmp(property_name, "motor") == 0) {
        motor_from_remote(motor_property.value.int_value);
    } else if (strcmp(property_name, "deflate") == 0) {
        deflate_property.value.int_value = deflate_property.value.int_value ? 1 : 0;
        apply_air();
    } else if (strcmp(property_name, "target_pressure") == 0 ||
               strcmp(property_name, "min_pressure") == 0) {
        clamp_pressures();
        apply_air();
    }
}

void on_action(cJSON *root)
{
    (void)root;
}

void on_mqtt_msg_process(char *topic, cJSON *root)
{
    (void)topic;
    (void)root;
}

void on_device_before_sleep(void)
{
    s_suck_hold = false;
    s_sine_mode = false;
    s_motor_cycle = 0;
    motor_property.value.int_value = 0;
    deflate_property.value.int_value = 0;
    apply_motor_output(0);
    gpio_set_level(GPIO_PUM, 0);
    gpio_set_level(GPIO_YV, 0);
}
