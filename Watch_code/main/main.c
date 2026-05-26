#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "i2c_bus.h"
#include "bme280.h"
#include "max30102.h"

static const char *TAG = "WATCH";

/* ==================== NUS UUID（与脚踝端一致） ==================== */

#define ANKLE_DEVICE_NAME   "ESP32_Gait_Gatt"
#define FUSION_TASK_STACK   4096
#define FUSION_TASK_PRIO    2

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

/* 调试开关：1 = 只测 WiFi+MQTT，跳过 BLE */
#define WIFI_ONLY_TEST  0

/* Nordic UART Service: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E */
static const ble_uuid128_t nus_svc_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);

/* TX (Notify): 6E400003-B5A3-F393-E0A9-E50E24DCCA9E */
static const ble_uuid128_t nus_tx_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

/* RX (Write): 6E400002-B5A3-F393-E0A9-E50E24DCCA9E */
static const ble_uuid128_t nus_rx_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);

/* ==================== 数据结构 ==================== */

typedef struct {
    char     action[16];
    int      cadence_spm;
    uint32_t timestamp_ms;
    bool     valid;
} ankle_data_t;

typedef struct {
    ankle_data_t ankle;
    float heart_rate_bpm;
    float temperature;
    float pressure_hpa;
} watch_data_t;

/* ==================== 全局状态 ==================== */

/* 传感器句柄 */
static i2c_bus_handle_t  g_i2c_bus;
static bme280_handle_t   g_bme280;
static max30102_handle_t g_max30102;

/* BLE Central 状态 */
static uint16_t g_ankle_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_nus_tx_val_handle = 0;
static uint16_t g_nus_rx_val_handle = 0;
static uint16_t g_nus_svc_start_handle = 0;
static uint16_t g_nus_svc_end_handle = 0;
static volatile bool g_ankle_connected = false;

/* 数据交换队列 */
static QueueHandle_t g_ankle_data_queue = NULL;

/* 本地传感器共享数据（volatile 单写多读） */
static volatile float g_current_bpm = 0.0f;
static volatile float g_current_temp = 0.0f;
static volatile float g_current_press = 0.0f;

/* 最新脚踝数据（供 MQTT 读取，由 fusion task 更新） */
static volatile ankle_data_t g_latest_ankle = {0};

/* BLE 数据拼接缓冲区 */
static char g_rx_buf[128];
static int  g_rx_len = 0;

/* WiFi / MQTT 状态 */
static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static volatile bool g_wifi_connected = false;
static volatile bool g_mqtt_connected = false;
static volatile bool g_upload_enabled = false;  /* 云平台上传开关 */

/* 前向声明 */
static void ble_start_scan(void);
static int ble_central_gap_event(struct ble_gap_event *event, void *arg);
static void wifi_init(void);
static void mqtt_init(void);
static void wifi_start_task(void *arg);

/* ==================== NVS 初始化 ==================== */

static void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS: erasing and re-init");
        nvs_flash_erase();
        nvs_flash_init();
    }
}

/* ==================== 传感器任务 ==================== */

static void environment_sensor_task(void *arg)
{
    bme280_handle_t *handle = (bme280_handle_t *)arg;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(100);
    bme280_data_t data;
    int log_cnt = 0;

    while (1) {
        if (bme280_read_data(handle, &data) == ESP_OK) {
            g_current_temp = data.temperature;
            g_current_press = data.pressure;
            if (++log_cnt >= 50) {  /* 每5秒打印一次 */
                ESP_LOGI("ENV", "Temp: %.2f C, Press: %.2f hPa",
                         data.temperature, data.pressure);
                log_cnt = 0;
            }
        }
        vTaskDelayUntil(&last_wake, period);
    }
}

static void heart_rate_task(void *arg)
{
    max30102_handle_t *handle = (max30102_handle_t *)arg;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(20);
    int log_cnt = 0;

    while (1) {
        int32_t red_sample;
        if (max30102_read_fifo(handle, &red_sample) == ESP_OK) {
            max30102_process_sample(handle, red_sample);
            float bpm = max30102_get_bpm(handle);
            if (bpm > 0.0f) {
                g_current_bpm = bpm;
            }
            if (++log_cnt >= 250) {  /* 每5秒打印一次 */
                ESP_LOGI("HR", "BPM: %.1f", handle->current_bpm);
                log_cnt = 0;
            }
        }
        vTaskDelayUntil(&last_wake, period);
    }
}

