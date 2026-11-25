#include "smqtt.h"
#include "device_common.h"
#include "esp_system.h"
#include <inttypes.h>
#include <sys/lock.h>

static const char *TAG = "SMQTT";
esp_mqtt_client_handle_t smqtt_client = NULL;
char publish_topic[32];
char recv_topic[32];

#define SMQTT_RETRY_INTERVAL_MS    3000
#define SMQTT_RX_QUEUE_LEN         24
#define SMQTT_TASK_STACK_SIZE      6144
#define SMQTT_START_TASK_PRIORITY  4
#define SMQTT_MQTT_TASK_PRIORITY   5
#define SMQTT_STOP_RETRY_MAX       20
#define SMQTT_STOP_RETRY_DELAY_MS  10
#define SMQTT_POST_STOP_GRACE_MS   20

typedef enum {
    SMQTT_RX_WAKE = 0,
    SMQTT_RX_DATA = 1,
} smqtt_rx_type_t;

typedef struct {
    smqtt_rx_type_t type;
    uint32_t gen;
    char *topic;
    int topic_len;
    char *data;
    int data_len;
} smqtt_rx_item_t;

// Multiple MQTT broker URIs for fallback connection
// 1) fixed host: easysmart.local
// 2) dynamic host: gateway IP (built at runtime)
static char mqtt_gateway_uri[64] = {0};
static const char *mqtt_broker_uris[] = {
    "mqtt://easysmart.local",
    mqtt_gateway_uri
};
static int mqtt_broker_count = 1;
static int mqtt_broker_index = 0;
static esp_event_handler_instance_t ip_event_inst = NULL;
static bool ip_event_registered = false;

// Periodic start task and client lifecycle state
static _lock_t s_smqtt_lock = 0;
static StaticQueue_t s_smqtt_rx_queue_buf;
static uint8_t s_smqtt_rx_queue_storage[SMQTT_RX_QUEUE_LEN * sizeof(smqtt_rx_item_t)];
static QueueHandle_t s_smqtt_rx_queue = NULL;
static StaticSemaphore_t s_smqtt_pub_done_sem_buf;
static SemaphoreHandle_t s_smqtt_pub_done_sem = NULL;
static TaskHandle_t smqtt_start_task_handle = NULL;
static bool smqtt_running = false;
static uint32_t smqtt_generation = 0;
static uint32_t smqtt_client_generation = 0;
static uint32_t smqtt_last_started_gen = 0;
static bool smqtt_connected_action_pending = false;
static uint32_t smqtt_connected_action_gen = 0;
static int smqtt_client_active_pubs = 0;
static volatile bool smqtt_connecting = false;
volatile bool smqtt_connected = false;

// Forward declaration for periodic task
static void smqtt_start_client_on_current_broker_task(void *arg);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data);

__attribute__((weak)) void app_release_ble_resources(const char *reason)
{
    (void)reason;
}

static void smqtt_ensure_primitives_locked(void)
{
    if (s_smqtt_rx_queue == NULL) {
        s_smqtt_rx_queue = xQueueCreateStatic(
            SMQTT_RX_QUEUE_LEN,
            sizeof(smqtt_rx_item_t),
            s_smqtt_rx_queue_storage,
            &s_smqtt_rx_queue_buf);
    }
    if (s_smqtt_pub_done_sem == NULL) {
        s_smqtt_pub_done_sem = xSemaphoreCreateBinaryStatic(&s_smqtt_pub_done_sem_buf);
    }
}

static void smqtt_wake_worker_locked(void)
{
    if (smqtt_start_task_handle != NULL && s_smqtt_rx_queue != NULL) {
        smqtt_rx_item_t wake = { .type = SMQTT_RX_WAKE };
        // If queue is already full, the worker is not sleeping and will inspect state next.
        (void)xQueueSendToFront(s_smqtt_rx_queue, &wake, 0);
    }
}

static void smqtt_free_rx_item(smqtt_rx_item_t *item)
{
    if (!item) {
        return;
    }
    free(item->topic);
    free(item->data);
    item->topic = NULL;
    item->data = NULL;
}

