#include "esp_log.h"
#include "iot_button.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "device_common.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "driver/i2c.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "nvs_flash.h"
#include "nvs.h"

// I2C配置
#define I2C_MASTER_SCL_IO           5    // SCL引脚
#define I2C_MASTER_SDA_IO           4    // SDA引脚
#define I2C_MASTER_NUM              0    // I2C端口号
#define I2C_MASTER_FREQ_HZ          400000  // I2C频率
#define I2C_MASTER_TX_BUF_DISABLE   0    // I2C主机不需要缓冲区
#define I2C_MASTER_RX_BUF_DISABLE   0    // I2C主机不需要缓冲区
#define I2C_MASTER_TIMEOUT_MS       1000

// VL6180X传感器地址和寄存器定义
#define VL6180X_ADDRESS             0x29

// 按钮GPIO定义
#define BUTTON0_GPIO                2
#define BUTTON1_GPIO                3

static const char *TAG = "QTZ";
static const char *VL6180X_TAG = "VL6180X";
static const char *BUTTON_TAG = "BUTTON";

// 缩放值
static const uint8_t ScalerValues[] = {0, 253, 127, 84};

// 寄存器地址定义
#define IDENTIFICATION__MODEL_ID              0x000
#define IDENTIFICATION__MODEL_REV_MAJOR       0x001
#define IDENTIFICATION__MODEL_REV_MINOR       0x002
#define IDENTIFICATION__MODULE_REV_MAJOR      0x003
#define IDENTIFICATION__MODULE_REV_MINOR      0x004
#define IDENTIFICATION__DATE_HI               0x006
#define IDENTIFICATION__DATE_LO               0x007
#define IDENTIFICATION__TIME                  0x008

#define SYSTEM__MODE_GPIO0                    0x010
#define SYSTEM__MODE_GPIO1                    0x011
#define SYSTEM__HISTORY_CTRL                  0x012
#define SYSTEM__INTERRUPT_CONFIG_GPIO         0x014
#define SYSTEM__INTERRUPT_CLEAR               0x015
#define SYSTEM__FRESH_OUT_OF_RESET            0x016
#define SYSTEM__GROUPED_PARAMETER_HOLD        0x017

#define SYSRANGE__START                       0x018
#define SYSRANGE__THRESH_HIGH                 0x019
#define SYSRANGE__THRESH_LOW                  0x01A
#define SYSRANGE__INTERMEASUREMENT_PERIOD     0x01B
#define SYSRANGE__MAX_CONVERGENCE_TIME        0x01C
#define SYSRANGE__CROSSTALK_COMPENSATION_RATE 0x01E
#define SYSRANGE__CROSSTALK_VALID_HEIGHT      0x021
#define SYSRANGE__EARLY_CONVERGENCE_ESTIMATE  0x022
#define SYSRANGE__PART_TO_PART_RANGE_OFFSET   0x024
#define SYSRANGE__RANGE_IGNORE_VALID_HEIGHT   0x025
#define SYSRANGE__RANGE_IGNORE_THRESHOLD      0x026
#define SYSRANGE__MAX_AMBIENT_LEVEL_MULT      0x02C
#define SYSRANGE__RANGE_CHECK_ENABLES         0x02D
#define SYSRANGE__VHV_RECALIBRATE             0x02E
#define SYSRANGE__VHV_REPEAT_RATE             0x031

#define SYSALS__START                         0x038
#define SYSALS__THRESH_HIGH                   0x03A
#define SYSALS__THRESH_LOW                    0x03C
#define SYSALS__INTERMEASUREMENT_PERIOD       0x03E
#define SYSALS__ANALOGUE_GAIN                 0x03F
#define SYSALS__INTEGRATION_PERIOD            0x040

