#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAXIMUM_RETRY      8
#define TELEMETRY_QUEUE_LENGTH 16

static const char *TAG = "iot_bridge";
static EventGroupHandle_t wifi_event_group;
static QueueHandle_t telemetry_queue;
static int retry_count;
static char device_id[13];

typedef struct {
    uint32_t sequence;
    TickType_t created_at;
    float temperature_c;
    uint16_t light_raw;
} telemetry_t;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        if (retry_count++ < MAXIMUM_RETRY) {
            ESP_LOGW(TAG, "Wi-Fi disconnected; retrying (%d/%d)", retry_count, MAXIMUM_RETRY);
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Wi-Fi reconnect limit reached");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        retry_count = 0;
        xEventGroupClearBits(wifi_event_group, WIFI_FAIL_BIT);
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi connected; bridge is online");
    }
}

static void wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, CONFIG_BRIDGE_WIFI_SSID,
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, CONFIG_BRIDGE_WIFI_PASSWORD,
            sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = strlen(CONFIG_BRIDGE_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void telemetry_producer_task(void *arg)
{
    uint32_t sequence = 0;
    while (true) {
        telemetry_t message = {
            .sequence = sequence++,
            .created_at = xTaskGetTickCount(),
            .temperature_c = 24.0f + (sequence % 30) / 10.0f,
            .light_raw = 1000 + (sequence % 200),
        };

        if (xQueueSend(telemetry_queue, &message, pdMS_TO_TICKS(100)) != pdPASS) {
            ESP_LOGW(TAG, "Telemetry queue full; dropped sequence=%" PRIu32, message.sequence);
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_BRIDGE_SAMPLE_PERIOD_MS));
    }
}

static esp_err_t send_telemetry(const telemetry_t *message)
{
    char payload[192];
    int length = snprintf(payload, sizeof(payload),
                          "{\"device_id\":\"%s\",\"sequence\":%" PRIu32
                          ",\"uptime_ms\":%" PRIu32 ",\"temperature_c\":%.1f,\"light_raw\":%u}",
                          device_id, message->sequence,
                          (uint32_t)(message->created_at * portTICK_PERIOD_MS),
                          message->temperature_c, message->light_raw);
    if (length < 0 || length >= sizeof(payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_config_t config = {
        .url = CONFIG_BRIDGE_HTTP_ENDPOINT,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, length);
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Forwarded sequence=%" PRIu32 ", HTTP %d", message->sequence,
                 esp_http_client_get_status_code(client));
    } else {
        ESP_LOGW(TAG, "Failed to forward sequence=%" PRIu32 ": %s", message->sequence,
                 esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
    return err;
}

static void cloud_bridge_task(void *arg)
{
    telemetry_t message;
    while (true) {
        if (xQueueReceive(telemetry_queue, &message, portMAX_DELAY) != pdPASS) {
            continue;
        }
        EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                                pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));
        if (!(bits & WIFI_CONNECTED_BIT)) {
            ESP_LOGW(TAG, "Offline; discarded sequence=%" PRIu32, message.sequence);
            continue;
        }
        send_telemetry(&message);
    }
}

void app_main(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(device_id, sizeof(device_id), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    telemetry_queue = xQueueCreate(TELEMETRY_QUEUE_LENGTH, sizeof(telemetry_t));
    configASSERT(telemetry_queue != NULL);
    wifi_init_sta();
    xTaskCreate(telemetry_producer_task, "telemetry_source", 3072, NULL, 5, NULL);
    xTaskCreate(cloud_bridge_task, "cloud_bridge", 6144, NULL, 5, NULL);
    ESP_LOGI(TAG, "IoT bridge started: queue=%d, endpoint=%s", TELEMETRY_QUEUE_LENGTH,
             CONFIG_BRIDGE_HTTP_ENDPOINT);
}
