/**
 * @file main.c
 * @brief ESP32-S3 NimBLE UART-over-BLE (Phase 1: 纯蓝牙功能验证)
 *
 * 创建一个 Nordic UART Service (NUS) 风格的 BLE 服务：
 *   - RX 特征 (Write): 接收手机端指令 ('r' 启动, 's' 停止)
 *   - TX 特征 (Notify): 推送模拟 CSV 数据
 *   - MTU 自动协商，支持 ~50 字节数据包
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

/* NimBLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_att.h"

/* ==================== 常量定义 ==================== */

#define TAG "BLE_UART"
#define DEVICE_NAME "ESP32_Gait_Gatt"

/* Nordic UART Service UUID: 6E400001-B5A3-F393-E0A9-E50E24DCCA9E */
static const ble_uuid128_t gatt_uart_svc_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);

/* TX Characteristic (Notify): 6E400003-B5A3-F393-E0A9-E50E24DCCA9E */
static const ble_uuid128_t gatt_uart_tx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

/* RX Characteristic (Write): 6E400002-B5A3-F393-E0A9-E50E24DCCA9E */
static const ble_uuid128_t gatt_uart_rx_uuid =
    BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                     0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);

/* ==================== 全局状态 ==================== */

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_tx_attr_handle = 0;
static bool g_tx_subscribed = false;
static volatile bool g_task_running = false;
static TaskHandle_t g_sim_task_handle = NULL;

static char tx_buf[64];

/* ==================== 前向声明 ==================== */

static void ble_app_advertise(void);
static int ble_uart_gap_event(struct ble_gap_event *event, void *arg);
static int ble_uart_rx_write(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg);
static int ble_uart_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg);
static void sim_data_task(void *arg);
static void ble_on_reset(int reason);
static void ble_on_sync(void);
static void nimble_host_task(void *param);

/* ==================== GATT 服务定义 ==================== */

static const struct ble_gatt_chr_def gatt_uart_chars[] = {
    {
        /* RX: 手机写入指令 */
        .uuid = &gatt_uart_rx_uuid.u,
        .access_cb = ble_uart_rx_write,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        /* TX: 设备通过 Notify 推送数据 */
        .uuid = &gatt_uart_tx_uuid.u,
        .access_cb = ble_uart_tx_access,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &g_tx_attr_handle,
    },
    { 0 },
};

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
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
            ESP_LOGI(TAG, "Recv R: Start");
            if (!g_task_running) {
                g_task_running = true;
                xTaskCreate(sim_data_task, "sim_data", 4096, NULL, 5,
                            &g_sim_task_handle);
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

/* ==================== 数据分片发送 ==================== */

static void ble_uart_send_line(const char *data, int len)
{
    uint16_t mtu = ble_att_mtu(g_conn_handle);
    uint16_t max_payload = (mtu > 3) ? (mtu - 3) : 20;

    if (len <= max_payload) {
        ble_uart_tx_notify(data, len);
    } else {
        /* 分片发送：每片 max_payload 字节，片间延时等待缓冲区释放 */
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

/* ==================== 模拟数据任务 ==================== */

static void sim_data_task(void *arg)
{
    uint32_t timestamp = 0;
    bool pressure_toggle = false;

    ESP_LOGI(TAG, "Simulated data task started");

    while (g_task_running) {
        int len = snprintf(tx_buf, sizeof(tx_buf),
                           "%lu,1.0,2.0,3.0,0.1,0.2,0.3,%d\n",
                           (unsigned long)timestamp,
                           pressure_toggle ? 1 : 0);

        if (len > 0 && len < (int)sizeof(tx_buf)) {
            ble_uart_send_line(tx_buf, len);
        }

        timestamp += 500;
        pressure_toggle = !pressure_toggle;

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Simulated data task stopping");
    g_sim_task_handle = NULL;
    vTaskDelete(NULL);
}

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

/* ==================== 主函数 ==================== */

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-S3 BLE UART Starting...");
    ESP_LOGI(TAG, "  Device: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "========================================");

    /* 1. 初始化 NVS */
    nvs_init();

    /* 2. 初始化 NimBLE */
    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return;
    }

    /* 2.5 显式设置 preferred MTU */
    ble_att_set_preferred_mtu(247);
    ESP_LOGI(TAG, "Preferred MTU set to: 247");

    /* 3. 配置 GAP */
    ble_svc_gap_init();
    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
        return;
    }

    /* 4. 初始化 GATT */
    ble_svc_gatt_init();

    /* 5. 注册自定义 UART GATT 服务 */
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

    /* 6. 设置 Host 回调 */
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;

    /* 7. 启动 NimBLE Host 任务 */
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE stack initialized, waiting for sync...");
}