#define RESULT__RANGE_STATUS                  0x04D
#define RESULT__ALS_STATUS                    0x04E
#define RESULT__INTERRUPT_STATUS_GPIO         0x04F
#define RESULT__ALS_VAL                       0x050
#define RESULT__HISTORY_BUFFER_0              0x052
#define RESULT__HISTORY_BUFFER_1              0x054
#define RESULT__HISTORY_BUFFER_2              0x056
#define RESULT__HISTORY_BUFFER_3              0x058
#define RESULT__HISTORY_BUFFER_4              0x05A
#define RESULT__HISTORY_BUFFER_5              0x05C
#define RESULT__HISTORY_BUFFER_6              0x05E
#define RESULT__HISTORY_BUFFER_7              0x060
#define RESULT__RANGE_VAL                     0x062
#define RESULT__RANGE_RAW                     0x064
#define RESULT__RANGE_RETURN_RATE             0x066
#define RESULT__RANGE_REFERENCE_RATE          0x068
#define RESULT__RANGE_RETURN_SIGNAL_COUNT     0x06C
#define RESULT__RANGE_REFERENCE_SIGNAL_COUNT  0x070
#define RESULT__RANGE_RETURN_AMB_COUNT        0x074
#define RESULT__RANGE_REFERENCE_AMB_COUNT     0x078
#define RESULT__RANGE_RETURN_CONV_TIME        0x07C
#define RESULT__RANGE_REFERENCE_CONV_TIME     0x080

#define RANGE_SCALER                          0x096

#define READOUT__AVERAGING_SAMPLE_PERIOD      0x10A
#define FIRMWARE__BOOTUP                      0x119
#define FIRMWARE__RESULT_SCALER               0x120
#define I2C_SLAVE__DEVICE_ADDRESS             0x212
#define INTERLEAVED_MODE__ENABLE              0x2A3

// VL6180X传感器结构体
typedef struct {
    uint8_t scaling;
    uint8_t ptp_offset;
    uint32_t io_timeout;
    bool did_timeout;
} vl6180x_sensor_t;

static vl6180x_sensor_t sensor = {0};

// 按钮句柄
static button_handle_t button0_handle = NULL;
static button_handle_t button1_handle = NULL;

device_property_t distance_property;
device_property_t report_delay_ms_property;
// device_property_t inout_divider_property;
device_property_t low_band_property;
device_property_t high_band_property;
device_property_t button0_property;
device_property_t button1_property;
extern device_property_t device_type_property;
extern device_property_t sleep_time_property;
device_property_t *device_properties[] = {
    &device_type_property,
    &distance_property,
    &report_delay_ms_property,
    &sleep_time_property,
    // &inout_divider_property,
    &low_band_property,
    &high_band_property,
    &button0_property,
    &button1_property,
};
int device_properties_num = sizeof(device_properties) / sizeof(device_properties[0]);

extern void get_property(char *property_name, int msg_id);
extern void mqtt_publish(cJSON *root);

float average_length_mm = 0;

int32_t low_band = 60; // trigger event if distance is less than low_band
int32_t high_band = 150;  // trigger event if distance is more than high_band

int low_state_flag = 0;
int high_state_flag = 0;

// VL6180X函数声明
static esp_err_t vl6180x_write_reg16(uint16_t reg_addr, uint8_t data);
static esp_err_t vl6180x_read_reg16(uint16_t reg_addr, uint8_t *data);
static esp_err_t vl6180x_write_reg16_16(uint16_t reg_addr, uint16_t data);
static esp_err_t vl6180x_read_reg16_16(uint16_t reg_addr, uint16_t *data);
static esp_err_t i2c_master_init(void);
static esp_err_t vl6180x_init(void);
static esp_err_t vl6180x_set_scaling(uint8_t new_scaling);
static esp_err_t vl6180x_configure_default(void);
static uint8_t vl6180x_read_range_continuous(void);
static uint8_t vl6180x_read_range_single(void);
static uint16_t vl6180x_read_range_single_millimeters(void);
static void vl6180x_set_timeout(uint32_t timeout);
static bool vl6180x_timeout_occurred(void);

static void report_distance_task(void);
static void check_distance_task(void);

// 按钮回调函数声明
static void button0_press_cb(void *arg, void *usr_data);
static void button0_release_cb(void *arg, void *usr_data);
static void button1_press_cb(void *arg, void *usr_data);
static void button1_release_cb(void *arg, void *usr_data);
static void init_buttons(void);

