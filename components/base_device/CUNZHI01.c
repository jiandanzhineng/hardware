#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/adc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "base_device.h"
#include "device_common.h"

static const char *TAG = "CUNZHI01";

// GPIO Definitions
#define GPIO_MOTOR       7   // PWM, shared with LED
#define GPIO_BAT_ADC     0   // ADC1_CH0
#define GPIO_BOOST_ADC   1   // ADC1_CH1
#define GPIO_PRESS1      2   // ADC1_CH2
#define GPIO_PRESS2      3   // ADC1_CH3
#define GPIO_PULSE_DIR_A 4   // Output
#define GPIO_PULSE_DIR_B 5   // Output
#define GPIO_PULSE_BOOST 6   // PWM
#define GPIO_BAT_EN      19  // Output

// Motor PWM Configuration
#define MOTOR_TIMER              LEDC_TIMER_0
#define MOTOR_MODE               LEDC_LOW_SPEED_MODE
#define MOTOR_CHANNEL            LEDC_CHANNEL_0
#define MOTOR_DUTY_RES           LEDC_TIMER_13_BIT // 8191 resolution
#define MOTOR_FREQ               5000

// Boost PWM Configuration
#define BOOST_TIMER              LEDC_TIMER_1
#define BOOST_MODE               LEDC_LOW_SPEED_MODE
#define BOOST_CHANNEL            LEDC_CHANNEL_1
#define BOOST_DUTY_RES           8 // 8 bit resolution for frequency control
#define BOOST_FREQ               10000 // Initial frequency 10kHz

// Properties
device_property_t power_property;
device_property_t voltage_property;
device_property_t pressure_property;
device_property_t pressure1_property;
device_property_t report_delay_ms_property;
device_property_t shock_property;
device_property_t delay_property;
device_property_t safe_property;

// Game properties
device_property_t game_mode_property;
device_property_t game_duration_property;
device_property_t game_e_vol_property;
device_property_t game_e_dur_property;
device_property_t game_p1_thresh_property;
device_property_t game_p2_thresh_property;
device_property_t game_m_dur_property;
device_property_t game_m_power_property;
device_property_t game_m_step_property;
device_property_t game_cooldown_property;
device_property_t game_kegel_t_property;

extern device_property_t device_type_property;
extern device_property_t sleep_time_property;
extern device_property_t battery_property;

device_property_t *device_properties[] = {
    &device_type_property,
    &sleep_time_property,
    &battery_property,
    &power_property,
    &voltage_property,
    &shock_property,
    &delay_property,
    &safe_property,
    &pressure_property,
    &pressure1_property,
    &report_delay_ms_property,
    &game_mode_property,
    &game_duration_property,
    &game_e_vol_property,
    &game_e_dur_property,
    &game_p1_thresh_property,
    &game_p2_thresh_property,
    &game_m_dur_property,
    &game_m_power_property,
    &game_m_step_property,
    &game_cooldown_property,
    &game_kegel_t_property,
};

int device_properties_num = sizeof(device_properties) / sizeof(device_properties[0]);

// Internal State
static esp_timer_handle_t discharge_timer;
static bool discharge_active = false;

// Discharge State Machine
typedef enum {
    DISCHARGE_IDLE = 0,
    DISCHARGE_FORWARD,
    DISCHARGE_PAUSE,
    DISCHARGE_REVERSE,
    DISCHARGE_COMPLETE
} discharge_state_t;

static discharge_state_t discharge_state = DISCHARGE_IDLE;

// Forward declarations
static void init_gpio(void);
static void init_pwm(void);
static void init_adc(void);
static void init_timer(void);
static void motor_control(int power);
static void boost_control(int voltage);
static void report_task(void *arg);
static void pressure_task(void *arg);
static void battery_task(void *arg);
static void pulse_task(void *arg);
static void boost_pid_task(void *arg);
static void gameplay_task(void *arg);
static void nvs_cunzhi01_init(void);
static void nvs_cunzhi01_read(void);
static void nvs_cunzhi01_set(void);

