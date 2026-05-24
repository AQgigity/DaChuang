#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
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

/* BLE 数据拼接缓冲区 */
static char g_rx_buf[128];
static int  g_rx_len = 0;

/* 前向声明 */
static void ble_start_scan(void);
static int ble_central_gap_event(struct ble_gap_event *event, void *arg);

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

/* ==================== 数据融合任务 ==================== */

static void data_fusion_task(void *arg)
{
    ankle_data_t ankle;
    int wait_cnt = 0;

    while (1) {
        if (xQueueReceive(g_ankle_data_queue, &ankle, pdMS_TO_TICKS(1000)) == pdTRUE) {
            wait_cnt = 0;
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

    /* 1. NVS（NimBLE 需要） */
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

    /* 8. 传感器任务 */
    xTaskCreate(environment_sensor_task, "env_sensor", 4096, &g_bme280, 5, NULL);
    xTaskCreate(heart_rate_task, "heart_rate", 4096, &g_max30102, 4, NULL);

    /* 9. 数据融合任务 */
    xTaskCreate(data_fusion_task, "data_fusion", FUSION_TASK_STACK, NULL,
                FUSION_TASK_PRIO, NULL);

    ESP_LOGI(TAG, "All tasks created. Scanning for ankle...");
}
