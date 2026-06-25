/**
 * @file ble_peripheral.c
 * @brief BLE Peripheral 模块 — NUS 服务、GATT/GAP、广播、Notify、分片发送
 */

#include "ble_peripheral.h"
#include "gait_sensor.h"
#include "gait_inference.h"
#include <string.h>

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_att.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_UART";

/* ==================== NUS UUID ==================== */

static const ble_uuid128_t gatt_uart_svc_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);

static const ble_uuid128_t gatt_uart_tx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

static const ble_uuid128_t gatt_uart_rx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);

/* ==================== 前向声明 ==================== */

static void ble_app_advertise(void);
static int ble_uart_gap_event(struct ble_gap_event *event, void *arg);

/* ==================== GATT 服务定义 ==================== */

static struct ble_gatt_chr_def gatt_uart_chars[] = {
    {
        .uuid = &gatt_uart_rx_uuid.u,
        .access_cb = NULL,  /* 填充在 ble_peripheral_init 中 */
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &gatt_uart_tx_uuid.u,
        .access_cb = NULL,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = NULL,  /* 填充在 ble_peripheral_init 中 */
    },
    { 0 },
};

static struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_uart_svc_uuid.u,
        .characteristics = gatt_uart_chars,
    },
    { 0 },
};

/* ==================== Notify 发送 ==================== */

static int ble_uart_tx_notify(const char *data, uint16_t len)
{
    if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE || !g_tx_subscribed) {
        return BLE_HS_ENOTCONN;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mbuf");
        return BLE_HS_ENOMEM;
    }

    return ble_gatts_notify_custom(g_conn_handle, g_tx_attr_handle, om);
}

void ble_uart_send_line(const char *data, int len)
{
    uint16_t mtu = ble_att_mtu(g_conn_handle);
    uint16_t max_payload = (mtu > 3) ? (mtu - 3) : 20;

    if (len <= max_payload) {
        ble_uart_tx_notify(data, len);
    } else {
        int offset = 0;
        while (offset < len) {
            int chunk = len - offset;
            if (chunk > max_payload) {
                chunk = max_payload;
            }
            ble_uart_tx_notify(data + offset, chunk);
            offset += chunk;
            if (offset < len) {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
    }
}

/* ==================== GATT 回调 ==================== */

static int ble_uart_rx_write(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    char cmd[16] = {0};

    if (len > 0 && len < sizeof(cmd)) {
        os_mbuf_copydata(ctxt->om, 0, len, cmd);
    }

    ESP_LOGI(TAG, "RX received: '%.*s' (len=%d)", len, cmd, len);

    if (len >= 1) {
        switch (cmd[0]) {
        case 'r':
            ESP_LOGI(TAG, "Recv R: Start sensor + inference");
            if (!g_task_running) {
                g_task_running = true;
                xTaskCreate(sensor_data_task, "sensor_data", 8192, NULL, 5,
                            &g_sim_task_handle);
                xTaskCreate(inference_task, "inference", 20480, NULL, 4,
                            &g_inference_handle);
            }
            break;
        case 's':
            ESP_LOGI(TAG, "Recv S: Stop");
            g_task_running = false;
            break;
        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02x", cmd[0]);
            break;
        }
    }

    return 0;
}

static int ble_uart_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;
}

/* ==================== GAP 回调 ==================== */

static int ble_uart_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "Connection %s; handle=%d",
                 event->connect.status == 0 ? "established" : "failed",
                 event->connect.conn_handle);

        if (event->connect.status != 0) {
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_app_advertise();
            return 0;
        }

        g_conn_handle = event->connect.conn_handle;
        g_tx_subscribed = false;
        ESP_LOGI(TAG, "Current ATT MTU: %d", ble_att_mtu(g_conn_handle));
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnect; reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_tx_subscribed = false;
        g_task_running = false;
        ble_app_advertise();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe; attr_handle=%d, cur_notify=%d",
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify);

        if (event->subscribe.attr_handle == g_tx_attr_handle) {
            g_tx_subscribed = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "TX notifications %s",
                     g_tx_subscribed ? "enabled" : "disabled");
        }
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated; conn_handle=%d, mtu=%d",
                 event->mtu.conn_handle,
                 event->mtu.value);
        return 0;

    default:
        return 0;
    }
}

/* ==================== 广播 ==================== */

static void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_GAP_ADV_FAST_INTERVAL1_MIN;
    adv_params.itvl_max = BLE_GAP_ADV_FAST_INTERVAL1_MAX;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_uart_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
    }
}

/* ==================== NimBLE Host 回调 ==================== */

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset; reason=%d", reason);
}

static void ble_on_sync(void)
{
    int rc;

    ESP_LOGI(TAG, "BLE host synced");

    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }

    ble_app_advertise();
}

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_deinit();
}

/* ==================== 公开接口 ==================== */

void ble_peripheral_init(void)
{
    /* 填充 GATT 回调和 val_handle */
    gatt_uart_chars[0].access_cb = ble_uart_rx_write;
    gatt_uart_chars[1].access_cb = ble_uart_tx_access;
    gatt_uart_chars[1].val_handle = &g_tx_attr_handle;

    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return;
    }

    ble_att_set_preferred_mtu(247);
    ESP_LOGI(TAG, "Preferred MTU set to: 247");

    ble_svc_gap_init();
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
        return;
    }

    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return;
    }

    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE stack initialized, waiting for sync...");
}