// Initialize Properties
void init_properties(void) {
    power_property.readable = true;
    power_property.writeable = true;
    strcpy(power_property.name, "power");
    power_property.value_type = PROPERTY_TYPE_INT;
    power_property.value.int_value = 0;
    power_property.min = 0;
    power_property.max = 255;

    voltage_property.readable = true;
    voltage_property.writeable = true;
    strcpy(voltage_property.name, "voltage");
    voltage_property.value_type = PROPERTY_TYPE_INT;
    voltage_property.value.int_value = 0;
    voltage_property.min = 0;
    voltage_property.max = 75;

    shock_property.readable = true;
    shock_property.writeable = true;
    strcpy(shock_property.name, "shock");
    shock_property.value_type = PROPERTY_TYPE_INT;
    shock_property.value.int_value = 0;
    shock_property.min = 0;
    shock_property.max = 1;

    delay_property.readable = true;
    delay_property.writeable = true;
    strcpy(delay_property.name, "delay");
    delay_property.value_type = PROPERTY_TYPE_INT;
    delay_property.value.int_value = 30;
    delay_property.min = 20;
    delay_property.max = 1000;

    safe_property.readable = true;
    safe_property.writeable = true;
    strcpy(safe_property.name, "safe");
    safe_property.value_type = PROPERTY_TYPE_INT;
    safe_property.value.int_value = 1;
    safe_property.min = 0;
    safe_property.max = 1;

    pressure_property.readable = true;
    pressure_property.writeable = false;
    strcpy(pressure_property.name, "pressure");
    pressure_property.value_type = PROPERTY_TYPE_FLOAT;
    pressure_property.value.float_value = 0;

    pressure1_property.readable = true;
    pressure1_property.writeable = false;
    strcpy(pressure1_property.name, "pressure1");
    pressure1_property.value_type = PROPERTY_TYPE_FLOAT;
    pressure1_property.value.float_value = 0;

    report_delay_ms_property.readable = true;
    report_delay_ms_property.writeable = true;
    strcpy(report_delay_ms_property.name, "report_delay_ms");
    report_delay_ms_property.value_type = PROPERTY_TYPE_INT;
    report_delay_ms_property.value.int_value = 5000;
    report_delay_ms_property.min = 100;
    report_delay_ms_property.max = 10000;

    // Initialize Game Properties
    game_mode_property.readable = true;
    game_mode_property.writeable = true;
    strcpy(game_mode_property.name, "game_mode");
    game_mode_property.value_type = PROPERTY_TYPE_INT;
    game_mode_property.value.int_value = 0;

    game_duration_property.readable = true;
    game_duration_property.writeable = true;
    strcpy(game_duration_property.name, "game_duration");
    game_duration_property.value_type = PROPERTY_TYPE_INT;
    game_duration_property.value.int_value = 0;

    game_e_vol_property.readable = true;
    game_e_vol_property.writeable = true;
    strcpy(game_e_vol_property.name, "game_e_vol");
    game_e_vol_property.value_type = PROPERTY_TYPE_INT;
    game_e_vol_property.value.int_value = 0;

    game_e_dur_property.readable = true;
    game_e_dur_property.writeable = true;
    strcpy(game_e_dur_property.name, "game_e_dur");
    game_e_dur_property.value_type = PROPERTY_TYPE_INT;
    game_e_dur_property.value.int_value = 0;

    game_p1_thresh_property.readable = true;
    game_p1_thresh_property.writeable = true;
    strcpy(game_p1_thresh_property.name, "game_p1_thresh");
    game_p1_thresh_property.value_type = PROPERTY_TYPE_FLOAT;
    game_p1_thresh_property.value.float_value = 0;

    game_p2_thresh_property.readable = true;
    game_p2_thresh_property.writeable = true;
    strcpy(game_p2_thresh_property.name, "game_p2_thresh");
    game_p2_thresh_property.value_type = PROPERTY_TYPE_FLOAT;
    game_p2_thresh_property.value.float_value = 0;

    game_m_dur_property.readable = true;
    game_m_dur_property.writeable = true;
    strcpy(game_m_dur_property.name, "game_m_dur");
    game_m_dur_property.value_type = PROPERTY_TYPE_INT;
    game_m_dur_property.value.int_value = 0;

    game_m_power_property.readable = true;
    game_m_power_property.writeable = true;
    strcpy(game_m_power_property.name, "game_m_power");
    game_m_power_property.value_type = PROPERTY_TYPE_INT;
    game_m_power_property.value.int_value = 0;

    game_m_step_property.readable = true;
    game_m_step_property.writeable = true;
    strcpy(game_m_step_property.name, "game_m_step");
    game_m_step_property.value_type = PROPERTY_TYPE_INT;
    game_m_step_property.value.int_value = 0;

    game_cooldown_property.readable = true;
    game_cooldown_property.writeable = true;
    strcpy(game_cooldown_property.name, "game_cooldown");
    game_cooldown_property.value_type = PROPERTY_TYPE_INT;
    game_cooldown_property.value.int_value = 0;

    game_kegel_t_property.readable = true;
    game_kegel_t_property.writeable = true;
    strcpy(game_kegel_t_property.name, "game_kegel_t");
    game_kegel_t_property.value_type = PROPERTY_TYPE_INT;
    game_kegel_t_property.value.int_value = 0;
}

