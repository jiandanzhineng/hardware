#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "ota_update.h"
#include "smqtt.h" // For reporting status

// Extern from smqtt
extern esp_mqtt_client_handle_t smqtt_client;
extern char publish_topic[32];

static const char *TAG = "OTA_UPDATE";
static portMUX_TYPE s_ota_state_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_ota_in_progress = false;
static int s_ota_progress = 0;

static bool ota_try_begin(void) {
    bool can_begin = false;

    portENTER_CRITICAL(&s_ota_state_mux);
    if (!s_ota_in_progress) {
        s_ota_in_progress = true;
        s_ota_progress = 0;
        can_begin = true;
    }
    portEXIT_CRITICAL(&s_ota_state_mux);

    return can_begin;
}

static void ota_set_progress(int progress) {
    if (progress < 0) progress = 0;
    if (progress > 100) progress = 100;

    portENTER_CRITICAL(&s_ota_state_mux);
    s_ota_progress = progress;
    portEXIT_CRITICAL(&s_ota_state_mux);
}

static int ota_get_progress(void) {
    int progress = 0;

    portENTER_CRITICAL(&s_ota_state_mux);
    progress = s_ota_progress;
    portEXIT_CRITICAL(&s_ota_state_mux);

    return progress;
}

static void ota_end(void) {
    portENTER_CRITICAL(&s_ota_state_mux);
    s_ota_in_progress = false;
    s_ota_progress = 0;
    portEXIT_CRITICAL(&s_ota_state_mux);
}

// Helper to report status via MQTT
static void report_ota_status(const char *status, int progress, const char *msg) {
    cJSON *root = cJSON_CreateObject();
    if (root) {
        cJSON_AddStringToObject(root, "method", "ota_status");
        cJSON_AddStringToObject(root, "status", status);
        cJSON_AddNumberToObject(root, "progress", progress);
        if (msg) cJSON_AddStringToObject(root, "msg", msg);
        
        char *json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
            ESP_LOGI(TAG, "OTA Status Report: %s", json_str);
            // TODO: Integrate with actual MQTT publish if available
            // extern void smqtt_publish_data(const char *data); 
             esp_mqtt_client_publish(smqtt_client, publish_topic, json_str, 0, 1, 0);
            free(json_str);
        }
        cJSON_Delete(root);
    }
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            break;
    }
    return ESP_OK;
}

static void ota_task_entry(void *pvParameter) {
    char *url = (char *)pvParameter;
    ESP_LOGI(TAG, "Starting OTA update task from %s", url);
    ota_set_progress(0);
    report_ota_status("start", 0, "Starting update");

    esp_http_client_config_t config = {
        .url = url,
        //.cert_pem = (char *)server_cert_pem_start, // Add certificate if HTTPS
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = _http_event_handler,
        .keep_alive_enable = true,
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .skip_cert_common_name_check = false,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (err != ESP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "OTA begin failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed: %s (0x%x)", esp_err_to_name(err), err);
        report_ota_status("failed", 0, msg);
        ota_end();
        free(url);
        vTaskDelete(NULL);
        return;
    }

    esp_app_desc_t app_desc;
    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
    if (err != ESP_OK) {
        char msg[64];
        snprintf(msg, sizeof(msg), "Get image desc failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "esp_https_ota_get_img_desc failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(https_ota_handle);
        report_ota_status("failed", 0, msg);
        ota_end();
        free(url);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "New firmware version: %s", app_desc.version);

    int last_progress = -1;
    int last_progress_value = 0;
    while (1) {
        err = esp_https_ota_perform(https_ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        int read_len = esp_https_ota_get_image_len_read(https_ota_handle);
        int total_len = esp_https_ota_get_image_size(https_ota_handle);
        int progress = (total_len > 0) ? (read_len * 100 / total_len) : 0;
        last_progress_value = progress;
        ota_set_progress(progress);
        
        if (progress != last_progress && progress % 10 == 0) {
            ESP_LOGI(TAG, "OTA Progress: %d%%", progress);
            report_ota_status("downloading", progress, NULL);
            last_progress = progress;
        }
    }

    int read_len = esp_https_ota_get_image_len_read(https_ota_handle);
    int total_len = esp_https_ota_get_image_size(https_ota_handle);
    if (total_len > 0) {
        last_progress_value = read_len * 100 / total_len;
    }
    ota_set_progress(last_progress_value);

    if (err != ESP_OK) {
        char msg[80];
        snprintf(msg, sizeof(msg), "OTA perform failed: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "OTA perform failed: %s (0x%x), read=%d/%d, progress=%d%%",
                 esp_err_to_name(err), err, read_len, total_len, last_progress_value);
        esp_https_ota_abort(https_ota_handle);
        report_ota_status("failed", last_progress_value, msg);
        ota_end();
        free(url);
        vTaskDelete(NULL);
        return;
    }

    esp_err_t finish_err = esp_https_ota_finish(https_ota_handle);
    if (finish_err == ESP_OK) {
        ESP_LOGI(TAG, "OTA upgrade successful. Rebooting...");
        ota_set_progress(100);
        report_ota_status("success", 100, "Rebooting");
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        ota_end();
        esp_restart();
    } else {
        char msg[80];
        snprintf(msg, sizeof(msg), "OTA finish failed: %s", esp_err_to_name(finish_err));
        ESP_LOGE(TAG, "OTA finish failed: %s (0x%x), read=%d/%d, progress=%d%%",
                 esp_err_to_name(finish_err), finish_err, read_len, total_len, last_progress_value);
        report_ota_status("failed", last_progress_value, msg);
        ota_end();
    }
    
    free(url);
    vTaskDelete(NULL);
}

void ota_perform_update(const char *url) {
    if (url == NULL || url[0] == '\0') {
        ESP_LOGE(TAG, "OTA update command missing URL");
        report_ota_status("failed", 0, "Missing URL");
        return;
    }

    if (!ota_try_begin()) {
        ESP_LOGW(TAG, "OTA update ignored: already in progress");
        report_ota_status("busy", ota_get_progress(), "OTA already in progress");
        return;
    }

    char *url_copy = strdup(url);
    if (url_copy) {
        BaseType_t ret = xTaskCreate(ota_task_entry, "ota_task", 8192, url_copy, 5, NULL);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create OTA task");
            report_ota_status("failed", 0, "Failed to create OTA task");
            free(url_copy);
            ota_end();
        }
    } else {
        ESP_LOGE(TAG, "Failed to allocate memory for URL");
        report_ota_status("failed", 0, "Failed to allocate URL");
        ota_end();
    }
}

void ota_mark_app_valid(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Current running partition is pending verification. Marking as valid.");
            esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "App marked as valid, rollback cancelled.");
            } else {
                ESP_LOGE(TAG, "Failed to mark app as valid: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGI(TAG, "Current partition state: %d, no need to verify.", ota_state);
        }
    } else {
        ESP_LOGW(TAG, "Failed to get state of running partition.");
    }
}
