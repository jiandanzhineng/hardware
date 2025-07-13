
#include "esp_log.h"
#include "iot_button.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "device_common.h"
#include "cJSON.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define BUTTON_PIN          6
#define SERVO_PIN           7
#define LED_PIN             10

#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_13_BIT  // 13-bit resolution (0-8191)
#define LEDC_FREQUENCY      50                  // 50Hz for servo motor

static const char *TAG = "zidongsuo";

// Emergency mode flag and related variables
static bool emergency_mode_flag = false;
static int64_t button_press_start_time = 0;
static bool button_is_pressed = false;
static bool long_press_triggered = false;

// Low battery detection counter
static int low_battery_count = 0;
static const int LOW_BATTERY_THRESHOLD_COUNT = 3;

// Function declarations
void set_servo_angle(float angle);
void emergency_mode_task(void *pvParameters);

// Define GPIO and ADC pins
#define BAT_EN_GPIO         GPIO_NUM_1    // GPIO pin connected to BAT_EN (adjust as needed)
#define BAT_ADC_CHANNEL     ADC_CHANNEL_0 // ADC channel for battery measurement (adjust as needed)
#define BAT_ADC_UNIT        ADC_UNIT_1    // ADC unit

// Voltage divider resistor values (in ohms)
#define R_TOP               27000.0f  // R14 (27k)
#define R_BOTTOM            10000.0f  // R17 (10k)


device_property_t open_property;
extern device_property_t device_type_property;
extern device_property_t sleep_time_property;
extern device_property_t battery_property;

device_property_t *device_properties[] = {
    &device_type_property,
    &open_property,
    &sleep_time_property,
    &battery_property,
};

int device_properties_num = sizeof(device_properties) / sizeof(device_properties[0]);

void button_single_click_cb(void *arg, void *usr_data)
{
    ESP_LOGI(TAG, "BUTTON_SINGLE_CLICK");
    // publish mqtt message {'method':'action', 'action':'key_clicked'}
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "method", "action");
    cJSON_AddStringToObject(root, "action", "key_clicked");
    char *json_data = cJSON_Print(root);
    ESP_LOGI(TAG, "json_data: %s", json_data);
    esp_mqtt_client_publish(smqtt_client, publish_topic, json_data, 0, 1, 0);
    cJSON_Delete(root);
    free(json_data);
}

void button_press_down_cb(void *arg, void *usr_data)
{
    ESP_LOGI(TAG, "BUTTON_PRESS_DOWN");
    button_is_pressed = true;
    button_press_start_time = esp_timer_get_time();
    long_press_triggered = false;  // Reset long press trigger flag
}

void button_press_up_cb(void *arg, void *usr_data)
{
    ESP_LOGI(TAG, "BUTTON_PRESS_UP");
    button_is_pressed = false;
}

void on_mqtt_msg_process(char *topic, cJSON *root){

}

void on_set_property(char *property_name, cJSON *property_value, int msg_id){
    ESP_LOGI(TAG, "on_set_property property_name:%s", property_name);
    if(strcmp(property_name, "open") == 0){
        int value = property_value->valueint;
        open_property.value.int_value = value;
        
        // Update servo position
        float angle = value ? 0.0f : 180.0f;
        set_servo_angle(angle);
        
        // Update LED state
        gpio_set_level(LED_PIN, !value);
    }
}

// ADC calibration and handle variables
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc_cali_handle = NULL;
static bool do_calibration = false;


void battery_measurement_init(void)
{
    // Configure BAT_EN GPIO as output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BAT_EN_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    // Configure ADC
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = BAT_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));
    
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_11,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BAT_ADC_CHANNEL, &config));
    
    // Try to calibrate ADC
    adc_cali_handle_t handle = NULL;
    
    if (!do_calibration) {
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = BAT_ADC_UNIT,
            .atten = ADC_ATTEN_DB_11,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &handle));
        do_calibration = true;
        adc_cali_handle = handle;
        ESP_LOGI(TAG, "Calibration scheme: Curve Fitting");
    } else {
        ESP_LOGW(TAG, "Curve Fitting calibration not supported, using default conversion");
        do_calibration = false;
    }
}