void on_device_init(void) {
    ESP_LOGI(TAG, "Initializing CUNZHI01 Device");
    init_properties();
    init_gpio();
    init_pwm();
    init_adc();
    init_timer();
    nvs_cunzhi01_init();
    nvs_cunzhi01_read();
    boost_control(0);
}

void on_device_first_ready(void) {
    xTaskCreate(report_task, "report_task", 4096, NULL, 5, NULL);
    xTaskCreate(pressure_task, "pressure_task", 4096, NULL, 5, NULL);
    xTaskCreate(battery_task, "battery_task", 4096, NULL, 1, NULL);
    xTaskCreate(pulse_task, "pulse_task", 4096, NULL, 10, NULL);
    xTaskCreate(boost_pid_task, "boost_pid_task", 4096, NULL, 6, NULL);
    xTaskCreate(gameplay_task, "gameplay_task", 4096, NULL, 5, NULL);
}

void on_set_property(char *property_name, cJSON *property_value, int msg_id) {
    if (strcmp(property_name, "power") == 0) {
        motor_control(power_property.value.int_value);
    } else if (strcmp(property_name, "voltage") == 0) {
        boost_control(voltage_property.value.int_value);
    } else if (strcmp(property_name, "shock") == 0) {
        ESP_LOGI(TAG, "Shock changed to: %d", shock_property.value.int_value);
        if (shock_property.value.int_value == 0) {
            boost_control(0);
        } else {
            boost_control(voltage_property.value.int_value);
        }
    } else if (strcmp(property_name, "safe") == 0) {
        nvs_cunzhi01_set();
    }
}

void on_action(cJSON *root) {
    // No specific actions defined
}

void on_mqtt_msg_process(char *topic, cJSON *root) {
    // Standard processing
}

void on_device_before_sleep(void) {
    // Reset game mode
    game_mode_property.value.int_value = 0;
    
    // Turn off outputs
    motor_control(0);
    boost_control(0);
    gpio_set_level(GPIO_PULSE_DIR_A, 0);
    gpio_set_level(GPIO_PULSE_DIR_B, 0);
    gpio_set_level(GPIO_BAT_EN, 0);
}

// Hardware Initialization
static void init_gpio(void) {
    gpio_reset_pin(GPIO_BAT_EN);
    gpio_set_direction(GPIO_BAT_EN, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_BAT_EN, 0);

    gpio_reset_pin(GPIO_PULSE_DIR_A);
    gpio_set_direction(GPIO_PULSE_DIR_A, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_PULSE_DIR_A, 0);

    gpio_reset_pin(GPIO_PULSE_DIR_B);
    gpio_set_direction(GPIO_PULSE_DIR_B, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_PULSE_DIR_B, 0);
}

static void init_pwm(void) {
    // Motor PWM
    ledc_timer_config_t motor_timer = {
        .speed_mode = MOTOR_MODE,
        .duty_resolution = MOTOR_DUTY_RES,
        .timer_num = MOTOR_TIMER,
        .freq_hz = MOTOR_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&motor_timer);

    ledc_channel_config_t motor_channel = {
        .gpio_num = GPIO_MOTOR,
        .speed_mode = MOTOR_MODE,
        .channel = MOTOR_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = MOTOR_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&motor_channel);

    // Boost PWM
    ledc_timer_config_t boost_timer = {
        .speed_mode = BOOST_MODE,
        .duty_resolution = BOOST_DUTY_RES,
        .timer_num = BOOST_TIMER,
        .freq_hz = BOOST_FREQ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    esp_err_t err = ledc_timer_config(&boost_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Boost timer config failed: %s", esp_err_to_name(err));
    }

    ledc_channel_config_t boost_channel = {
        .gpio_num = GPIO_PULSE_BOOST,
        .speed_mode = BOOST_MODE,
        .channel = BOOST_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BOOST_TIMER,
        .duty = 0,
        .hpoint = 0
    };
    err = ledc_channel_config(&boost_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Boost channel config failed: %s", esp_err_to_name(err));
    }
}

static void init_adc(void) {
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11); // GPIO 0 (Bat)
    adc1_config_channel_atten(ADC1_CHANNEL_1, ADC_ATTEN_DB_11); // GPIO 1 (Boost ADC)
    adc1_config_channel_atten(ADC1_CHANNEL_2, ADC_ATTEN_DB_11); // GPIO 2 (Press1)
    adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_DB_11); // GPIO 3 (Press2)
}