void nvs0_init(void);
void nvs0_read(void);
void nvs0_set(void);
void update_in_state(void);

void on_set_property(char *property_name, cJSON *property_value, int msg_id)
{
    ESP_LOGI(TAG, "on_set_property property_name:%s", property_name);
    if (strcmp(property_name, "low_band") == 0)
    {
        low_band = property_value->valueint;
        nvs0_set();
    }
    else if (strcmp(property_name, "high_band") == 0)
    {
        high_band = property_value->valueint;
        nvs0_set();
    }
}
// void on_device_init(void);
// void on_device_first_ready(void);
void on_action(cJSON *root)
{
}
void on_mqtt_msg_process(char *topic, cJSON *root)
{
}

void on_device_first_ready(void)
{
    ESP_LOGI(TAG, "device_first_ready");
    // start report distance task every report_delay_ms
    xTaskCreate(report_distance_task, "report_distance_task", 1024 * 2, NULL, 10, NULL);
}

// I2C写入16位寄存器
static esp_err_t vl6180x_write_reg16(uint16_t reg_addr, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL6180X_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, (reg_addr >> 8) & 0xFF, true);  // 高字节
    i2c_master_write_byte(cmd, reg_addr & 0xFF, true);         // 低字节
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

// I2C读取16位寄存器
static esp_err_t vl6180x_read_reg16(uint16_t reg_addr, uint8_t *data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL6180X_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, (reg_addr >> 8) & 0xFF, true);  // 高字节
    i2c_master_write_byte(cmd, reg_addr & 0xFF, true);         // 低字节
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL6180X_ADDRESS << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, data, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

// I2C写入16位寄存器（2字节数据）
static esp_err_t vl6180x_write_reg16_16(uint16_t reg_addr, uint16_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL6180X_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, (reg_addr >> 8) & 0xFF, true);  // 寄存器高字节
    i2c_master_write_byte(cmd, reg_addr & 0xFF, true);         // 寄存器低字节
    i2c_master_write_byte(cmd, (data >> 8) & 0xFF, true);      // 数据高字节
    i2c_master_write_byte(cmd, data & 0xFF, true);             // 数据低字节
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

// I2C读取16位寄存器（2字节数据）
static esp_err_t vl6180x_read_reg16_16(uint16_t reg_addr, uint16_t *data)
{
    uint8_t data_h, data_l;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL6180X_ADDRESS << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, (reg_addr >> 8) & 0xFF, true);  // 高字节
    i2c_master_write_byte(cmd, reg_addr & 0xFF, true);         // 低字节
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (VL6180X_ADDRESS << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &data_h, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &data_l, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) {
        *data = (data_h << 8) | data_l;
    }
    return ret;
}

// 初始化I2C
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

static void report_distance_task(void)
{
    // report distance every report_delay_ms
    int fail_num = 0;
    while (1)
    {
        get_property("distance", 0);
        vTaskDelay(pdMS_TO_TICKS(report_delay_ms_property.value.int_value));
    }
}

