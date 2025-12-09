#include "esp_log.h"
#include "device_common.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include <math.h>

static const char *TAG = "QIYA";

#define WFSENSOR_I2C_ADDR 0x6D
#define CMD_GROUP_CONVERT 0x0A
#define I2C_MASTER_SCL_IO 6
#define I2C_MASTER_SDA_IO 7
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

device_property_t pressure_property;
device_property_t temperature_property;
device_property_t report_delay_ms_property;
extern device_property_t device_type_property;
extern device_property_t sleep_time_property;
extern device_property_t battery_property;

device_property_t *device_properties[] = {
    &device_type_property,
    &pressure_property,
    &temperature_property,
    &report_delay_ms_property,
    &sleep_time_property,
    &battery_property,
};
int device_properties_num = sizeof(device_properties) / sizeof(device_properties[0]);

extern void get_property(char *property_name, int msg_id);
extern void mqtt_publish(cJSON *root);

float current_pressure = 0.0;
float current_temperature = 0.0;
int32_t report_interval_ms = 5000;
uint8_t sensor_data[5];

static esp_err_t i2c_master_init(void);
static esp_err_t write_byte(uint8_t reg_addr, uint8_t data);
static esp_err_t read_continuous(uint8_t reg_addr, uint8_t *data, size_t len);
static uint8_t wait_finish(void);
static void indicate_group_convert(void);
static void get_tp_data(void);
static void calculate_press(void);
static void pressure_sensor_init(void);
static void read_pressure_data(void);
static void report_pressure_task(void);

void on_set_property(char *property_name, cJSON *property_value, int msg_id)
{
    if (strcmp(property_name, "report_delay_ms") == 0) {
        report_interval_ms = property_value->valueint;
    }
}

void on_action(cJSON *root)
{
}

void on_mqtt_msg_process(char *topic, cJSON *root)
{
}

void on_device_first_ready(void)
{
    ESP_LOGI(TAG, "Pressure sensor device ready");
}

static esp_err_t i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        return err;
    }
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
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
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

static uint8_t wait_finish(void)
{
    uint8_t status = 0;
    esp_err_t ret;
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (WFSENSOR_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, 0x02, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (WFSENSOR_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &status, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read status register: %s", esp_err_to_name(ret));
        return 0;
    }
    
    ESP_LOGD(TAG, "Status register: 0x%02X", status);
    return status;
}

static void indicate_group_convert(void)
{
    write_byte(0x30, CMD_GROUP_CONVERT);
    vTaskDelay(pdMS_TO_TICKS(5));
}

static void get_tp_data(void)
{
    read_continuous(0x06, sensor_data, 5);
}

static void calculate_press(void)
{
    int32_t dat = (sensor_data[0] << 16) | (sensor_data[1] << 8) | sensor_data[2];
    float fDat;
    
    ESP_LOGI(TAG, "Raw pressure data: 0x%02X 0x%02X 0x%02X, combined: 0x%06lX (%ld)", 
             sensor_data[0], sensor_data[1], sensor_data[2], (long)dat, (long)dat);
    
    if (dat >= 0x800000) {
        fDat = (dat - 0x1000000) / 8388608.0;
        ESP_LOGI(TAG, "Negative pressure: fDat = %.6f", fDat);
    } else {
        fDat = dat / 8388608.0;
        ESP_LOGI(TAG, "Positive pressure: fDat = %.6f", fDat);
    }
    
    current_pressure = fDat * 50 + 5;
    ESP_LOGI(TAG, "Final pressure: %.2f kPa", current_pressure);
    
    uint16_t dat_temp = (sensor_data[3] << 8) | sensor_data[4];
    
    ESP_LOGI(TAG, "Raw temp data: 0x%02X 0x%02X, combined: 0x%04X (%u)", 
             sensor_data[3], sensor_data[4], dat_temp, (unsigned int)dat_temp);
    
    if (dat_temp > 0x8000) {
        current_temperature = (dat_temp - 65844) / 256.0;
    } else {
        current_temperature = (dat_temp - 308) / 256.0;
    }
}

static void pressure_sensor_init(void)
{
    ESP_LOGI(TAG, "Initializing pressure sensor...");
    esp_err_t ret = i2c_master_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C master init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    uint8_t test_data[5] = {0};
    ret = read_continuous(0x06, test_data, 5);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sensor detected, test data: %02X %02X %02X %02X %02X", 
                 test_data[0], test_data[1], test_data[2], test_data[3], test_data[4]);
    } else {
        ESP_LOGE(TAG, "Sensor not detected or communication failed: %s", esp_err_to_name(ret));
    }
    
    ESP_LOGI(TAG, "Pressure sensor initialized");
}