// Timer Callback for Pulse Discharge Sequence
static void timer_callback(void* arg) {
    if (!discharge_active) {
        esp_timer_stop(discharge_timer);
        return;
    }

    switch (discharge_state) {
        case DISCHARGE_FORWARD:
            // Forward finished, pause
            gpio_set_level(GPIO_PULSE_DIR_A, 0);
            gpio_set_level(GPIO_PULSE_DIR_B, 0);
            discharge_state = DISCHARGE_PAUSE;
            esp_timer_start_once(discharge_timer, 150); // 150us pause
            break;
        case DISCHARGE_PAUSE:
            // Pause finished, reverse
            gpio_set_level(GPIO_PULSE_DIR_A, 0);
            gpio_set_level(GPIO_PULSE_DIR_B, 1); // Reverse
            discharge_state = DISCHARGE_REVERSE;
            esp_timer_start_once(discharge_timer, 150); // 150us reverse
            break;
        case DISCHARGE_REVERSE:
            // Reverse finished, stop
            gpio_set_level(GPIO_PULSE_DIR_A, 0);
            gpio_set_level(GPIO_PULSE_DIR_B, 0);
            discharge_state = DISCHARGE_COMPLETE;
            discharge_active = false; // Sequence done
            break;
        default:
            discharge_active = false;
            break;
    }
}

static void init_timer(void) {
    esp_timer_create_args_t timer_args = {
        .callback = timer_callback,
        .name = "discharge_timer",
        .dispatch_method = ESP_TIMER_TASK,
        .skip_unhandled_events = false
    };
    esp_timer_create(&timer_args, &discharge_timer);
}

// Control Functions
static void motor_control(int power) {
    if (power < 0) power = 0;
    if (power > 255) power = 255;
    
    // Map 0-255 to 0-8191
    uint32_t duty = (uint32_t)power * 8191 / 255;
    ledc_set_duty(MOTOR_MODE, MOTOR_CHANNEL, duty);
    ledc_update_duty(MOTOR_MODE, MOTOR_CHANNEL);
}

static void boost_control(int voltage) {
    int max_voltage = (safe_property.value.int_value == 1) ? 36 : 75;
    if (voltage < 0) voltage = 0;
    if (voltage > max_voltage) voltage = max_voltage;
    ESP_LOGI(TAG, "Boost changed to voltage: %d", voltage);
    if (shock_property.value.int_value == 0 || voltage == 0) {
        ledc_set_duty(BOOST_MODE, BOOST_CHANNEL, 0);
        ledc_update_duty(BOOST_MODE, BOOST_CHANNEL);
    }
}

static void start_discharge_sequence(void) {
    if (discharge_active) return;

    discharge_active = true;
    discharge_state = DISCHARGE_FORWARD;
    
    // Start Forward
    gpio_set_level(GPIO_PULSE_DIR_A, 1);
    gpio_set_level(GPIO_PULSE_DIR_B, 0);
    
    esp_timer_start_once(discharge_timer, 150); // 150us
}