// VL6180X传感器初始化
static esp_err_t vl6180x_init(void)
{
    uint8_t fresh_out_of_reset;
    esp_err_t ret;
    
    // 读取part-to-part偏移量
    ret = vl6180x_read_reg16(SYSRANGE__PART_TO_PART_RANGE_OFFSET, &sensor.ptp_offset);
    if (ret != ESP_OK) {
        ESP_LOGE(VL6180X_TAG, "读取PTP偏移量失败");
        return ret;
    }
    
    // 检查是否首次复位
    ret = vl6180x_read_reg16(SYSTEM__FRESH_OUT_OF_RESET, &fresh_out_of_reset);
    if (ret != ESP_OK) {
        ESP_LOGE(VL6180X_TAG, "读取复位状态失败");
        return ret;
    }
    
    if (fresh_out_of_reset == 1) {
        sensor.scaling = 1;
        
        // 初始化寄存器设置
        vl6180x_write_reg16(0x207, 0x01);
        vl6180x_write_reg16(0x208, 0x01);
        vl6180x_write_reg16(0x096, 0x00);
        vl6180x_write_reg16(0x097, 0xFD);
        vl6180x_write_reg16(0x0E3, 0x01);
        vl6180x_write_reg16(0x0E4, 0x03);
        vl6180x_write_reg16(0x0E5, 0x02);
        vl6180x_write_reg16(0x0E6, 0x01);
        vl6180x_write_reg16(0x0E7, 0x03);
        vl6180x_write_reg16(0x0F5, 0x02);
        vl6180x_write_reg16(0x0D9, 0x05);
        vl6180x_write_reg16(0x0DB, 0xCE);
        vl6180x_write_reg16(0x0DC, 0x03);
        vl6180x_write_reg16(0x0DD, 0xF8);
        vl6180x_write_reg16(0x09F, 0x00);
        vl6180x_write_reg16(0x0A3, 0x3C);
        vl6180x_write_reg16(0x0B7, 0x00);
        vl6180x_write_reg16(0x0BB, 0x3C);
        vl6180x_write_reg16(0x0B2, 0x09);
        vl6180x_write_reg16(0x0CA, 0x09);
        vl6180x_write_reg16(0x198, 0x01);
        vl6180x_write_reg16(0x1B0, 0x17);
        vl6180x_write_reg16(0x1AD, 0x00);
        vl6180x_write_reg16(0x0FF, 0x05);
        vl6180x_write_reg16(0x100, 0x05);
        vl6180x_write_reg16(0x199, 0x05);
        vl6180x_write_reg16(0x1A6, 0x1B);
        vl6180x_write_reg16(0x1AC, 0x3E);
        vl6180x_write_reg16(0x1A7, 0x1F);
        vl6180x_write_reg16(0x030, 0x00);
        vl6180x_write_reg16(SYSTEM__FRESH_OUT_OF_RESET, 0);
    } else {
        uint16_t scaler_val;
        ret = vl6180x_read_reg16_16(RANGE_SCALER, &scaler_val);
        if (ret != ESP_OK) {
            ESP_LOGE(VL6180X_TAG, "读取缩放值失败");
            return ret;
        }
        
        if (scaler_val == ScalerValues[3]) {
            sensor.scaling = 3;
        } else if (scaler_val == ScalerValues[2]) {
            sensor.scaling = 2;
        } else {
            sensor.scaling = 1;
        }
        sensor.ptp_offset = sensor.ptp_offset * sensor.scaling;
    }
    
    return ESP_OK;
}

// 设置缩放
static esp_err_t vl6180x_set_scaling(uint8_t new_scaling)
{
    esp_err_t ret;
    sensor.scaling = new_scaling;
    
    ret = vl6180x_write_reg16_16(RANGE_SCALER, (0x00 << 8) | ScalerValues[new_scaling]);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16(SYSRANGE__PART_TO_PART_RANGE_OFFSET, sensor.ptp_offset / new_scaling);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16(SYSRANGE__CROSSTALK_VALID_HEIGHT, 20 / new_scaling);
    if (ret != ESP_OK) return ret;
    
    uint8_t rce;
    ret = vl6180x_read_reg16(SYSRANGE__RANGE_CHECK_ENABLES, &rce);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16(SYSRANGE__RANGE_CHECK_ENABLES, (rce & 0xFE) | (new_scaling == 1));
    return ret;
}

// 配置默认参数
static esp_err_t vl6180x_configure_default(void)
{
    esp_err_t ret;
    
    ret = vl6180x_write_reg16(READOUT__AVERAGING_SAMPLE_PERIOD, 0x30);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16(SYSALS__ANALOGUE_GAIN, 0x46);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16(SYSRANGE__VHV_REPEAT_RATE, 0xFF);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16_16(SYSALS__INTEGRATION_PERIOD, 0x0063);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16(SYSRANGE__VHV_RECALIBRATE, 0x01);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16(SYSRANGE__INTERMEASUREMENT_PERIOD, 0x09);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16(SYSALS__INTERMEASUREMENT_PERIOD, 0x31);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16(SYSTEM__INTERRUPT_CONFIG_GPIO, 0x24);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16(SYSRANGE__MAX_CONVERGENCE_TIME, 0x31);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_write_reg16(INTERLEAVED_MODE__ENABLE, 0);
    if (ret != ESP_OK) return ret;
    
    ret = vl6180x_set_scaling(1);
    return ret;
}

