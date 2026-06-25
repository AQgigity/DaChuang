/**
 * @file main.c
 * @brief 智能运动手表 — 主函数（初始化 + 任务创建）
 *
 * 模块架构：
 *   watch_common.h   — 共享数据结构和全局变量
 *   ble_central.c/h  — BLE Central（扫描/连接/NUS/通知/解析）
 *   wifi_mqtt.c/h    — WiFi + MQTT + OneNET 云平台
 *   sensor_task.c/h  — BME280 + MAX30102 传感器任务
 *   display_task.c/h — LVGL + UI 刷新任务
 *   emg_sensor.c/h   — EMG 肌电采集模块
 *   data_fusion.c/h  — 数据融合任务
 */

#include "watch_common.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "display.h"
#include "ui.h"

#include "ble_central.h"
#include "wifi_mqtt.h"
#include "sensor_task.h"
#include "display_task.h"
#include "data_fusion.h"

#if ENABLE_EMG
#include "emg_sensor.h"
#endif

static const char *TAG = "WATCH";

/* ==================== 全局变量定义（extern 声明在 watch_common.h） ==================== */

i2c_bus_handle_t  g_i2c_bus;
bme280_handle_t   g_bme280;
max30102_handle_t g_max30102;

volatile float g_current_bpm = 0.0f;
volatile float g_current_temp = 0.0f;
volatile float g_current_press = 0.0f;

volatile ankle_data_t g_latest_ankle = {0};
QueueHandle_t g_ankle_data_queue = NULL;

volatile bool g_ankle_connected = false;
uint16_t g_ankle_conn_handle = BLE_HS_CONN_HANDLE_NONE;
uint16_t g_nus_tx_val_handle = 0;
uint16_t g_nus_rx_val_handle = 0;
uint16_t g_nus_svc_start_handle = 0;
uint16_t g_nus_svc_end_handle = 0;

volatile bool g_wifi_connected = false;
volatile bool g_mqtt_connected = false;
volatile bool g_upload_enabled = false;

SemaphoreHandle_t g_lvgl_mutex = NULL;

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

#if ENABLE_EMG
    emg_init();
#endif

    /* 3. 创建数据交换队列和互斥锁 */
    g_ankle_data_queue = xQueueCreate(4, sizeof(ankle_data_t));
    g_lvgl_mutex = xSemaphoreCreateMutex();
    wifi_mqtt_create_cccd_sem();

#if !WIFI_ONLY_TEST
    /* 4. 初始化 BLE Central */
    ble_central_init();
#endif

    /* 5. 显示 + UI */
    display_init();
    ui_init();

    /* 6. 传感器任务 */
    xTaskCreate(environment_sensor_task, "env_sensor", 4096, &g_bme280, 5, NULL);
    xTaskCreate(heart_rate_task, "heart_rate", 4096, &g_max30102, 4, NULL);
#if ENABLE_EMG
    xTaskCreate(emg_collect_task, "emg_collect", 4096, NULL, 3, NULL);
#endif

    /* 7. LVGL + UI 刷新任务 */
    xTaskCreate(lvgl_task, "lvgl", 4096, NULL, 2, NULL);
    xTaskCreate(ui_refresh_task, "ui_refresh", 4096, NULL, 2, NULL);

    /* 8. 数据融合任务 */
    xTaskCreate(data_fusion_task, "data_fusion", FUSION_TASK_STACK, NULL,
                FUSION_TASK_PRIO, NULL);

    /* 9. MQTT 上报任务 */
    xTaskCreate(mqtt_upload_task, "mqtt_upload", 4096, NULL, 1, NULL);

    /* 10. BLE 扫描重试任务 */
    xTaskCreate(ble_scan_retry_task, "ble_retry", 2048, NULL, 1, NULL);

#if WIFI_ONLY_TEST
    ESP_LOGI(TAG, "WIFI_ONLY_TEST mode, starting WiFi directly...");
    wifi_init();
    mqtt_init();
#endif

    ESP_LOGI(TAG, "All tasks created.");
}
