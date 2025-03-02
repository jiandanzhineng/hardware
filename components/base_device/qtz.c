#include "esp_log.h"
#include "iot_button.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "device_common.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "driver/timer.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "nvs_flash.h"
#include "nvs.h"

#define Trig 6
#define Echo 7
#define GPIO_INPUT_IO_0 Echo
#define GPIO_INPUT_PIN_SEL ((1ULL << GPIO_INPUT_IO_0))

#define ESP_INTR_FLAG_DEFAULT 0

static const char *TAG = "QTZ";
static const char *HCSR04TAG = "HCSR04";

device_property_t distance_property;
device_property_t report_delay_ms_property;
// device_property_t inout_divider_property;
device_property_t low_band_property;
device_property_t high_band_property;
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
};
int device_properties_num = sizeof(device_properties) / sizeof(device_properties[0]);

extern void get_property(char *property_name, int msg_id);
extern void mqtt_publish(cJSON *root);

uint64_t timer_counter_value = 0;
uint64_t timer_counter_update = 0;
float length_mm = 0;
float average_length_mm = 0;

int checking = 0;
int rising = 0;
int falling = 0;
int check_finish = 0;

static QueueHandle_t gpio_evt_queue = NULL;

int32_t inout_divider = 105;
int32_t low_band = 60; // trigger event if distance is less than low_band
int32_t high_band = 150;  // trigger event if distance is more than high_band

int low_state_flag = 0;
int high_state_flag = 0;


void hcsr04_timer_init(void);
void hcsr04_delay_us(uint16_t us);

static void report_distance_task(void);
static void check_distance_task(void);

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

static void check_distance_task(void)
{
    // report distance every report_delay_ms
    int fail_num = 0;
    while (1)
    {
        if (checking && !check_finish)
        {
            fail_num++;
            if (fail_num > 15)
            {
                ESP_LOGE(HCSR04TAG, "check fail");
                check_finish = 1;
                checking = 0;
                fail_num = 0;
            }
        }
        else
        {
            // ESP_LOGI(TAG, "start check_distance_task");
            gpio_set_level(Trig, 0);
            // hcsr04_delay_us(20);
            vTaskDelay(0.02 / portTICK_PERIOD_MS);
            gpio_set_level(Trig, 1); // 然后拉高Trig至少10us以上
            vTaskDelay(0.01 / portTICK_PERIOD_MS);
            gpio_set_level(Trig, 0); // 再拉低Trig，完成一次声波发出信号
            checking = 1;
            check_finish = 0;
            rising = 0;
            falling = 0;
            fail_num = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    if (checking && !check_finish)
    {
        if (!rising)
        {
            timer_get_counter_value(0, 0, &timer_counter_value);
            rising = 1;
        }
        else if (!falling)
        {
            timer_get_counter_value(0, 0, &timer_counter_update);
            falling = 1;
            xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
        }
    }
}

static void gpio_task_example(void *arg)
{
    uint32_t io_num;
    for (;;)
    {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY))
        {
            if (timer_counter_update - timer_counter_value < 130000)
            {                                                                                                // 131989 value for error
                distance_property.value.float_value = 0.0425 * (timer_counter_update - timer_counter_value); // counter * 340000 / 4000000 / 2
                average_length_mm = 0.93 * average_length_mm + 0.07 * distance_property.value.float_value;
                // ESP_LOGE(HCSR04TAG, "length: %.2f mm average:%.2f mm", distance_property.value.float_value, average_length_mm);
                update_in_state();
            }
            else
            {
                // ESP_LOGE(HCSR04TAG, "length: error");
            }
            vTaskDelay(pdMS_TO_TICKS(1));
            check_finish = 1;
            checking = 0;
        }
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

    // create gpio button
    button_config_t gpio_btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = 9,
            .active_level = 0,
        },
    };
    button_handle_t gpio_btn = iot_button_create(&gpio_btn_cfg);
    if (NULL == gpio_btn)
    {
        ESP_LOGE(TAG, "Button create failed");
    }

    // 设置Trig引脚为输出模式 Echo引脚为输入模式//
    esp_rom_gpio_pad_select_gpio(Trig);
    gpio_set_direction(Trig, GPIO_MODE_OUTPUT);

    gpio_config_t io_conf = {};
    // interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    // bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    // set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    // enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    // change gpio interrupt type for one pin
    gpio_set_intr_type(GPIO_INPUT_IO_0, GPIO_INTR_ANYEDGE);

    // create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    // xTaskCreate(test_task, "test_task", 2048, NULL, 10, NULL);

    // start gpio task
    xTaskCreate(gpio_task_example, "gpio_task_example", 2048, NULL, 10, NULL);

    // install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    // hook isr handler for specific gpio pin
    gpio_isr_handler_add(GPIO_INPUT_IO_0, gpio_isr_handler, (void *)GPIO_INPUT_IO_0);

    hcsr04_timer_init(); // 初始化

    // start distance task
    xTaskCreate(check_distance_task, "check_distance_task", 1024 * 2, NULL, 10, NULL);

    nvs0_init();
    nvs0_read();
    // inout_divider_property.value.int_value = inout_divider;
    low_band_property.value.int_value = low_band;
    high_band_property.value.int_value = high_band;
}

// set 80m hz /20 ,4m hz tick
void hcsr04_timer_init(void)
{
#define TIMER_DIVIDER (20)                           //  Hardware timer clock divider
#define TIMER_SCALE (TIMER_BASE_CLK / TIMER_DIVIDER) // convert counter value to seconds, 80M hz
    int group = 0;
    int timer = 0;
    /* Select and initialize basic parameters of the timer */
    timer_config_t config = {
        .divider = TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_DIS,
        .auto_reload = TIMER_AUTORELOAD_DIS,
    }; // default clock source is APB
    timer_init(group, timer, &config);
    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    timer_set_counter_value(group, timer, 0);
    timer_start(group, timer);
}

void hcsr04_delay_us(uint16_t us)
{
    uint64_t timer_counter_value = 0;
    uint64_t timer_counter_update = 0;
    // uint32_t delay_ccount = 200 * us;

    timer_get_counter_value(0, 0, &timer_counter_value);
    timer_counter_update = timer_counter_value + (us << 2);
    do
    {
        timer_get_counter_value(0, 0, &timer_counter_value);
    } while (timer_counter_value < timer_counter_update);
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