// Tasks
static void boost_pid_task(void *arg) {
    float kp = 1.0f;
    float ki = 0.1f;
    float kd = 0.0f;
    float integral = 0.0f;
    float prev_error = 0.0f;

    while (1) {
        if (shock_property.value.int_value == 0 || voltage_property.value.int_value == 0) {
            integral = 0;
            prev_error = 0;
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int target_v = voltage_property.value.int_value;
        int max_voltage = (safe_property.value.int_value == 1) ? 36 : 75;
        if (target_v > max_voltage) {
            target_v = max_voltage;
        }
        int raw = adc1_get_raw(ADC1_CHANNEL_1);
        
        // R35 = 510k, R36 = 15k, ratio = 35
        float current_v = (raw / 4095.0f) * 3.3f * 35.0f;
        
        float error = target_v - current_v;
        integral += error;
        
        if (integral > 1000) integral = 1000;
        if (integral < -1000) integral = -1000;
        
        float derivative = error - prev_error;
        prev_error = error;
        
        float output = kp * error + ki * integral + kd * derivative;
        
        if (output > 200) output = 200; // Limit max duty to 200/255
        if (output < 0) output = 0;
        
        ledc_set_duty(BOOST_MODE, BOOST_CHANNEL, (uint32_t)output);
        ledc_update_duty(BOOST_MODE, BOOST_CHANNEL);
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void pulse_task(void *arg) {
    while (1) {
        if (shock_property.value.int_value == 1 && voltage_property.value.int_value > 0) {
            start_discharge_sequence();
        }
        int d = delay_property.value.int_value;
        if (d < 20) d = 20;
        if (d > 1000) d = 1000;
        vTaskDelay(pdMS_TO_TICKS(d));
    }
}

static float calculate_pressure(int raw) {
    float v_ref = 3.3f;
    float r_fixed = 1000.0f;
    float v = (raw / 4095.0f) * v_ref;
    float r = 0.0f;

    if (v >= 3.25f) {
        // Voltage near 3.3V -> Open circuit
        r = 99999.0f;
    } else if (v <= 0.05f) {
        // Voltage near 0V -> Short circuit
        r = 0.0f;
    } else {
        // Calculate resistance: Rx = (V_out * R_fixed) / (V_ref - V_out)
        r = (v * r_fixed) / (v_ref - v);
    }
    if (r < 100.0f) {
        return 800.0f; // 限制最大压力值
    } else if(r >= 99998.0f){
        return 0.0f;
    }
    return 80000.0f / r;
}

static void pressure_task(void *arg) {
    while (1) {
        int raw1 = adc1_get_raw(ADC1_CHANNEL_2);
        int raw2 = adc1_get_raw(ADC1_CHANNEL_3);
        
        float p1 = calculate_pressure(raw1);
        float p2 = calculate_pressure(raw2);

        float r11 = 0.5 * p1 + 0.5 * pressure_property.value.float_value;
        device_update_property_float("pressure", r11);

        float r22 = 0.5 * p2 + 0.5 * pressure1_property.value.float_value;
        device_update_property_float("pressure1", r22);
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void battery_task(void *arg) {
    while (1) {
        gpio_set_level(GPIO_BAT_EN, 1);
        vTaskDelay(pdMS_TO_TICKS(20)); // Wait for stable
        int raw = adc1_get_raw(ADC1_CHANNEL_0);
        gpio_set_level(GPIO_BAT_EN, 0);

        // Map ADC to Percentage.
        // Reference TD01/dianji. 
        // float bat_v = raw / BAT_ADC_K;
        // Let's use simple mapping for now.
        // 4.2V = 100%, 3.4V = 0%.
        // Assuming default attenuation, 4095 ~= 2.6V? No, DB_11 is up to 2.6V or 3.3V?
        // ESP32C3 DB_11 is up to ~2500mV. Divider is needed.
        // Assuming hardware is correct, just map raw.
        // Placeholder mapping:
        int pct = raw * 100 / 4095; 
        device_update_property_int("battery", pct);

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

static void report_task(void *arg) {
    while (1) {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "method", "update");
        cJSON_AddNumberToObject(root, "pressure", pressure_property.value.float_value);
        cJSON_AddNumberToObject(root, "pressure1", pressure1_property.value.float_value);
        cJSON_AddNumberToObject(root, "battery", battery_property.value.int_value);
        
        // Also report status
        mqtt_publish(root);

        int delay = report_delay_ms_property.value.int_value;
        if (delay < 100) delay = 100;
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

// Gameplay Task State Variables
static int64_t game_start_time = 0;
static int64_t active_e_stim_end = 0;
static int64_t active_motor_end = 0;
static int64_t cooldown_end_time = 0;
static float current_motor_power = 0.0f;
static int no_kegel_time_ms = 0;
static int last_game_mode = 0;

static void gameplay_task(void *arg) {
    while (1) {
        int current_mode = game_mode_property.value.int_value;
        int64_t current_time = esp_timer_get_time();

        // Check for mode switch
        if (current_mode != last_game_mode) {
            last_game_mode = current_mode;
            game_start_time = current_time;
            active_e_stim_end = 0;
            active_motor_end = 0;
            cooldown_end_time = 0;
            current_motor_power = 0.0f;
            no_kegel_time_ms = 0;
            
            // Turn off outputs if mode changed
            motor_control(0);
            boost_control(0);
            device_update_property_int("shock", 0);
            device_update_property_int("voltage", 0);
            device_update_property_int("power", 0);
        }

        if (current_mode == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // Check for game timeout
        int duration_s = game_duration_property.value.int_value;
        if (duration_s > 0 && (current_time - game_start_time) > (int64_t)duration_s * 1000000) {
            device_update_property_int("game_mode", 0);
            motor_control(0);
            boost_control(0);
            device_update_property_int("shock", 0);
            device_update_property_int("voltage", 0);
            device_update_property_int("power", 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        float p1 = pressure_property.value.float_value;
        float p2 = pressure1_property.value.float_value;

        if (current_mode == 1) { // Edging
            float p1_thresh = game_p1_thresh_property.value.float_value;
            int e_dur = game_e_dur_property.value.int_value;
            int e_vol = game_e_vol_property.value.int_value;
            int m_step = game_m_step_property.value.int_value;
            int cooldown = game_cooldown_property.value.int_value;

            if (current_time < cooldown_end_time) {
                // In punishment or cooldown
                if (current_time > active_e_stim_end) {
                    device_update_property_int("shock", 0);
                    boost_control(0);
                }
                // Motor forced to 0 during cooldown/punishment
                current_motor_power = 0;
                motor_control(0);
            } else {
                if (p1 > p1_thresh) {
                    // Trigger punishment
                    current_motor_power = 0;
                    motor_control(0);
                    device_update_property_int("power", 0);

                    device_update_property_int("voltage", e_vol);
                    device_update_property_int("shock", 1);
                    boost_control(e_vol);

                    active_e_stim_end = current_time + (int64_t)e_dur * 1000;
                    cooldown_end_time = active_e_stim_end + (int64_t)cooldown * 1000;
                } else {
                    // Normal increase
                    current_motor_power += (m_step * 0.1f);
                    if (current_motor_power > 255.0f) current_motor_power = 255.0f;
                    int pwr = (int)current_motor_power;
                    motor_control(pwr);
                    device_update_property_int("power", pwr);
                }
            }
        } else if (current_mode == 2) { // Kegel
            float p1_thresh = game_p1_thresh_property.value.float_value;
            float p2_thresh = game_p2_thresh_property.value.float_value;
            int e_dur = game_e_dur_property.value.int_value;
            int e_vol = game_e_vol_property.value.int_value;
            int m_dur = game_m_dur_property.value.int_value;
            int m_power = game_m_power_property.value.int_value;
            int cooldown = game_cooldown_property.value.int_value;
            int kegel_t = game_kegel_t_property.value.int_value;

            if (p1 < p1_thresh) {
                no_kegel_time_ms += 100;
            } else {
                no_kegel_time_ms = 0;
            }

            if (current_time < cooldown_end_time) {
                if (current_time > active_e_stim_end) {
                    device_update_property_int("shock", 0);
                    boost_control(0);
                }
                if (current_time > active_motor_end) {
                    motor_control(0);
                    device_update_property_int("power", 0);
                }
                no_kegel_time_ms = 0; // Don't accumulate during punishment/cooldown
            } else {
                bool cond_tiptoe = (p2 > p2_thresh);
                bool cond_kegel = (no_kegel_time_ms >= kegel_t);

                bool triggered = false;

                if (cond_tiptoe || cond_kegel) {
                    device_update_property_int("voltage", e_vol);
                    device_update_property_int("shock", 1);
                    boost_control(e_vol);
                    active_e_stim_end = current_time + (int64_t)e_dur * 1000;
                    triggered = true;
                }

                if (cond_kegel) {
                    motor_control(m_power);
                    device_update_property_int("power", m_power);
                    active_motor_end = current_time + (int64_t)m_dur * 1000;
                    triggered = true;
                }

                if (triggered) {
                    int64_t punish_end = (active_e_stim_end > active_motor_end) ? active_e_stim_end : active_motor_end;
                    cooldown_end_time = punish_end + (int64_t)cooldown * 1000;
                    no_kegel_time_ms = 0;
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void nvs_cunzhi01_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void nvs_cunzhi01_read(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("cunzhi01_store", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
    } else {
        int32_t safe_value = 1; // default value
        err = nvs_get_i32(my_handle, "safe", &safe_value);
        if (err == ESP_OK) {
            device_update_property_int("safe", safe_value);
        }
        nvs_close(my_handle);
    }
}

static void nvs_cunzhi01_set(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("cunzhi01_store", NVS_READWRITE, &my_handle);
    if (err == ESP_OK) {
        nvs_set_i32(my_handle, "safe", safe_property.value.int_value);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}