// 读取距离（连续模式）
static uint8_t vl6180x_read_range_continuous(void)
{
    uint32_t start_time = esp_timer_get_time() / 1000;  // 转换为毫秒
    uint8_t interrupt_status;
    
    do {
        esp_err_t ret = vl6180x_read_reg16(RESULT__INTERRUPT_STATUS_GPIO, &interrupt_status);
        if (ret != ESP_OK) {
            ESP_LOGE(VL6180X_TAG, "读取中断状态失败");
            return 255;
        }
        
        if (sensor.io_timeout > 0 && ((esp_timer_get_time() / 1000) - start_time) > sensor.io_timeout) {
            sensor.did_timeout = true;
            return 255;
        }
    } while ((interrupt_status & 0x07) != 0x04);
    
    uint8_t range;
    esp_err_t ret = vl6180x_read_reg16(RESULT__RANGE_VAL, &range);
    if (ret != ESP_OK) {
        ESP_LOGE(VL6180X_TAG, "读取距离值失败");
        return 255;
    }
    
    vl6180x_write_reg16(SYSTEM__INTERRUPT_CLEAR, 0x01);
    return range;
}

// 读取距离（单次模式）
static uint8_t vl6180x_read_range_single(void)
{
    vl6180x_write_reg16(SYSRANGE__START, 0x01);
    return vl6180x_read_range_continuous();
}

// 读取距离（毫米）
static uint16_t vl6180x_read_range_single_millimeters(void)
{
    return sensor.scaling * vl6180x_read_range_single();
}

// 设置超时
static void vl6180x_set_timeout(uint32_t timeout)
{
    sensor.io_timeout = timeout;
}

// 检查是否超时
static bool vl6180x_timeout_occurred(void)
{
    bool tmp = sensor.did_timeout;
    sensor.did_timeout = false;
    return tmp;
}

static void check_distance_task(void)
{
    // 使用VL6180X传感器持续读取距离
    while (1)
    {
        uint16_t distance = vl6180x_read_range_single_millimeters();
        
        if (vl6180x_timeout_occurred()) {
            ESP_LOGE(VL6180X_TAG, "传感器超时");
        } else {
            distance_property.value.float_value = (float)distance;
            average_length_mm = 0.7 * average_length_mm + 0.3 * distance_property.value.float_value;
            //ESP_LOGI(VL6180X_TAG, "距离: %.2f mm 平均:%.2f mm", distance_property.value.float_value, average_length_mm);
            update_in_state();
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms延迟
    }
}



void update_in_state(void)
{
    if(low_state_flag){
        if (average_length_mm > low_band + 10)
        {
            low_state_flag = 0;
            ESP_LOGI(TAG, "low_state_flag change to %d average_length_mm=%.2f", low_state_flag, average_length_mm);
        }
    }else{
        if (average_length_mm < low_band)
        {
            low_state_flag = 1;
            ESP_LOGI(TAG, "low_state_flag change to %d average_length_mm=%.2f", low_state_flag, average_length_mm);
                        cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "method", "low");
            mqtt_publish(root);
        }
    }

    if(high_state_flag){
        if (average_length_mm < high_band - 10)
        {
            high_state_flag = 0;
            ESP_LOGI(TAG, "high_state_flag change to %d average_length_mm=%.2f", high_state_flag, average_length_mm);
        }
    }else{
        if (average_length_mm > high_band)
        {
            high_state_flag = 1;
            ESP_LOGI(TAG, "high_state_flag change to %d average_length_mm=%.2f", high_state_flag, average_length_mm);
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "method", "high");
            mqtt_publish(root);
        }
    }
}

