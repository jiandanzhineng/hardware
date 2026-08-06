#include "device_serial_debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "base_device.h"
#include "device_identity.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define SERIAL_DEBUG_UART              CONFIG_ESP_CONSOLE_UART_NUM
#define SERIAL_DEBUG_RX_BUFFER_SIZE    2048
#define SERIAL_DEBUG_TX_BUFFER_SIZE    4096
#define SERIAL_DEBUG_MAX_LINE_SIZE     1024
#define SERIAL_DEBUG_TX_QUEUE_DEPTH    16

typedef enum {
    SERIAL_DEBUG_CLOSED = 0,
    SERIAL_DEBUG_STARTING,
    SERIAL_DEBUG_ACTIVE,
} serial_debug_state_t;

typedef struct {
    char *data;
    size_t len;
} serial_tx_item_t;

static const char *TAG = "SERIAL_DEBUG";
static QueueHandle_t s_tx_queue;
static SemaphoreHandle_t s_output_mutex;
static TaskHandle_t s_tx_task;
static TaskHandle_t s_rx_task;
static vprintf_like_t s_original_log_vprintf = vprintf;
static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static serial_debug_state_t s_state = SERIAL_DEBUG_CLOSED;
static bool s_initialized;

static serial_debug_state_t serial_debug_get_state(void)
{
    serial_debug_state_t state;
    portENTER_CRITICAL(&s_state_mux);
    state = s_state;
    portEXIT_CRITICAL(&s_state_mux);
    return state;
}

static void serial_debug_set_state(serial_debug_state_t state)
{
    portENTER_CRITICAL(&s_state_mux);
    s_state = state;
    portEXIT_CRITICAL(&s_state_mux);
}

bool device_serial_debug_is_active(void)
{
    return serial_debug_get_state() == SERIAL_DEBUG_ACTIVE;
}

static int serial_debug_log_vprintf(const char *format, va_list args)
{
    if (!s_output_mutex || !s_original_log_vprintf) {
        return vprintf(format, args);
    }

    xSemaphoreTakeRecursive(s_output_mutex, portMAX_DELAY);
    int result = s_original_log_vprintf(format, args);
    xSemaphoreGiveRecursive(s_output_mutex);
    return result;
}