/* ==================== BLE 扫描 ==================== */

static void ble_start_scan(void)
{
    struct ble_gap_disc_params disc_params;
    memset(&disc_params, 0, sizeof(disc_params));
    disc_params.filter_duplicates = 1;
    disc_params.passive = 0;
    disc_params.itvl = 0x10;
    disc_params.window = 0x10;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    ESP_LOGI(TAG, "Scanning for '%s'...", ANKLE_DEVICE_NAME);

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER,
                          &disc_params, ble_central_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
    }
}

/* ==================== BLE 服务/特征发现 ==================== */

/* 步骤 4: CCCD 写入完成后，写 'r' 到 RX 启动脚踝 */
static int ble_central_on_cccd_write(uint16_t conn_handle,
                                     const struct ble_gatt_error *error,
                                     struct ble_gatt_attr *attr, void *arg)
{
    if (error->status != 0) {
        ESP_LOGE(TAG, "CCCD write failed: %d", error->status);
        return 0;
    }
    ESP_LOGI(TAG, "TX notifications enabled");

    /* 写 'r' 到 RX 特征，启动脚踝传感器 + 推理 */
    struct os_mbuf *om = ble_hs_mbuf_from_flat("r", 1);
    if (om != NULL) {
        int rc = ble_gattc_write_no_rsp(conn_handle, g_nus_rx_val_handle, om);
        if (rc == 0) {
            ESP_LOGI(TAG, "Sent 'r' to ankle (start command)");
        } else {
            ESP_LOGE(TAG, "Write 'r' failed: %d", rc);
        }
    }
    return 0;
}

/* 步骤 3: 发现 RX 特征后，向 TX 的 CCCD 写入通知使能 */
static int ble_central_on_rx_disc(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status != 0) {
        if (error->status == BLE_HS_EDONE) {
            ESP_LOGI(TAG, "RX discovery complete");
        } else {
            ESP_LOGE(TAG, "RX characteristic discovery failed: %d", error->status);
        }
        return 0;
    }

    g_nus_rx_val_handle = chr->val_handle;
    ESP_LOGI(TAG, "RX characteristic found, val_handle=%d", g_nus_rx_val_handle);

    /* 向 TX 的 CCCD 写入 0x0001 使能通知（CCCD handle = val_handle + 1） */
    uint16_t cccd_handle = g_nus_tx_val_handle + 1;
    uint16_t notify_en = 0x0001;
    int rc = ble_gattc_write_flat(conn_handle, cccd_handle,
                                   &notify_en, sizeof(notify_en),
                                   ble_central_on_cccd_write, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "CCCD write failed: %d", rc);
    }
    return 0;
}

/* 步骤 2b: 发现 TX 特征后，发现 RX */
static int ble_central_on_tx_disc(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status != 0) {
        if (error->status == BLE_HS_EDONE) {
            ESP_LOGI(TAG, "TX discovery complete");
        } else {
            ESP_LOGE(TAG, "TX characteristic discovery failed: %d", error->status);
        }
        return 0;
    }

    g_nus_tx_val_handle = chr->val_handle;
    ESP_LOGI(TAG, "TX characteristic found, val_handle=%d", g_nus_tx_val_handle);

    /* 发现 RX 特征 */
    int rc = ble_gattc_disc_chrs_by_uuid(conn_handle,
                                          g_nus_svc_start_handle,
                                          g_nus_svc_end_handle,
                                          &nus_rx_uuid.u,
                                          ble_central_on_rx_disc, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "RX characteristic discovery failed: %d", rc);
    }
    return 0;
}

/* 步骤 2a: 发现 NUS 服务后，发现 TX 特征 */
static int ble_central_on_svc_disc(uint16_t conn_handle,
                                   const struct ble_gatt_error *error,
                                   const struct ble_gatt_svc *svc, void *arg)
{
    if (error->status != 0) {
        if (error->status == BLE_HS_EDONE) {
            ESP_LOGI(TAG, "Service discovery complete");
        } else {
            ESP_LOGE(TAG, "Service discovery failed: %d", error->status);
        }
        return 0;
    }

    g_nus_svc_start_handle = svc->start_handle;
    g_nus_svc_end_handle = svc->end_handle;
    ESP_LOGI(TAG, "NUS service found, handles=%d-%d",
             g_nus_svc_start_handle, g_nus_svc_end_handle);

    /* 发现 TX 特征 */
    int rc = ble_gattc_disc_chrs_by_uuid(conn_handle,
                                          g_nus_svc_start_handle,
                                          g_nus_svc_end_handle,
                                          &nus_tx_uuid.u,
                                          ble_central_on_tx_disc, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "TX characteristic discovery failed: %d", rc);
    }
    return 0;
}