void report_all_properties(void)
{
    ESP_LOGI(TAG, "report_all_properties");
    // build json
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "method", "report");
    cJSON_AddStringToObject(root, "device_type", device_type_property.value.string_value);
    cJSON_AddNumberToObject(root, "distance", distance_property.value.float_value);
    cJSON_AddNumberToObject(root, "report_delay_ms", report_delay_ms_property.value.int_value);
    char *json_data = cJSON_Print(root);
    // publish
    esp_mqtt_client_publish(smqtt_client, publish_topic, json_data, 0, 1, 0);
    free(json_data);
    cJSON_Delete(root);
}

void on_device_init(void)
{
    ESP_LOGI(TAG, "device_init");
    // init distance, it is a int
    distance_property.readable = true;
    distance_property.writeable = false;
    strcpy(distance_property.name, "distance");
    distance_property.value_type = PROPERTY_TYPE_FLOAT;
    distance_property.value.float_value = 0;

    // init report_delay_ms, it is a int
    report_delay_ms_property.readable = true;
    report_delay_ms_property.writeable = true;
    strcpy(report_delay_ms_property.name, "report_delay_ms");
    report_delay_ms_property.value_type = PROPERTY_TYPE_INT;
    report_delay_ms_property.value.int_value = 10000;

    // init inout_divider, it is a int
    // inout_divider_property.readable = true;
    // inout_divider_property.writeable = true;
    // strcpy(inout_divider_property.name, "inout_divider");
    // inout_divider_property.value_type = PROPERTY_TYPE_INT;
    // inout_divider_property.value.int_value = inout_divider;

    // init low_band, it is a int
    low_band_property.readable = true;
    low_band_property.writeable = true;
    strcpy(low_band_property.name, "low_band");
    low_band_property.value_type = PROPERTY_TYPE_INT;
    low_band_property.value.int_value = low_band;

    // init high_band, it is a int
    high_band_property.readable = true;
    high_band_property.writeable = true;
    strcpy(high_band_property.name, "high_band");
    high_band_property.value_type = PROPERTY_TYPE_INT;
    high_band_property.value.int_value = high_band;

    // init button0, it is a int (0=released, 1=pressed)
    button0_property.readable = true;
    button0_property.writeable = false;
    strcpy(button0_property.name, "button0");
    button0_property.value_type = PROPERTY_TYPE_INT;
    button0_property.value.int_value = 0;

    // init button1, it is a int (0=released, 1=pressed)
    button1_property.readable = true;
    button1_property.writeable = false;
    strcpy(button1_property.name, "button1");
    button1_property.value_type = PROPERTY_TYPE_INT;
    button1_property.value.int_value = 0;

    // 初始化按钮
    init_buttons();

    // 初始化I2C
    ESP_ERROR_CHECK(i2c_master_init());
    ESP_LOGI(TAG, "I2C初始化完成");
    
    // 初始化VL6180X传感器
    ESP_ERROR_CHECK(vl6180x_init());
    ESP_LOGI(TAG, "VL6180X传感器初始化完成");
    
    // 配置默认参数
    ESP_ERROR_CHECK(vl6180x_configure_default());
    ESP_LOGI(TAG, "VL6180X传感器配置完成");
    
    // 设置缩放为3
    ESP_ERROR_CHECK(vl6180x_set_scaling(3));
    ESP_LOGI(TAG, "设置缩放为3");
    
    // 设置超时为500ms
    vl6180x_set_timeout(500);
    ESP_LOGI(TAG, "设置超时为500ms");

    // start distance task
    xTaskCreate(check_distance_task, "check_distance_task", 1024 * 2, NULL, 10, NULL);

    nvs0_init();
    nvs0_read();
    // inout_divider_property.value.int_value = inout_divider;
    low_band_property.value.int_value = low_band;
    high_band_property.value.int_value = high_band;
}



