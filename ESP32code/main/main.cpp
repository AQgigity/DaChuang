/**
 * @file main.cpp
 * @brief ESP32-S3 步态分析固件 — 主函数（初始化 + FSR 测试）
 *
 * 模块架构：
 *   gait_common.h       — 共享数据结构和全局变量
 *   ble_peripheral.c/h  — BLE Peripheral（NUS/GATT/GAP/广播/分片）
 *   gait_inference.cpp/h — Edge Impulse 推理（C++）
 *   gait_sensor.c/h     — 传感器采集+步频+发力检测
 */

#include <cstdio>
#include <cstring>

extern "C" {
#include "gait_common.h"
#include "ble_peripheral.h"
#include "gait_sensor.h"
}

#include "gait_inference.h"

static const char *TAG = "BLE_UART";

/* ==================== 全局变量定义（extern 声明在 gait_common.h） ==================== */

uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
uint16_t g_tx_attr_handle = 0;
bool g_tx_subscribed = false;
volatile bool g_task_running = false;
TaskHandle_t g_sim_task_handle = NULL;
TaskHandle_t g_inference_handle = NULL;

mpu6050_handle_t g_mpu6050_handle = {0};

float ei_buf[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];
SemaphoreHandle_t ei_sem = NULL;

volatile float cadence_avg_spm = 0.0f;
char gait_style[32] = "未发力";

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

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ESP32-S3 BLE + Edge Impulse Starting...");
    ESP_LOGI(TAG, "  Device: %s", DEVICE_NAME);
    ESP_LOGI(TAG, "========================================");

    /* 1. 初始化 NVS */
    nvs_init();

    /* 2. 初始化 NimBLE + BLE Peripheral */
    ble_peripheral_init();

    /* 3. 初始化 MPU6050 */
    if (mpu6050_init(&g_mpu6050_handle) != ESP_OK) {
        ESP_LOGE(TAG, "MPU6050 init failed!");
    } else {
        ESP_LOGI(TAG, "MPU6050 initialized");
    }

    /* 4. 初始化 FSR402 Heel */
    fsr402_config_t fsr_cfg = {
        .channel = ADC_CHANNEL_4,
        .atten   = ADC_ATTEN_DB_12,
    };
    if (fsr402_init(&fsr_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "FSR402 init failed!");
    } else {
        ESP_LOGI(TAG, "FSR402 initialized");
    }

    /* 5. 初始化 FSR402 Toe */
    if (fsr402_toe_init(ADC_ATTEN_DB_12) != ESP_OK) {
        ESP_LOGE(TAG, "FSR402 toe init failed!");
    } else {
        ESP_LOGI(TAG, "FSR402 toe initialized");
    }

#if FSR_TEST_MODE
    /* === FSR 传感器测试 === */
    ESP_LOGW(TAG, "===== FSR TEST MODE: press heel/toe to verify =====");
    for (int i = 0; i < 50; i++) {
        uint16_t heel_raw = fsr402_read_raw();
        uint16_t toe_raw  = fsr402_toe_read_raw();
        bool heel_p = fsr402_is_pressed();
        bool toe_p  = fsr402_toe_is_pressed();
        ESP_LOGI(TAG, "[%2d] Heel: ADC=%4d (%s)  |  Toe: ADC=%4d (%s)",
                 i, heel_raw, heel_p ? "PRESSED" : "-----",
                 toe_raw,  toe_p  ? "PRESSED" : "-----");
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    ESP_LOGW(TAG, "===== FSR TEST DONE, continuing normal boot =====");
#endif

    /* 6. 创建推理信号量 */
    ei_sem = xSemaphoreCreateBinary();
    if (ei_sem == NULL) {
        ESP_LOGE(TAG, "Failed to create inference semaphore!");
    }

    ESP_LOGI(TAG, "System ready. Send 'r' to start, 's' to stop.");
}