/* ==================== 数据解析 ==================== */

static void parse_and_enqueue(const char *msg)
{
    ankle_data_t data = {0};
    data.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    char *action_start = strstr(msg, "：");
    char *cadence_start = strstr(msg, "步频：");

    if (action_start && cadence_start) {
        action_start += 3;
        char *comma = strstr(action_start, "，");
        if (comma && (comma - action_start) < (int)sizeof(data.action) - 1) {
            memcpy(data.action, action_start, comma - action_start);
            data.action[comma - action_start] = '\0';
        }

        cadence_start += 9;
        data.cadence_spm = atoi(cadence_start);
        data.valid = true;

        xQueueSend(g_ankle_data_queue, &data, 0);
    }
}

/* ==================== 通知回调（带分片拼接） ==================== */

static int ble_central_on_notify(struct ble_gap_event *event)
{
    uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
    if (len == 0) return 0;

    /* 追加到拼接缓冲区 */
    if (g_rx_len + len < (int)sizeof(g_rx_buf) - 1) {
        os_mbuf_copydata(event->notify_rx.om, 0, len, g_rx_buf + g_rx_len);
        g_rx_len += len;
        g_rx_buf[g_rx_len] = '\0';
    } else {
        /* 缓冲区溢出，重置 */
        g_rx_len = 0;
        return 0;
    }

    /* 按换行符切分完整消息 */
    char *nl;
    while ((nl = memchr(g_rx_buf, '\n', g_rx_len)) != NULL) {
        *nl = '\0';
        parse_and_enqueue(g_rx_buf);

        /* 移除已处理的消息 */
        int consumed = (nl - g_rx_buf) + 1;
        g_rx_len -= consumed;
        memmove(g_rx_buf, g_rx_buf + consumed, g_rx_len);
    }

    return 0;
}

/* ==================== GAP 事件处理 ==================== */

static int ble_central_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        /* 解析广播包，匹配设备名 */
        struct ble_hs_adv_fields fields;
        ble_hs_adv_parse_fields(&fields, event->disc.data,
                                event->disc.length_data);

        if (fields.name != NULL && fields.name_len > 0) {
            char name[32] = {0};
            memcpy(name, fields.name, fields.name_len);
            if (strcmp(name, ANKLE_DEVICE_NAME) == 0) {
                ESP_LOGI(TAG, "Found ankle: %s", name);
                ble_gap_disc_cancel();

                int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC,
                                         &event->disc.addr,
                                         30000, NULL,
                                         ble_central_gap_event, NULL);
                if (rc != 0) {
                    ESP_LOGE(TAG, "ble_gap_connect failed: %d", rc);
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    ble_start_scan();
                }
            }
        }
        return 0;
    }

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            ESP_LOGE(TAG, "Connection failed; status=%d", event->connect.status);
            vTaskDelay(pdMS_TO_TICKS(2000));
            ble_start_scan();
            return 0;
        }

        ESP_LOGI(TAG, "Connected to ankle; handle=%d", event->connect.conn_handle);
        g_ankle_conn_handle = event->connect.conn_handle;
        g_ankle_connected = true;

        /* BLE 连接稳定后延迟启动 WiFi（避免共存冲突） */
        xTaskCreate(wifi_start_task, "wifi_start", 4096, NULL, 3, NULL);

        /* 启动 NUS 服务发现 */
        int rc = ble_gattc_disc_svc_by_uuid(g_ankle_conn_handle,
                                             &nus_svc_uuid.u,
                                             ble_central_on_svc_disc, NULL);
        if (rc != 0) {
            ESP_LOGE(TAG, "Service discovery start failed: %d", rc);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
        g_ankle_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_ankle_connected = false;
        g_nus_tx_val_handle = 0;
        g_nus_rx_val_handle = 0;

        /* 延迟后重新扫描 */
        vTaskDelay(pdMS_TO_TICKS(1000));
        ble_start_scan();
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        return ble_central_on_notify(event);

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated; conn_handle=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

/* ==================== NimBLE Host 回调 ==================== */

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset; reason=%d", reason);
}

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "BLE host synced, starting scan...");
    ble_start_scan();
}

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_deinit();
}