static void smqtt_drain_rx_queue(void)
{
    if (s_smqtt_rx_queue == NULL) {
        return;
    }
    smqtt_rx_item_t item;
    while (xQueueReceive(s_smqtt_rx_queue, &item, 0) == pdTRUE) {
        smqtt_free_rx_item(&item);
    }
}

static bool smqtt_validate_rx_gen(void *ctx)
{
    uint32_t expected_gen = *(const uint32_t *)ctx;
    bool valid = false;
    _lock_acquire(&s_smqtt_lock);
    valid = (smqtt_running && g_device_mode != MODE_BLE && smqtt_generation == expected_gen);
    _lock_release(&s_smqtt_lock);
    return valid;
}

static void smqtt_wait_active_pubs_done(void)
{
    for (;;) {
        _lock_acquire(&s_smqtt_lock);
        int active = smqtt_client_active_pubs;
        SemaphoreHandle_t sem = s_smqtt_pub_done_sem;
        _lock_release(&s_smqtt_lock);

        if (active == 0) {
            return;
        }
        if (sem != NULL) {
            xSemaphoreTake(sem, pdMS_TO_TICKS(50));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

static void smqtt_destroy_client_after_pubs(esp_mqtt_client_handle_t client, uint32_t client_gen)
{
    if (!client) {
        return;
    }
    // Wait until every in-flight smqtt_publish_message() finishes using client.
    smqtt_wait_active_pubs_done();

    // ESP-IDF 5.2.3 esp_mqtt_task sets client->run = true at task entry, so
    // esp_mqtt_client_stop() can return ESP_FAIL if called before esp_mqtt_task runs.
    esp_err_t stop_err = esp_mqtt_client_stop(client);
    for (int retry = 0; stop_err != ESP_OK && retry < SMQTT_STOP_RETRY_MAX; retry++) {
        _lock_acquire(&s_smqtt_lock);
        bool task_entered = (smqtt_last_started_gen == client_gen);
        _lock_release(&s_smqtt_lock);

        vTaskDelay(pdMS_TO_TICKS(SMQTT_STOP_RETRY_DELAY_MS));
        stop_err = esp_mqtt_client_stop(client);
        if (stop_err == ESP_OK || task_entered) {
            break;
        }
    }

    // ESP-IDF 5.2.3 esp_mqtt_task sets STOPPED_BIT before updating client->state
    // and calling vTaskDelete(NULL). Combined with SMQTT_START_TASK_PRIORITY <
    // SMQTT_MQTT_TASK_PRIORITY, this grace delay ensures esp_mqtt_task has fully exited.
    vTaskDelay(pdMS_TO_TICKS(SMQTT_POST_STOP_GRACE_MS));
    esp_mqtt_client_destroy(client);
}

static void smqtt_run_pending_connected_action(void)
{
    bool has_action = false;
    uint32_t action_gen = 0;

    _lock_acquire(&s_smqtt_lock);
    if (smqtt_connected_action_pending) {
        smqtt_connected_action_pending = false;
        if (smqtt_running && g_device_mode != MODE_BLE &&
            smqtt_connected && smqtt_connected_action_gen == smqtt_generation) {
            has_action = true;
            action_gen = smqtt_connected_action_gen;
        }
    }
    _lock_release(&s_smqtt_lock);

    if (!has_action) {
        return;
    }

    esp_err_t ready_err = device_first_ready();
    if (ready_err == ESP_OK) {
        bool can_release_ble = false;
        _lock_acquire(&s_smqtt_lock);
        can_release_ble = (smqtt_running &&
                           smqtt_connected &&
                           smqtt_generation == action_gen &&
                           g_device_mode != MODE_BLE);
        _lock_release(&s_smqtt_lock);
        if (can_release_ble) {
            app_release_ble_resources("mqtt connected");
        }
    } else {
        ESP_LOGE(TAG, "Device first-ready failed: %s", esp_err_to_name(ready_err));
    }
}

int smqtt_publish_message(const char *json_data)
{
    if (!json_data) {
        return -1;
    }

    esp_mqtt_client_handle_t client = NULL;
    char topic[sizeof(publish_topic)];

    _lock_acquire(&s_smqtt_lock);
    if (!smqtt_running || !smqtt_connected || smqtt_client == NULL || g_device_mode == MODE_BLE) {
        _lock_release(&s_smqtt_lock);
        return -1;
    }
    client = smqtt_client;
    strlcpy(topic, publish_topic, sizeof(topic));
    smqtt_client_active_pubs++;
    _lock_release(&s_smqtt_lock);

    ESP_LOGI(TAG, "publish(mqtt): %s", json_data);
    int msg_id = esp_mqtt_client_publish(client, topic, json_data, 0, 1, 0);

    _lock_acquire(&s_smqtt_lock);
    if (smqtt_client_active_pubs > 0) {
        smqtt_client_active_pubs--;
    }
    if (smqtt_client_active_pubs == 0 && s_smqtt_pub_done_sem != NULL) {
        xSemaphoreGive(s_smqtt_pub_done_sem);
    }
    _lock_release(&s_smqtt_lock);

    return msg_id;
}

static void ip_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        char gw_uri[sizeof(mqtt_gateway_uri)];
        snprintf(gw_uri, sizeof(gw_uri), "mqtt://" IPSTR, IP2STR(&event->ip_info.gw));

        bool updated = false;
        _lock_acquire(&s_smqtt_lock);
        if (smqtt_running && g_device_mode != MODE_BLE) {
            strlcpy(mqtt_gateway_uri, gw_uri, sizeof(mqtt_gateway_uri));
            mqtt_broker_count = 2;
            updated = true;
        }
        _lock_release(&s_smqtt_lock);

        if (updated) {
            ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP: gateway=%s, enable gateway broker", gw_uri);
        }
    }
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
    uint32_t event_gen = (uint32_t)(uintptr_t)handler_args;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_BEFORE_CONNECT:
        _lock_acquire(&s_smqtt_lock);
        smqtt_last_started_gen = event_gen;
        _lock_release(&s_smqtt_lock);
        break;
    case MQTT_EVENT_CONNECTED:
    {
        bool stale = false;
        char local_recv_topic[sizeof(recv_topic)];
        _lock_acquire(&s_smqtt_lock);
        if (!smqtt_running || smqtt_generation != event_gen || g_device_mode == MODE_BLE) {
            stale = true;
        } else {
            smqtt_connecting = false;
            smqtt_connected = true;
            smqtt_connected_action_pending = true;
            smqtt_connected_action_gen = event_gen;
            strlcpy(local_recv_topic, recv_topic, sizeof(local_recv_topic));
            smqtt_wake_worker_locked();
        }
        _lock_release(&s_smqtt_lock);

        if (stale) {
            ESP_LOGI(TAG, "Ignore stale MQTT_EVENT_CONNECTED (gen=%" PRIu32 ")", event_gen);
            break;
        }

        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        /*subscibe topic: '/all'*/
        msg_id = esp_mqtt_client_subscribe(client, "/all", 0);
        ESP_LOGI(TAG, "sent subscribe successful, topic is /all, msg_id=%d", msg_id);
        /*subscibe topic: /drecv/{mac} */
        msg_id = esp_mqtt_client_subscribe(client, local_recv_topic, 0);
        ESP_LOGI(TAG, "sent subscribe successful, topic is %s, msg_id=%d", local_recv_topic, msg_id);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        _lock_acquire(&s_smqtt_lock);
        if (smqtt_generation == event_gen) {
            smqtt_connecting = false;
            smqtt_connected = false;
            smqtt_connected_action_pending = false;
        }
        _lock_release(&s_smqtt_lock);
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
    {
        // Offload payload processing out of ESP-MQTT's API-locked callback so
        // s_receive_mutex <-> MQTT_API_LOCK lock-order inversion cannot deadlock.
        bool stale = false;
        QueueHandle_t rx_queue = NULL;
        _lock_acquire(&s_smqtt_lock);
        stale = (!smqtt_running || smqtt_generation != event_gen || g_device_mode == MODE_BLE);
        rx_queue = s_smqtt_rx_queue;
        _lock_release(&s_smqtt_lock);

        if (stale || rx_queue == NULL || event->topic_len < 0 || event->data_len <= 0 ||
            !event->topic || !event->data) {
            break;
        }

        smqtt_rx_item_t item = {
            .type = SMQTT_RX_DATA,
            .gen = event_gen,
            .topic = (char *)malloc((size_t)event->topic_len + 1),
            .topic_len = event->topic_len,
            .data = (char *)malloc((size_t)event->data_len + 1),
            .data_len = event->data_len,
        };
        if (!item.topic || !item.data) {
            ESP_LOGW(TAG, "Drop MQTT data: out of memory (topic=%d, data=%d)",
                     event->topic_len, event->data_len);
            smqtt_free_rx_item(&item);
            break;
        }
        if (event->topic_len > 0) {
            memcpy(item.topic, event->topic, (size_t)event->topic_len);
        }
        item.topic[event->topic_len] = '\0';
        memcpy(item.data, event->data, (size_t)event->data_len);
        item.data[event->data_len] = '\0';

        if (xQueueSend(rx_queue, &item, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Drop MQTT data: rx queue full");
            smqtt_free_rx_item(&item);
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        _lock_acquire(&s_smqtt_lock);
        if (smqtt_generation == event_gen) {
            smqtt_connecting = false;
            smqtt_connected = false;
            smqtt_connected_action_pending = false;
        }
        _lock_release(&s_smqtt_lock);
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

// Periodic lifecycle & RX worker task: runs until stopped
static void smqtt_start_client_on_current_broker_task(void *arg)
{
    (void)arg;
    const TickType_t retry_interval = pdMS_TO_TICKS(SMQTT_RETRY_INTERVAL_MS);
    TickType_t next_retry_tick = xTaskGetTickCount() + retry_interval;

    for (;;) {
        // Run post-connect initialization and BLE release outside MQTT_API_LOCK
        // so Bluedroid BTC <-> MQTT_API_LOCK cannot deadlock.
        smqtt_run_pending_connected_action();

        TickType_t now = xTaskGetTickCount();
        int32_t remaining = (int32_t)(next_retry_tick - now);
        TickType_t wait_ticks = (remaining > 0) ? (TickType_t)remaining : 0;

        smqtt_rx_item_t item;
        bool got_item = false;
        if (s_smqtt_rx_queue != NULL) {
            got_item = (xQueueReceive(s_smqtt_rx_queue, &item, wait_ticks) == pdTRUE);
        } else if (wait_ticks > 0) {
            vTaskDelay(wait_ticks);
        }

        // Run any connected action queued before processing inbound MQTT commands.
        smqtt_run_pending_connected_action();

        if (got_item && item.type == SMQTT_RX_DATA) {
            uint32_t expected_gen = item.gen;
            if (smqtt_validate_rx_gen(&expected_gen)) {
                device_handle_receive_checked(
                    item.topic, item.topic_len,
                    item.data, item.data_len,
                    smqtt_validate_rx_gen, &expected_gen);
            }
            smqtt_free_rx_item(&item);

            // Re-check immediately whether stop was requested while processing.
            _lock_acquire(&s_smqtt_lock);
            bool stop_now = (!smqtt_running || g_device_mode == MODE_BLE);
            _lock_release(&s_smqtt_lock);
            if (!stop_now) {
                continue;
            }
        }

        esp_mqtt_client_handle_t client_to_destroy = NULL;
        uint32_t client_to_destroy_gen = 0;
        char broker_uri[sizeof(mqtt_gateway_uri)] = {0};
        int prev_index = 0;
        int next_index = 0;
        uint32_t gen = 0;

        _lock_acquire(&s_smqtt_lock);

        if (!smqtt_running || g_device_mode == MODE_BLE) {
            smqtt_connecting = false;
            smqtt_connected = false;
            smqtt_connected_action_pending = false;
            client_to_destroy = smqtt_client;
            client_to_destroy_gen = smqtt_client_generation;
            smqtt_client = NULL;
            smqtt_client_generation = 0;

            if (client_to_destroy == NULL) {
                smqtt_drain_rx_queue();
                smqtt_start_task_handle = NULL;
                _lock_release(&s_smqtt_lock);
                ESP_LOGI(TAG, "smqtt_start_task stopped");
                vTaskDelete(NULL);
                return;
            }
            _lock_release(&s_smqtt_lock);

            smqtt_destroy_client_after_pubs(client_to_destroy, client_to_destroy_gen);
            smqtt_drain_rx_queue();

            _lock_acquire(&s_smqtt_lock);
            if (!smqtt_running || g_device_mode == MODE_BLE) {
                smqtt_drain_rx_queue();
                smqtt_start_task_handle = NULL;
                _lock_release(&s_smqtt_lock);
                ESP_LOGI(TAG, "smqtt_start_task stopped");
                vTaskDelete(NULL);
                return;
            }
            _lock_release(&s_smqtt_lock);

            next_retry_tick = xTaskGetTickCount() + retry_interval;
            continue;
        }

        // If woken early (e.g., connected action wake) while still running, honor retry interval.
        now = xTaskGetTickCount();
        if ((int32_t)(next_retry_tick - now) > 0) {
            _lock_release(&s_smqtt_lock);
            continue;
        }
        next_retry_tick = now + retry_interval;

        bool stale_client = (smqtt_client != NULL && smqtt_client_generation != smqtt_generation);
        if (!stale_client && (smqtt_connecting || smqtt_connected)) {
            _lock_release(&s_smqtt_lock);
            continue;
        }

        if (smqtt_client != NULL) {
            smqtt_connecting = false;
            smqtt_connected = false;
            smqtt_connected_action_pending = false;
            client_to_destroy = smqtt_client;
            client_to_destroy_gen = smqtt_client_generation;
            smqtt_client = NULL;
            smqtt_client_generation = 0;
        }

        if (mqtt_broker_count <= 0) {
            mqtt_broker_count = 1;
        }
        prev_index = mqtt_broker_index;
        mqtt_broker_index = (mqtt_broker_index + 1) % mqtt_broker_count;
        next_index = mqtt_broker_index;
        strlcpy(broker_uri, mqtt_broker_uris[next_index], sizeof(broker_uri));
        gen = smqtt_generation;
        _lock_release(&s_smqtt_lock);

        if (client_to_destroy != NULL) {
            smqtt_destroy_client_after_pubs(client_to_destroy, client_to_destroy_gen);
            smqtt_drain_rx_queue();
        }

        if (broker_uri[0] == '\0') {
            continue;
        }

        _lock_acquire(&s_smqtt_lock);
        bool stop_requested = (!smqtt_running || g_device_mode == MODE_BLE);
        bool gen_changed = (smqtt_generation != gen);
        _lock_release(&s_smqtt_lock);
        if (stop_requested) {
            next_retry_tick = xTaskGetTickCount();
            continue;
        }
        if (gen_changed) {
            continue;
        }

        ESP_LOGW(TAG, "Switch MQTT broker from %d to %d", prev_index, next_index);
        ESP_LOGI(TAG, "Start MQTT on broker[%d]: %s", next_index, broker_uri);

        esp_mqtt_client_config_t mqtt_cfg = {
            .broker.address.uri = broker_uri,
            // Disable auto reconnect to allow manual fallback switching
            .network.disable_auto_reconnect = true,
            .task.priority = SMQTT_MQTT_TASK_PRIORITY,
        };
        esp_mqtt_client_handle_t new_client = esp_mqtt_client_init(&mqtt_cfg);
        if (new_client == NULL) {
            ESP_LOGE(TAG, "Failed to init MQTT client");
            continue;
        }
        esp_err_t reg_err = esp_mqtt_client_register_event(
            new_client, ESP_EVENT_ANY_ID, mqtt_event_handler, (void *)(uintptr_t)gen);
        if (reg_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(reg_err));
            esp_mqtt_client_destroy(new_client);
            continue;
        }

        // Start new_client while holding s_smqtt_lock so mqtt_app_stop() cannot
        // slip between the generation check and esp_mqtt_client_start().
        // new_client is not published yet; its internal API lock has no contention
        // and esp_mqtt_task cannot enter its loop until esp_mqtt_client_start() returns.
        esp_err_t start_err = ESP_FAIL;
        bool aborted_before_start = false;
        bool stop_after_abort = false;
        _lock_acquire(&s_smqtt_lock);
        if (!smqtt_running || g_device_mode == MODE_BLE || smqtt_generation != gen) {
            aborted_before_start = true;
            stop_after_abort = (!smqtt_running || g_device_mode == MODE_BLE);
        } else {
            start_err = esp_mqtt_client_start(new_client);
            if (start_err == ESP_OK) {
                smqtt_client = new_client;
                smqtt_client_generation = gen;
                smqtt_connecting = true;
            }
        }
        _lock_release(&s_smqtt_lock);

        if (aborted_before_start) {
            esp_mqtt_client_destroy(new_client);
            if (stop_after_abort) {
                next_retry_tick = xTaskGetTickCount();
            }
            continue;
        }
        if (start_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(start_err));
            esp_mqtt_client_destroy(new_client);
        }
    }
}

void mqtt_app_start(void)
{
    if (g_device_mode == MODE_BLE) {
        ESP_LOGI(TAG, "Skip mqtt_app_start in BLE mode");
        return;
    }

    ESP_LOGI(TAG, "mqtt_app_start");

    // Register IP event once to update gateway URI when IP is acquired
    bool need_register_ip = false;
    _lock_acquire(&s_smqtt_lock);
    smqtt_ensure_primitives_locked();
    if (!ip_event_registered) {
        ip_event_registered = true;
        need_register_ip = true;
    }
    _lock_release(&s_smqtt_lock);

    if (need_register_ip) {
        esp_err_t err = esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL, &ip_event_inst);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(err));
            _lock_acquire(&s_smqtt_lock);
            ip_event_registered = false;
            _lock_release(&s_smqtt_lock);
        }
    }

    _lock_acquire(&s_smqtt_lock);
    if (g_device_mode == MODE_BLE) {
        _lock_release(&s_smqtt_lock);
        ESP_LOGI(TAG, "Abort mqtt_app_start: switched to BLE mode");
        return;
    }

    smqtt_ensure_primitives_locked();
    smqtt_running = true;
    smqtt_generation++;
    smqtt_connecting = false;
    smqtt_connected = false;
    smqtt_connected_action_pending = false;
    // Start with only easysmart.local until we get GOT_IP
    mqtt_gateway_uri[0] = '\0';
    mqtt_broker_count = 1;
    mqtt_broker_index = 0;
    // init topic
    snprintf(publish_topic, sizeof(publish_topic), "/dpub/%s", macStr);
    snprintf(recv_topic, sizeof(recv_topic), "/drecv/%s", macStr);

    if (smqtt_start_task_handle == NULL) {
        BaseType_t ret = xTaskCreate(
            smqtt_start_client_on_current_broker_task,
            "smqtt_start_task",
            SMQTT_TASK_STACK_SIZE,
            NULL,
            SMQTT_START_TASK_PRIORITY,
            &smqtt_start_task_handle);
        if (ret != pdPASS) {
            smqtt_running = false;
            smqtt_start_task_handle = NULL;
            ESP_LOGE(TAG, "Failed to create smqtt_start_task");
        }
    }
    _lock_release(&s_smqtt_lock);
}

void mqtt_app_stop(void)
{
    ESP_LOGI(TAG, "mqtt_app_stop");

    _lock_acquire(&s_smqtt_lock);
    smqtt_running = false;
    smqtt_generation++;
    smqtt_connecting = false;
    smqtt_connected = false;
    smqtt_connected_action_pending = false;
    mqtt_gateway_uri[0] = '\0';
    mqtt_broker_count = 1;
    mqtt_broker_index = 0;
    smqtt_wake_worker_locked();
    _lock_release(&s_smqtt_lock);
}
