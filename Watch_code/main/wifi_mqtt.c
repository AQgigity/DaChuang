/**
 * @file wifi_mqtt.c
 * @brief WiFi + MQTT + OneNET 云平台模块
 */

#include "wifi_mqtt.h"
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

#if ENABLE_EMG
#include "emg_sensor.h"
#endif

static const char *TAG = "CLOUD";

/* ==================== WiFi / MQTT 配置 ==================== */

#define WIFI_SSID      "无敌大菠萝"
#define WIFI_PASS      "55555555"
#define MQTT_URI       "mqtt://mqtts.heclouds.com:1883"
#define MQTT_CLIENT_ID "TEST1"
#define MQTT_USERNAME  "b2aLMZ812F"
#define MQTT_PASSWORD  "version=2018-10-31&res=products%2Fb2aLMZ812F%2Fdevices%2FTEST1&et=1905932926&method=md5&sign=tgOEowtK7HHgxR5siXZQBg%3D%3D"
#define ONENET_TOPIC   "$sys/b2aLMZ812F/TEST1/thing/property/post"
#define ONENET_SET_TOPIC "$sys/b2aLMZ812F/TEST1/thing/property/set"
#define MQTT_SEND_INTERVAL_MS 2000

/* 模块私有状态 */
static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static SemaphoreHandle_t g_cccd_sem = NULL;

/* ==================== WiFi 事件 ==================== */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started, connecting...");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected to AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
            g_wifi_connected = false;
            g_mqtt_connected = false;
            esp_wifi_connect();
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        g_wifi_connected = true;

        if (g_mqtt_client != NULL) {
            esp_mqtt_client_start(g_mqtt_client);
        }
    }
}

/* ==================== MQTT 事件 ==================== */

static void mqtt_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to OneNET");
        g_mqtt_connected = true;
        esp_mqtt_client_subscribe(g_mqtt_client, ONENET_SET_TOPIC, 1);
        ESP_LOGI(TAG, "Subscribed to: %s", ONENET_SET_TOPIC);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        g_mqtt_connected = false;
        break;
    case MQTT_EVENT_DATA: {
        char topic[128] = {0};
        int topic_len = event->topic_len < (int)sizeof(topic) - 1 ?
                        event->topic_len : (int)sizeof(topic) - 1;
        memcpy(topic, event->topic, topic_len);

        if (strcmp(topic, ONENET_SET_TOPIC) == 0) {
            char payload[512] = {0};
            int payload_len = event->data_len < (int)sizeof(payload) - 1 ?
                              event->data_len : (int)sizeof(payload) - 1;
            memcpy(payload, event->data, payload_len);

            ESP_LOGI(TAG, "Received set cmd: %.*s", payload_len, payload);

            bool new_state = g_upload_enabled;
            if (strstr(payload, "\"control_switch\"") != NULL) {
                if (strstr(payload, ":true") != NULL) {
                    new_state = true;
                } else if (strstr(payload, ":false") != NULL) {
                    new_state = false;
                }
                g_upload_enabled = new_state;
                ESP_LOGI(TAG, ">>> Upload %s",
                         new_state ? "ENABLED" : "DISABLED");

                char resp[128];
                int resp_len = snprintf(resp, sizeof(resp),
                    "{\"code\":200,\"msg\":\"success\","
                    "\"data\":{\"control_switch\":%s}}",
                    new_state ? "true" : "false");
                esp_mqtt_client_publish(g_mqtt_client, ONENET_SET_TOPIC,
                                        resp, resp_len, 1, 0);
                ESP_LOGI(TAG, "Reply sent: %.*s", resp_len, resp);

                if (g_mqtt_connected) {
                    char status_buf[128];
                    static int sw_id = 0;
                    int slen = snprintf(status_buf, sizeof(status_buf),
                        "{\"id\":\"sw%d\",\"version\":\"1.0\","
                        "\"params\":{\"control_switch\":{\"value\":%s}}}",
                        ++sw_id, new_state ? "true" : "false");
                    esp_mqtt_client_publish(g_mqtt_client, ONENET_TOPIC,
                                            status_buf, slen, 1, 0);
                    ESP_LOGI(TAG, "Switch status reported: %s",
                             new_state ? "true" : "false");
                }
            }
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error type: %d", event->error_handle->error_type);
        break;
    default:
        break;
    }
}

/* ==================== OneNET 数据打包 ==================== */

static int status_to_enum(const char *action)
{
    if (strcmp(action, "走") == 0 || strcmp(action, "walk") == 0) return 0;
    if (strcmp(action, "跑") == 0 || strcmp(action, "run") == 0) return 1;
    if (strcmp(action, "静止") == 0 || strcmp(action, "still") == 0) return 2;
    if (strcmp(action, "准备") == 0 || strcmp(action, "ready") == 0) return 3;
    if (strcmp(action, "跳") == 0 || strcmp(action, "jump") == 0) return 4;
    return 2;
}