static void read_pressure_data(void)
{
    esp_err_t ret = ESP_OK;
    
    ret = write_byte(0x30, CMD_GROUP_CONVERT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send convert command: %s", esp_err_to_name(ret));
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    
    int timeout_count = 0;
    uint8_t status;
    do {
        vTaskDelay(pdMS_TO_TICKS(2));
        status = wait_finish();
        timeout_count++;
        if (timeout_count > 100) {
            ESP_LOGW(TAG, "Sensor conversion timeout, last status: 0x%02X", status);
            return;
        }
    } while (status != 0x01);
    
    ret = read_continuous(0x06, sensor_data, 5);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read sensor data: %s", esp_err_to_name(ret));
        return;
    }
    
    calculate_press();
    
    ESP_LOGI(TAG, "Pressure: %.2f kPa, Temperature: %.2f °C", current_pressure, current_temperature);

    // update properties
    device_update_property_float("pressure", current_pressure);
    device_update_property_float("temperature", current_temperature);
}

static void report_pressure_task(void)
{
    int32_t remaining_ms;
    int32_t delay_ms;
    while (1) {
        read_pressure_data();
        if(g_device_mode == MODE_WIFI){
            cJSON *root = cJSON_CreateObject();
            
            // 限制到4位小数
            double pressure_rounded = round(current_pressure * 10000.0) / 10000.0;
            double temperature_rounded = round(current_temperature * 10000.0) / 10000.0;
            
            cJSON_AddNumberToObject(root, "pressure", pressure_rounded);
            cJSON_AddNumberToObject(root, "temperature", temperature_rounded);
            
            cJSON_AddStringToObject(root, "method", "update");
            mqtt_publish(root);
        }
        // 分段延时，最多1秒生效延迟
        if(report_interval_ms>1000){
            remaining_ms = report_interval_ms;
            while (remaining_ms > 0) {
                delay_ms = remaining_ms > 1000 ? 1000 : remaining_ms;
                if(report_interval_ms < remaining_ms){
                    remaining_ms = report_interval_ms;
                }
                vTaskDelay(pdMS_TO_TICKS(delay_ms));
                remaining_ms -= delay_ms;
            }
        }else{
            vTaskDelay(pdMS_TO_TICKS(report_interval_ms));
        }
    }
}


void on_device_init(void)
{
    ESP_LOGI(TAG, "Initializing QIYA pressure sensor device");
    
    pressure_property.readable = true;
    pressure_property.writeable = false;
    strcpy(pressure_property.name, "pressure");
    pressure_property.value_type = PROPERTY_TYPE_FLOAT;
    pressure_property.value.float_value = current_pressure;
    
    temperature_property.readable = true;
    temperature_property.writeable = false;
    strcpy(temperature_property.name, "temperature");
    temperature_property.value_type = PROPERTY_TYPE_FLOAT;
    temperature_property.value.float_value = current_temperature;
    
    report_delay_ms_property.readable = true;
    report_delay_ms_property.writeable = true;
    strcpy(report_delay_ms_property.name, "report_delay_ms");
    report_delay_ms_property.value_type = PROPERTY_TYPE_INT;
    report_delay_ms_property.value.int_value = report_interval_ms;
    
    pressure_sensor_init();
    
    xTaskCreate(report_pressure_task, "pressure_report", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "QIYA device initialized successfully");
}

void on_device_before_sleep(void)
{
    ESP_LOGI(TAG, "Preparing pressure sensor for sleep");
}
