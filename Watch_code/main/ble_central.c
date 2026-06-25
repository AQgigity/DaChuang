/**
 * @file ble_central.c
 * @brief BLE Central 模块 — 扫描、连接、NUS 服务发现、通知接收、数据解析
 */

#include "ble_central.h"
#include "wifi_mqtt.h"
#include <string.h>
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "BLE";

/* ==================== NUS UUID（与脚踝端一致） ==================== */

static const ble_uuid128_t nus_svc_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);

static const ble_uuid128_t nus_tx_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

static const ble_uuid128_t nus_rx_uuid = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);

/* 模块私有状态 */
static char g_rx_buf[128];
static int  g_rx_len = 0;

/* ==================== 前向声明 ==================== */

static int ble_central_gap_event(struct ble_gap_event *event, void *arg);

/* ==================== 数据解析 ==================== */

static void parse_and_enqueue(const char *msg)
{
    ESP_LOGI("PARSE", "Received: %s", msg);

    ankle_data_t data = {0};
    data.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);

    char *action_start = strstr(msg, "：");
    char *cadence_start = strstr(msg, "步频：");
    char *gait_start = strstr(msg, "发力：");

    if (action_start && cadence_start && gait_start) {
        action_start += 3;
        char *paren = strstr(action_start, "(");
        if (paren && (paren - action_start) < (int)sizeof(data.action) - 1) {
            memcpy(data.action, action_start, paren - action_start);
            data.action[paren - action_start] = '\0';
        }

        if (paren && *(paren + 1)) {
            data.confidence = atoi(paren + 1);
        }

        cadence_start += 9;
        data.cadence_spm = atoi(cadence_start);

        gait_start += 9;
        char *fali = strstr(gait_start, "发力");
        if (fali) {
            int len = fali - gait_start;
            if (len >= (int)sizeof(data.gait_style)) len = sizeof(data.gait_style) - 1;
            memcpy(data.gait_style, gait_start, len);
            data.gait_style[len] = '\0';
        } else {
            snprintf(data.gait_style, sizeof(data.gait_style), "%s", gait_start);
        }

        data.valid = true;
        ESP_LOGI("PARSE", "Parsed: action=%s confidence=%d%% cadence=%d gait=%s",
                 data.action, data.confidence, data.cadence_spm, data.gait_style);
        xQueueSend(g_ankle_data_queue, &data, 0);
    } else {
        ESP_LOGW("PARSE", "Failed to parse message: action=%p cadence=%p gait=%p",
                 action_start, cadence_start, gait_start);
    }
}

/* ==================== 通知回调（带分片拼接） ==================== */

static int ble_central_on_notify(struct ble_gap_event *event)
{
    uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
    if (len == 0) return 0;

    if (g_rx_len + len < (int)sizeof(g_rx_buf) - 1) {
        os_mbuf_copydata(event->notify_rx.om, 0, len, g_rx_buf + g_rx_len);
        g_rx_len += len;
        g_rx_buf[g_rx_len] = '\0';
    } else {
        g_rx_len = 0;
        return 0;
    }

    char *nl;
    while ((nl = memchr(g_rx_buf, '\n', g_rx_len)) != NULL) {
        *nl = '\0';
        parse_and_enqueue(g_rx_buf);
        int consumed = (nl - g_rx_buf) + 1;
        g_rx_len -= consumed;
        memmove(g_rx_buf, g_rx_buf + consumed, g_rx_len);
    }

    return 0;
}

/* ==================== BLE 服务/特征发现 ==================== */

static int ble_central_on_cccd_write(uint16_t conn_handle,
                                     const struct ble_gatt_error *error,
                                     struct ble_gatt_attr *attr, void *arg)
{
    if (error->status != 0) {
        ESP_LOGE(TAG, "CCCD write failed: %d", error->status);
        return 0;
    }
    ESP_LOGI(TAG, "TX notifications enabled");

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

/* ==================== GAP 事件处理 ==================== */

static int ble_central_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
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

        xTaskCreate(wifi_start_task, "wifi_start", 4096, NULL, 3, NULL);

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

/* ==================== 公开接口 ==================== */

void ble_central_init(void)
{
    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return;
    }
    ble_att_set_preferred_mtu(247);

    ble_svc_gap_init();
    rc = ble_svc_gap_device_name_set("ESP32_S3_Watch");
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
        return;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(nimble_host_task);
}

void ble_start_scan(void)
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

void ble_scan_retry_task(void *arg)
{
    int retry_count = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));

        if (!g_ankle_connected &&
            g_ankle_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            retry_count++;
            if (retry_count % 3 == 1) {
                ESP_LOGI(TAG, "BLE not connected, restarting scan... (retry #%d)", retry_count);
            }
            ble_start_scan();
        } else {
            retry_count = 0;
        }
    }
}