/* ==================== WiFi ==================== */

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

        /* 获取 IP 后启动 MQTT */
        if (g_mqtt_client != NULL) {
            esp_mqtt_client_start(g_mqtt_client);
        }
    }
}

static void wifi_init(void)
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

/* ==================== MQTT ==================== */

static void mqtt_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected to OneNET");
        g_mqtt_connected = true;
        /* 订阅属性设置 topic */
        esp_mqtt_client_subscribe(g_mqtt_client, ONENET_SET_TOPIC, 1);
        ESP_LOGI(TAG, "Subscribed to: %s", ONENET_SET_TOPIC);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        g_mqtt_connected = false;
        break;
    case MQTT_EVENT_DATA: {
        /* 检查是否是属性设置 topic */
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

            /* 解析 control_switch 字段
             * OneNET 格式: {"params":{"control_switch":true}} 或
             *               {"control_switch":{"value":true}}
             */
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

                /* 回复 OneNET（否则平台报超时） */
                char resp[128];
                int resp_len = snprintf(resp, sizeof(resp),
                    "{\"code\":200,\"msg\":\"success\","
                    "\"data\":{\"control_switch\":%s}}",
                    new_state ? "true" : "false");
                esp_mqtt_client_publish(g_mqtt_client, ONENET_SET_TOPIC,
                                        resp, resp_len, 1, 0);
                ESP_LOGI(TAG, "Reply sent: %.*s", resp_len, resp);

                /* 立即上报一次开关状态到物模型 */
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

static void mqtt_init(void)
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
    /* 不在这里 start，等 WiFi 获取 IP 后再 start */
    ESP_LOGI(TAG, "MQTT client init done");
}

/* ==================== OneNET 数据打包 ==================== */

static int status_to_enum(const char *action)
{
    if (strcmp(action, "走") == 0 || strcmp(action, "walk") == 0) return 0;
    if (strcmp(action, "跑") == 0 || strcmp(action, "run") == 0) return 1;
    if (strcmp(action, "静止") == 0 || strcmp(action, "still") == 0) return 2;
    if (strcmp(action, "准备") == 0 || strcmp(action, "ready") == 0) return 3;
    if (strcmp(action, "跳") == 0 || strcmp(action, "jump") == 0) return 4;
    return 2; /* 默认静止 */
}

static int onenet_build_payload(char *buf, int buf_size,
                                 float temp, float press, float hr,
                                 const char *action, int cadence)
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
        "\"step_frequency\":{\"value\":%d}"
        "}"
        "}",
        ++msg_id, (int)hr, temp, press, status_val, cadence);

    return (len > 0 && len < buf_size) ? len : -1;
}

/* ==================== MQTT 上报任务 ==================== */

/* ==================== 延迟启动 WiFi（BLE 共存优化） ==================== */

static SemaphoreHandle_t g_cccd_sem = NULL;

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