static esp_err_t serial_debug_queue_frame(const char *data, size_t len)
{
    if (!data || len == 0 || !s_tx_queue) {
        return ESP_ERR_INVALID_STATE;
    }

    serial_tx_item_t item = {
        .data = malloc(len),
        .len = len,
    };
    if (!item.data) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(item.data, data, len);
    if (xQueueSend(s_tx_queue, &item, 0) != pdTRUE) {
        free(item.data);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t serial_debug_queue_control(const char *line)
{
    return serial_debug_queue_frame(line, strlen(line));
}

static esp_err_t serial_debug_send_control_sync(const char *line)
{
    if (!line || !s_output_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t len = strlen(line);
    xSemaphoreTakeRecursive(s_output_mutex, portMAX_DELAY);
    int written = uart_write_bytes(SERIAL_DEBUG_UART, line, len);
    esp_err_t err = written == (int)len
                        ? uart_wait_tx_done(SERIAL_DEBUG_UART, pdMS_TO_TICKS(2000))
                        : ESP_FAIL;
    xSemaphoreGiveRecursive(s_output_mutex);
    return err;
}

static esp_err_t serial_debug_send_ready(bool synchronous)
{
    char identity[DEVICE_IDENTITY_JSON_MAX_LEN];
    esp_err_t err = device_identity_write_json(identity, sizeof(identity), NULL);
    if (err != ESP_OK) {
        return err;
    }

    char frame[DEVICE_IDENTITY_JSON_MAX_LEN + sizeof("@DEBUG READY \r\n")];
    int frame_len = snprintf(frame, sizeof(frame), "@DEBUG READY %s\r\n", identity);
    if (frame_len < 0 || (size_t)frame_len >= sizeof(frame)) {
        return ESP_ERR_INVALID_SIZE;
    }

    return synchronous
               ? serial_debug_send_control_sync(frame)
               : serial_debug_queue_control(frame);
}

esp_err_t device_serial_debug_send_payload(const char *payload, size_t len)
{
    static const char prefix[] = "@MSG ";
    static const char suffix[] = "\r\n";

    if (!device_serial_debug_is_active()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!payload || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t frame_len = sizeof(prefix) - 1 + len + sizeof(suffix) - 1;
    char *frame = malloc(frame_len);
    if (!frame) {
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    memcpy(frame + offset, prefix, sizeof(prefix) - 1);
    offset += sizeof(prefix) - 1;
    memcpy(frame + offset, payload, len);
    offset += len;
    memcpy(frame + offset, suffix, sizeof(suffix) - 1);

    serial_tx_item_t item = {
        .data = frame,
        .len = frame_len,
    };
    if (xQueueSend(s_tx_queue, &item, 0) != pdTRUE) {
        free(frame);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void serial_debug_tx_task(void *arg)
{
    (void)arg;
    serial_tx_item_t item;

    while (true) {
        if (xQueueReceive(s_tx_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        xSemaphoreTakeRecursive(s_output_mutex, portMAX_DELAY);
        int written = uart_write_bytes(SERIAL_DEBUG_UART, item.data, item.len);
        if (written >= 0) {
            uart_wait_tx_done(SERIAL_DEBUG_UART, pdMS_TO_TICKS(2000));
        }
        xSemaphoreGiveRecursive(s_output_mutex);

        free(item.data);
    }
}

static void serial_debug_start_session(void)
{
    serial_debug_state_t state = serial_debug_get_state();
    if (state == SERIAL_DEBUG_ACTIVE) {
        esp_err_t err = serial_debug_send_ready(false);
        if (err == ESP_OK) {
            device_report_all_properties();
        } else {
            ESP_LOGE(TAG, "Failed to resend serial identity: %s", esp_err_to_name(err));
        }
        return;
    }
    if (state != SERIAL_DEBUG_CLOSED) {
        return;
    }

    serial_debug_set_state(SERIAL_DEBUG_STARTING);
    esp_err_t err = device_first_ready();
    if (err != ESP_OK) {
        serial_debug_set_state(SERIAL_DEBUG_CLOSED);
        ESP_LOGE(TAG, "Device first-ready failed: %s", esp_err_to_name(err));
        return;
    }
    err = uart_flush_input(SERIAL_DEBUG_UART);
    if (err != ESP_OK) {
        serial_debug_set_state(SERIAL_DEBUG_CLOSED);
        ESP_LOGE(TAG, "Failed to flush serial input: %s", esp_err_to_name(err));
        return;
    }
    err = serial_debug_send_ready(true);
    if (err != ESP_OK) {
        serial_debug_set_state(SERIAL_DEBUG_CLOSED);
        ESP_LOGE(TAG, "Failed to start serial debug session: %s", esp_err_to_name(err));
        return;
    }
    serial_debug_set_state(SERIAL_DEBUG_ACTIVE);
    device_report_all_properties();
}

static bool serial_debug_process_line(char *line)
{
    static const char start_command[] = "@DEBUG START";
    static const char command_prefix[] = "@CMD ";

    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r') {
        line[--len] = '\0';
    }
    if (len == 0) {
        return false;
    }

    if (strcmp(line, start_command) == 0) {
        bool starting = serial_debug_get_state() == SERIAL_DEBUG_CLOSED;
        serial_debug_start_session();
        return starting;
    }

    if (!device_serial_debug_is_active()) {
        return false;
    }

    if (strncmp(line, command_prefix, sizeof(command_prefix) - 1) == 0) {
        char *payload = line + sizeof(command_prefix) - 1;
        size_t payload_len = len - (sizeof(command_prefix) - 1);
        if (payload_len > 0) {
            device_handle_receive("serial", 6, payload, payload_len);
        }
    }
    return false;
}

static void serial_debug_rx_task(void *arg)
{
    (void)arg;
    uint8_t rx_data[128];
    char line[SERIAL_DEBUG_MAX_LINE_SIZE + 1];
    size_t line_len = 0;
    bool discarding = false;

    while (true) {
        int read_len = uart_read_bytes(
            SERIAL_DEBUG_UART,
            rx_data,
            sizeof(rx_data),
            pdMS_TO_TICKS(100));
        if (read_len <= 0) {
            continue;
        }

        for (int i = 0; i < read_len; i++) {
            char ch = (char)rx_data[i];
            if (ch == '\n') {
                if (!discarding) {
                    line[line_len] = '\0';
                    bool session_started = serial_debug_process_line(line);
                    if (session_started) {
                        line_len = 0;
                        discarding = false;
                        break;
                    }
                }
                line_len = 0;
                discarding = false;
                continue;
            }

            if (discarding) {
                continue;
            }
            if (ch == '\0') {
                line_len = 0;
                discarding = true;
                continue;
            }
            if (line_len >= SERIAL_DEBUG_MAX_LINE_SIZE) {
                line_len = 0;
                discarding = true;
                continue;
            }
            line[line_len++] = ch;
        }
    }
}

esp_err_t device_serial_debug_init(void)
{
    esp_err_t err;

    portENTER_CRITICAL(&s_state_mux);
    if (s_initialized) {
        portEXIT_CRITICAL(&s_state_mux);
        return ESP_OK;
    }
    portEXIT_CRITICAL(&s_state_mux);

    uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    s_output_mutex = xSemaphoreCreateRecursiveMutex();
    s_tx_queue = xQueueCreate(SERIAL_DEBUG_TX_QUEUE_DEPTH, sizeof(serial_tx_item_t));
    if (!s_output_mutex || !s_tx_queue) {
        goto no_mem;
    }

    err = uart_param_config(SERIAL_DEBUG_UART, &uart_config);
    if (err != ESP_OK) {
        goto cleanup_objects;
    }
    err = uart_driver_install(
        SERIAL_DEBUG_UART,
        SERIAL_DEBUG_RX_BUFFER_SIZE,
        SERIAL_DEBUG_TX_BUFFER_SIZE,
        0,
        NULL,
        0);
    if (err != ESP_OK) {
        goto cleanup_objects;
    }

    esp_vfs_dev_uart_use_driver(SERIAL_DEBUG_UART);
    s_original_log_vprintf = esp_log_set_vprintf(serial_debug_log_vprintf);

    if (xTaskCreate(serial_debug_tx_task, "serial_debug_tx", 3072, NULL, 8, &s_tx_task) != pdPASS ||
        xTaskCreate(serial_debug_rx_task, "serial_debug_rx", 4096, NULL, 8, &s_rx_task) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto cleanup_driver;
    }

    portENTER_CRITICAL(&s_state_mux);
    s_initialized = true;
    portEXIT_CRITICAL(&s_state_mux);

    ESP_LOGI(TAG, "Serial debug listener ready on UART%d", SERIAL_DEBUG_UART);
    return ESP_OK;

cleanup_driver:
    if (s_rx_task) {
        vTaskDelete(s_rx_task);
        s_rx_task = NULL;
    }
    if (s_tx_task) {
        vTaskDelete(s_tx_task);
        s_tx_task = NULL;
    }
    esp_log_set_vprintf(s_original_log_vprintf);
    esp_vfs_dev_uart_use_nonblocking(SERIAL_DEBUG_UART);
    uart_driver_delete(SERIAL_DEBUG_UART);
cleanup_objects:
    if (s_tx_queue) {
        vQueueDelete(s_tx_queue);
        s_tx_queue = NULL;
    }
    if (s_output_mutex) {
        vSemaphoreDelete(s_output_mutex);
        s_output_mutex = NULL;
    }
    return err;

no_mem:
    err = ESP_ERR_NO_MEM;
    goto cleanup_objects;
}