static int onenet_build_payload(char *buf, int buf_size,
                                 float temp, float press, float hr,
                                 const char *action, int cadence,
                                 const char *gait_style, int arm_freq)
{
    static int msg_id = 0;
    int status_val = status_to_enum(action);
    int len = snprintf(buf, buf_size,
        "{"
        "\"id\":\"%d\","
        "\"version\":\"1.0\","
        "\"params\":{"
        "\"heart_rate\":{\"value\":%d},"
        "\"Temp\":{\"value\":%.1f},"
        "\"barometric\":{\"value\":%.2f},"
        "\"status\":{\"value\":%d},"
        "\"step_frequency\":{\"value\":%d},"
        "\"gait_style\":{\"value\":\"%s\"},"
        "\"arm_frequency\":{\"value\":%d}"
        "}"
        "}",
        ++msg_id, (int)hr, temp, press, status_val, cadence,
        gait_style, arm_freq);

    return (len > 0 && len < buf_size) ? len : -1;
}

/* ==================== BLE/WiFi 共存 CCCD 回调 ==================== */

static int ble_cccd_write_cb(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             struct ble_gatt_attr *attr, void *arg)
{
    if (error->status == 0) {
        ESP_LOGI(TAG, "CCCD write OK (notify %s)",
                 arg == NULL ? "disabled" : "enabled");
    } else {
        ESP_LOGW(TAG, "CCCD write failed: %d", error->status);
    }
    if (g_cccd_sem) xSemaphoreGive(g_cccd_sem);
    return 0;
}

/* ==================== 公开接口 ==================== */

void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any_id, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_got_ip));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init done, SSID: %s", WIFI_SSID);
}

void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URI,
        .credentials.client_id = MQTT_CLIENT_ID,
        .credentials.username = MQTT_USERNAME,
        .credentials.authentication.password = MQTT_PASSWORD,
    };

    g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    ESP_LOGI(TAG, "MQTT client init done");
}

void wifi_mqtt_create_cccd_sem(void)
{
    g_cccd_sem = xSemaphoreCreateBinary();
}

void wifi_start_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(2000));

    if (g_ankle_conn_handle != BLE_HS_CONN_HANDLE_NONE &&
        g_nus_tx_val_handle != 0) {
        uint16_t cccd_handle = g_nus_tx_val_handle + 1;
        uint16_t notify_dis = 0x0000;
        int rc = ble_gattc_write_flat(g_ankle_conn_handle, cccd_handle,
                                       &notify_dis, sizeof(notify_dis),
                                       ble_cccd_write_cb, NULL);
        if (rc == 0 && g_cccd_sem) {
            xSemaphoreTake(g_cccd_sem, pdMS_TO_TICKS(1000));
        }
        ESP_LOGI(TAG, "BLE notifications paused for WiFi");
    }

    wifi_init();
    mqtt_init();

    for (int i = 0; i < 150 && !g_wifi_connected; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (g_wifi_connected) {
        ESP_LOGI(TAG, "WiFi connected, re-enabling BLE notifications");
        if (g_ankle_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            uint16_t cccd_handle = g_nus_tx_val_handle + 1;
            uint16_t notify_en = 0x0001;
            int rc = ble_gattc_write_flat(g_ankle_conn_handle, cccd_handle,
                                           &notify_en, sizeof(notify_en),
                                           ble_cccd_write_cb, (void *)1);
            if (rc == 0 && g_cccd_sem) {
                xSemaphoreTake(g_cccd_sem, pdMS_TO_TICKS(1000));
            }
        }
    } else {
        ESP_LOGW(TAG, "WiFi connect timeout, re-enabling BLE anyway");
        if (g_ankle_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            uint16_t cccd_handle = g_nus_tx_val_handle + 1;
            uint16_t notify_en = 0x0001;
            ble_gattc_write_flat(g_ankle_conn_handle, cccd_handle,
                                  &notify_en, sizeof(notify_en),
                                  ble_cccd_write_cb, (void *)1);
        }
    }

    ESP_LOGI(TAG, "WiFi + MQTT init complete");
    vTaskDelete(NULL);
}

void mqtt_upload_task(void *arg)
{
    char payload[320];
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(MQTT_SEND_INTERVAL_MS);

    while (1) {
        vTaskDelayUntil(&last_wake, period);

        if (!g_mqtt_connected || !g_upload_enabled) {
            continue;
        }

        float temp = g_current_temp;
        float press = g_current_press;
        float hr = g_current_bpm;
        ankle_data_t latest = g_latest_ankle;

        const char *action = latest.valid ? latest.action : "静止";
        int cadence = latest.valid ? latest.cadence_spm : 0;
        const char *gait_style = latest.valid ? latest.gait_style : "未发力";

#if ENABLE_EMG
        int arm_freq = (g_emg_peak_count + 1) / 2;
#else
        int arm_freq = 0;
#endif

        int len = onenet_build_payload(payload, sizeof(payload),
                                        temp, press, hr, action, cadence,
                                        gait_style, arm_freq);
        if (len <= 0) {
            ESP_LOGE(TAG, "Payload build failed");
            continue;
        }

        ESP_LOGI(TAG, "Payload[%d]: %.*s", len, len, payload);

        int msg_id = esp_mqtt_client_publish(g_mqtt_client, ONENET_TOPIC,
                                              payload, len, 0, 0);
        if (msg_id >= 0) {
            ESP_LOGI(TAG, "MQTT publish OK: %s Gait=%s HR=%.0f T=%.1f P=%.1f C=%d",
                     action, gait_style, hr, temp, press, cadence);
        } else {
            ESP_LOGW(TAG, "MQTT publish failed (ret=%d)", msg_id);
        }
    }
}