void nvs0_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void nvs0_read(void)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
    else
    {
        printf("Done\n");
        // Read
        printf("Reading restart counter from NVS ... ");
        // err = nvs_get_i32(my_handle, "inout_diveder", &inout_divider);
        err = nvs_get_i32(my_handle, "low_band", &low_band);
        err = nvs_get_i32(my_handle, "high_band", &high_band);
        switch (err)
        {
        case ESP_OK:
            printf("Done\n");
            printf("get low_band = %" PRIu32 "\n", low_band);
            printf("get high_band = %" PRIu32 "\n", high_band);
            break;
        case ESP_ERR_NVS_NOT_FOUND:
            printf("The value is not initialized yet!\n");
            break;
        default:
            printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
        nvs_close(my_handle);
    }
}

void nvs0_set(void)
{
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK)
    {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
    else
    {
        printf("Done\n");
        // Read
        printf("Write to NVS ... ");
        // err = nvs_set_i32(my_handle, "inout_diveder", inout_divider);
        err = nvs_set_i32(my_handle, "low_band", low_band);
        err = nvs_set_i32(my_handle, "high_band", high_band);
        printf((err != ESP_OK) ? "Failed!\n" : "Done\n");
        printf("Committing updates in NVS ... ");
        err = nvs_commit(my_handle);
        printf((err != ESP_OK) ? "Failed!\n" : "Done\n");
    }
    nvs_close(my_handle);
}

// 按钮0按下回调函数
static void button0_press_cb(void *arg, void *usr_data)
{
    ESP_LOGI(BUTTON_TAG, "Button 0 pressed");
    button0_property.value.int_value = 1;
    get_property("button0", 0);
}

// 按钮0释放回调函数
static void button0_release_cb(void *arg, void *usr_data)
{
    ESP_LOGI(BUTTON_TAG, "Button 0 released");
    button0_property.value.int_value = 0;
    get_property("button0", 0);
}

// 按钮1按下回调函数
static void button1_press_cb(void *arg, void *usr_data)
{
    ESP_LOGI(BUTTON_TAG, "Button 1 pressed");
    button1_property.value.int_value = 1;
    get_property("button1", 0);
}

// 按钮1释放回调函数
static void button1_release_cb(void *arg, void *usr_data)
{
    ESP_LOGI(BUTTON_TAG, "Button 1 released");
    button1_property.value.int_value = 0;
    get_property("button1", 0);
}

// 初始化按钮
static void init_buttons(void)
{
    // 配置按钮0
    button_config_t btn0_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,  // 长按时间1秒
        .short_press_time = 50,   // 短按时间50ms，用于消抖
        .gpio_button_config = {
            .gpio_num = BUTTON0_GPIO,
            .active_level = 0,  // 低电平有效
        },
    };
    
    button0_handle = iot_button_create(&btn0_cfg);
    if (button0_handle == NULL) {
        ESP_LOGE(BUTTON_TAG, "Button 0 create failed");
        return;
    }
    
    // 注册按钮0的按下和释放回调
    iot_button_register_cb(button0_handle, BUTTON_PRESS_DOWN, button0_press_cb, NULL);
    iot_button_register_cb(button0_handle, BUTTON_PRESS_UP, button0_release_cb, NULL);
    
    // 配置按钮1
    button_config_t btn1_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = 1000,  // 长按时间1秒
        .short_press_time = 50,   // 短按时间50ms，用于消抖
        .gpio_button_config = {
            .gpio_num = BUTTON1_GPIO,
            .active_level = 0,  // 低电平有效
        },
    };
    
    button1_handle = iot_button_create(&btn1_cfg);
    if (button1_handle == NULL) {
        ESP_LOGE(BUTTON_TAG, "Button 1 create failed");
        return;
    }
    
    // 注册按钮1的按下和释放回调
    iot_button_register_cb(button1_handle, BUTTON_PRESS_DOWN, button1_press_cb, NULL);
    iot_button_register_cb(button1_handle, BUTTON_PRESS_UP, button1_release_cb, NULL);
    
    ESP_LOGI(BUTTON_TAG, "Buttons initialized successfully");
}