// Function to read battery voltage
float read_battery_voltage(void)
{
    // Enable the battery measurement circuit
    gpio_set_level(BAT_EN_GPIO, 1);
    
    // Small delay to allow the circuit to stabilize
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Read ADC value (take multiple samples for better accuracy)
    int adc_reading = 0;
    const int NUM_SAMPLES = 1;
    
    for (int i = 0; i < NUM_SAMPLES; i++) {
        int raw;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BAT_ADC_CHANNEL, &raw));
        adc_reading += raw;
    }
    adc_reading /= NUM_SAMPLES;
    
    // Disable the battery measurement circuit to save power
    gpio_set_level(BAT_EN_GPIO, 0);
    
    // Convert ADC reading to voltage
    int voltage_mv = 0;
    if (do_calibration) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_reading, &voltage_mv));
    } else {
        // If no calibration scheme, use a simple approximation (adjust for your specific hardware)
        voltage_mv = (adc_reading * 3300) / 4095;  // Assuming 3.3V reference and 12-bit ADC
    }
    
    // Calculate actual battery voltage considering the voltage divider
    float divider_ratio = (R_TOP + R_BOTTOM) / R_BOTTOM;
    float battery_voltage = (voltage_mv / 1000.0f) * divider_ratio;
    
    return battery_voltage;
}

// 电压值到电池百分比转换函数
static int toPercentage(float voltage)
{
    // 电压值转换为毫伏
    int voltage_mv = (int)(voltage * 1000);
    
    // 参考Battery.c中的电压百分比对照表
    const static int Battery_Level_Percent_Table[11] = {3000, 3650, 3700, 3740, 3760, 3795, 3840, 3910, 3980, 4070, 4200};
    
    int i = 0;
    if (voltage_mv < Battery_Level_Percent_Table[0])
    {
        return 0;
    }

    for (i = 1; i < 11; i++)
    {
        if (voltage_mv < Battery_Level_Percent_Table[i])
        {
            return i * 10 - (10 * (Battery_Level_Percent_Table[i] - voltage_mv)) / (Battery_Level_Percent_Table[i] - Battery_Level_Percent_Table[i - 1]);
        }
    }

    return 100;
}

