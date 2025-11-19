#include "smqtt.h"
#include "esp_system.h"
#include "esp_bt.h"

static const char *TAG = "SMQTT";
esp_mqtt_client_handle_t smqtt_client;
char publish_topic[32];
char recv_topic[32];

// Multiple MQTT broker URIs for fallback connection
// 1) fixed host: easysmart.local
// 2) dynamic host: gateway IP (built at runtime)
static char mqtt_gateway_uri[64] = {0};
static const char *mqtt_broker_uris[] = {
    "mqtt://easysmart.local",
    mqtt_gateway_uri
};
static int mqtt_broker_count = 0;
static int mqtt_broker_index = 0;
static esp_event_handler_instance_t ip_event_inst = NULL;

// Periodic start task state
static TaskHandle_t smqtt_start_task_handle = NULL;
static volatile bool smqtt_connecting = false;
static volatile bool smqtt_connected = false;

// Forward declaration for periodic task
static void smqtt_start_client_on_current_broker_task(void *arg);

static void ip_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(mqtt_gateway_uri, sizeof(mqtt_gateway_uri), "mqtt://%d.%d.%d.%d", IP2STR(&event->ip_info.gw));
        mqtt_broker_count = 2;
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP: gateway=%s, enable gateway broker", mqtt_gateway_uri);
    }
}

static void smqtt_start_client_on_current_broker_impl(void)
{
    ESP_LOGI(TAG, "Start MQTT on broker[%d]: %s", mqtt_broker_index, mqtt_broker_uris[mqtt_broker_index]);
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_broker_uris[mqtt_broker_index],
        // Disable auto reconnect to allow manual fallback switching
        .network.disable_auto_reconnect = true,
    };

    smqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(smqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(smqtt_client);
    smqtt_connecting = true;
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        smqtt_connecting = false;
        smqtt_connected = true;
        /*subscibe topic: '/all'*/
        msg_id = esp_mqtt_client_subscribe(client, "/all", 0);
        ESP_LOGI(TAG, "sent subscribe successful, topic is /all, msg_id=%d", msg_id);
        /*subscibe topic: /drecv/{mac} */
        msg_id = esp_mqtt_client_subscribe(client, recv_topic, 0);
        ESP_LOGI(TAG, "sent subscribe successful, topic is %s, msg_id=%d", recv_topic, msg_id);
        device_first_ready();
        if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
            esp_bt_controller_disable();
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        smqtt_connected = false;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        //ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        //printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        //printf("DATA=%.*s\r\n", event->data_len, event->data);
        mqtt_msg_process(event->topic, event->topic_len, event->data, event->data_len);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        smqtt_connecting = false;
        smqtt_connected = false;
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

// Periodic task: runs every 3 seconds; 
static void smqtt_start_client_on_current_broker_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(3000));

        // Skip starting if we are currently connecting or connected
        if (smqtt_connecting || smqtt_connected) {
            continue;
        }


        if (smqtt_client) {
            esp_mqtt_client_stop(smqtt_client);
            esp_mqtt_client_destroy(smqtt_client);
            smqtt_client = NULL;
        }
        int prev_index = mqtt_broker_index;
        mqtt_broker_index = (mqtt_broker_index + 1) % mqtt_broker_count;
        ESP_LOGW(TAG, "Switch MQTT broker from %d to %d", prev_index, mqtt_broker_index);
        smqtt_start_client_on_current_broker_impl();
    }
}

void mqtt_app_start(void)
{
    ESP_LOGI(TAG, "mqtt_app_start");
    smqtt_connecting = false;
    // Register IP event to update gateway URI once IP is acquired
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL, &ip_event_inst);
    // Start with only easysmart.local until we get GOT_IP
    mqtt_gateway_uri[0] = '\0';
    mqtt_broker_count = 1;
    // init topic
    sprintf(publish_topic, "/dpub/%s", macStr);
    sprintf(recv_topic, "/drecv/%s", macStr);
    // Launch periodic start task (runs every 5s, +1s if connected or connecting)
    if (smqtt_start_task_handle == NULL) {
        xTaskCreate(smqtt_start_client_on_current_broker_task, "smqtt_start_task", 4096, NULL, 5, &smqtt_start_task_handle);
    }
}