static void wifi_start_task(void *arg)
{
    /* 等待 2 秒让 BLE 连接稳定 */
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* 暂停 BLE 通知，给 WiFi 腾出射频 */
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

    /* 启动 WiFi */
    wifi_init();
    mqtt_init();

    /* 等待 WiFi 连接成功（最多 15 秒） */
    for (int i = 0; i < 150 && !g_wifi_connected; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (g_wifi_connected) {
        ESP_LOGI(TAG, "WiFi connected, re-enabling BLE notifications");
        /* 恢复 BLE 通知 */
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

static void mqtt_upload_task(void *arg)
{
    char payload[320];
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(MQTT_SEND_INTERVAL_MS);

    while (1) {
        vTaskDelayUntil(&last_wake, period);

        if (!g_mqtt_connected || !g_upload_enabled) {
            continue;
        }

        /* 读取最新融合数据 */
        float temp = g_current_temp;
        float press = g_current_press;
        float hr = g_current_bpm;
        ankle_data_t latest = g_latest_ankle;

        const char *action = latest.valid ? latest.action : "静止";
        int cadence = latest.valid ? latest.cadence_spm : 0;

        int len = onenet_build_payload(payload, sizeof(payload),
                                        temp, press, hr, action, cadence);
        if (len <= 0) {
            ESP_LOGE(TAG, "Payload build failed");
            continue;
        }

        /* 打印 payload 用于调试 */
        ESP_LOGI(TAG, "Payload[%d]: %.*s", len, len, payload);

        int msg_id = esp_mqtt_client_publish(g_mqtt_client, ONENET_TOPIC,
                                              payload, len, 0, 0);
        if (msg_id >= 0) {
            ESP_LOGI(TAG, "MQTT publish OK: %s HR=%.0f T=%.1f P=%.1f C=%d",
                     action, hr, temp, press, cadence);
        } else {
            ESP_LOGW(TAG, "MQTT publish failed (ret=%d)", msg_id);
        }
    }
}

/* ==================== 数据融合任务 ==================== */

static void data_fusion_task(void *arg)
{
    ankle_data_t ankle;
    int wait_cnt = 0;

    while (1) {
        if (xQueueReceive(g_ankle_data_queue, &ankle, pdMS_TO_TICKS(1000)) == pdTRUE) {
            wait_cnt = 0;
            g_latest_ankle = ankle;  /* 更新共享变量供 MQTT 读取 */
            ESP_LOGI("FUSION", "[%lu ms] Action=%s Cadence=%dspm "
                     "HR=%.1fbpm Temp=%.1fC Press=%.1fhPa",
                     (unsigned long)ankle.timestamp_ms,
                     ankle.action,
                     ankle.cadence_spm,
                     g_current_bpm,
                     g_current_temp,
                     g_current_press);
        } else {
            if (++wait_cnt >= 10) {
                ESP_LOGW("FUSION", "Waiting for ankle data... (connected=%d)",
                         g_ankle_connected);
                wait_cnt = 0;
            }
        }
    }
}

/* ==================== 主函数 ==================== */

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Smart Sports Watch Starting...");
    ESP_LOGI(TAG, "========================================");

    /* 1. NVS（NimBLE 和 WiFi 都需要） */
    nvs_init();

    /* 2. I2C 总线 + 传感器 */
    i2c_bus_config_t bus_cfg = {
        .port      = I2C_NUM_0,
        .sda_pin   = GPIO_NUM_5,
        .scl_pin   = GPIO_NUM_4,
        .clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_bus_init(&bus_cfg, &g_i2c_bus));
    ESP_ERROR_CHECK(bme280_init(g_i2c_bus, &g_bme280));
    ESP_ERROR_CHECK(max30102_init(g_i2c_bus, &g_max30102));

    /* 3. 创建数据交换队列 */
    g_ankle_data_queue = xQueueCreate(4, sizeof(ankle_data_t));
    g_cccd_sem = xSemaphoreCreateBinary();

#if !WIFI_ONLY_TEST
    /* 4. 初始化 NimBLE */
    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return;
    }
    ble_att_set_preferred_mtu(247);

    /* 5. 配置 GAP */
    ble_svc_gap_init();
    rc = ble_svc_gap_device_name_set("ESP32_S3_Watch");
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
        return;
    }

    /* 6. 设置 Host 回调 */
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    /* 7. 启动 NimBLE Host 任务 */
    nimble_port_freertos_init(nimble_host_task);
#endif

    /* 8. 传感器任务 */
    xTaskCreate(environment_sensor_task, "env_sensor", 4096, &g_bme280, 5, NULL);
    xTaskCreate(heart_rate_task, "heart_rate", 4096, &g_max30102, 4, NULL);

    /* 9. 数据融合任务 */
    xTaskCreate(data_fusion_task, "data_fusion", FUSION_TASK_STACK, NULL,
                FUSION_TASK_PRIO, NULL);

    /* 10. MQTT 上报任务（优先级最低，等待 MQTT 连接后自动发送） */
    xTaskCreate(mqtt_upload_task, "mqtt_upload", 4096, NULL, 1, NULL);

#if WIFI_ONLY_TEST
    /* WiFi 测试模式：直接启动 WiFi + MQTT */
    ESP_LOGI(TAG, "WIFI_ONLY_TEST mode, starting WiFi directly...");
    wifi_init();
    mqtt_init();
#else
    /* WiFi + MQTT 在 BLE 连接脚踝后自动启动（wifi_start_task） */
#endif

    ESP_LOGI(TAG, "All tasks created.");
}