// Function to periodically read battery voltage
void battery_task(void *pvParameters)
{
    while (1) {
        float battery_v = read_battery_voltage();
        ESP_LOGI(TAG, "Battery Voltage: %.2f V", battery_v);
        
        // 将电压值转换为百分比并设置到battery_property
        int battery_percentage = toPercentage(battery_v);
        battery_property.value.int_value = battery_percentage;
        ESP_LOGI(TAG, "Battery Percentage: %d%%", battery_percentage);
        
        // Check if battery is below 20% and count consecutive low readings
        if (battery_percentage < 20) {
            low_battery_count++;
            ESP_LOGI(TAG, "Battery below 20%% (count: %d/%d)", low_battery_count, LOW_BATTERY_THRESHOLD_COUNT);
            
            // Activate emergency mode only after 3 consecutive low readings
            if (low_battery_count >= LOW_BATTERY_THRESHOLD_COUNT) {
                ESP_LOGI(TAG, "Battery below 20%% for %d consecutive readings, activating emergency mode", LOW_BATTERY_THRESHOLD_COUNT);
                emergency_mode_flag = true;
            }
        } else {
            // Reset counter if battery is above 20%
            if (low_battery_count > 0) {
                ESP_LOGI(TAG, "Battery above 20%%, resetting low battery counter");
                low_battery_count = 0;
            }
        }
        
        // Wait for 30 seconds before next reading
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

// Emergency mode task - opens device every 10 seconds when flag is set
void emergency_mode_task(void *pvParameters)
{
    while (1) {
        // Check if button has been pressed for more than 60 seconds
        if (button_is_pressed && !long_press_triggered) {
            int64_t press_duration = esp_timer_get_time() - button_press_start_time;
            if (press_duration >= 60000000) { // 60 seconds in microseconds
                ESP_LOGI(TAG, "Button pressed for more than 60 seconds, activating emergency mode");
                emergency_mode_flag = true;
                long_press_triggered = true;  // Prevent multiple triggers
            }
        }
        
        if (emergency_mode_flag) {
            ESP_LOGI(TAG, "Emergency mode active - opening device");
            
            // Set open property to 1 (open)
            open_property.value.int_value = 1;
            
            // Update servo position to open (0 degrees)
            set_servo_angle(0.0f);
            
            // Update LED state (turn off LED when open)
            gpio_set_level(LED_PIN, 0);
            
            // Publish MQTT message about emergency opening
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "method", "action");
            cJSON_AddStringToObject(root, "action", "emergency_open");
            char *json_data = cJSON_Print(root);
            ESP_LOGI(TAG, "Emergency open json_data: %s", json_data);
            esp_mqtt_client_publish(smqtt_client, publish_topic, json_data, 0, 1, 0);
            cJSON_Delete(root);
            free(json_data);
            
            // Wait for 10 seconds before next emergency open
            vTaskDelay(pdMS_TO_TICKS(10000));
        } else {
            // If not in emergency mode, check every second
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

// Convert angle to PWM duty cycle
uint32_t angle_to_duty(float angle) {
    // Convert angle (0-180) to duty cycle (0-8191)
    // Servo expects pulse width between 0.5ms and 2.5ms within a 20ms period
    // 0.5ms/20ms = 2.5% duty, 2.5ms/20ms = 12.5% duty
    float duty_percent = (angle / 180.0) * (12.5 - 2.5) + 2.5;
    return (uint32_t)((duty_percent / 100.0) * 8191);
}

// Set servo angle
void set_servo_angle(float angle) {
    uint32_t duty = angle_to_duty(angle);
    ESP_LOGI(TAG, "Setting angle to %.1f, duty: %lu", angle, duty);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

void on_device_init(void){
    ESP_LOGI(TAG, "on_device_init property num:%d", device_properties_num);
    
    // Initialize open_property
    open_property.readable = true;
    open_property.writeable = true;
    strcpy(open_property.name, "open");
    open_property.value_type = PROPERTY_TYPE_INT;
    open_property.value.int_value = 0;
    
    // Configure LED
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LED_PIN);
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    
    // Configure servo using LEDC
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = LEDC_FREQUENCY,
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .channel = LEDC_CHANNEL,
        .duty = 0,
        .gpio_num = SERVO_PIN,
        .speed_mode = LEDC_MODE,
        .timer_sel = LEDC_TIMER
    };
    ledc_channel_config(&ledc_channel);
    
    // Initial state - LED off initially
    gpio_set_level(LED_PIN, 1);
    // set_servo_angle(0);
    
    // Create button with iot_button library
    button_config_t gpio_btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = BUTTON_PIN,
            .active_level = 0,
        },
    };
    button_handle_t gpio_btn = iot_button_create(&gpio_btn_cfg);
    if(NULL == gpio_btn) {
        ESP_LOGE(TAG, "Button create failed");
    }
    iot_button_register_cb(gpio_btn, BUTTON_SINGLE_CLICK, button_single_click_cb, NULL);
    iot_button_register_cb(gpio_btn, BUTTON_PRESS_DOWN, button_press_down_cb, NULL);
    iot_button_register_cb(gpio_btn, BUTTON_PRESS_UP, button_press_up_cb, NULL);
    
    // Initialize battery measurement
    battery_measurement_init();
    
    // Create battery monitoring task
    xTaskCreate(battery_task, "battery_task", 2048, NULL, 5, NULL);
    
    // Create emergency mode task
    xTaskCreate(emergency_mode_task, "emergency_mode_task", 2048, NULL, 4, NULL);
}

void on_device_first_ready(void){
    // Initial servo position
    // set_servo_angle(0);
}

void on_action(cJSON *root){
    
}

