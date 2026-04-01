/**
 * @file main.c
 * @brief ESP32-S3 传感器驱动DEMO
 *        驱动 FSR402 压力传感器 和 MPU6050 陀螺仪加速度计
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "fsr402.h"
#include "mpu6050.h"

/* ==================== 配置定义 ==================== */
#define SENSOR_TASK_STACK_SIZE  4096
#define SENSOR_TASK_PRIORITY    5
#define SENSOR_SAMPLE_INTERVAL_MS 50  /* 20Hz采样率 */

#define TAG_MAIN "MAIN"

/* ==================== 全局变量 ==================== */
static mpu6050_handle_t mpu6050_handle = {0};
static fsr402_config_t fsr402_config = {
    .channel = FSR402_ADC_CHANNEL,
    .atten = FSR402_ADC_ATTEN,
};

/* ==================== 任务函数 ==================== */

/**
 * @brief 传感器数据采集任务
 */
static void sensor_task(void *arg)
{
    esp_err_t ret;

    /* 初始化FSR402 */
    ret = fsr402_init(&fsr402_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "FSR402 initialization failed!");
        vTaskDelete(NULL);
        return;
    } else {
        ESP_LOGI(TAG_MAIN, "FSR402 initialized successfully");
    }

    /* 初始化MPU6050 */
    ret = mpu6050_init(&mpu6050_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_MAIN, "MPU6050 initialization failed!");
        vTaskDelete(NULL);
        return;
    } else {
        ESP_LOGI(TAG_MAIN, "MPU6050 initialized successfully");
    }

    /* 等待系统稳定 */
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* 执行MPU6050校准 */
    ESP_LOGW(TAG_MAIN, "========================================");
    ESP_LOGW(TAG_MAIN, "  CALIBRATION IN PROGRESS");
    ESP_LOGW(TAG_MAIN, "  Please keep the device still!");
    ESP_LOGW(TAG_MAIN, "========================================");
    mpu6050_calibrate(&mpu6050_handle);
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG_MAIN, "========================================");
    ESP_LOGI(TAG_MAIN, "  SENSOR DEMO STARTED");
    ESP_LOGI(TAG_MAIN, "  Sampling rate: 20Hz");
    ESP_LOGI(TAG_MAIN, "========================================");

    uint32_t sample_count = 0;

    /* 主循环 - 采集传感器数据 */
    while (1) {
        sample_count++;

        /* 读取FSR402数据 */
        fsr402_data_t fsr_data;
        if (fsr402_read(&fsr_data) == ESP_OK) {
            ESP_LOGI(TAG_MAIN, "[%ld] FSR402: Raw=%4d, Voltage=%5.0fmV, Pressure=%5.2fkg",
                     sample_count, fsr_data.raw_value, fsr_data.voltage_mv, fsr_data.pressure_kg);
        }

        /* 读取MPU6050数据 */
        mpu6050_data_t mpu_data;
        if (mpu6050_read_data(&mpu6050_handle, &mpu_data) == ESP_OK) {
            mpu6050_print_data(&mpu_data);
        }

        ESP_LOGI(TAG_MAIN, "----------------------------------------");

        vTaskDelay(pdMS_TO_TICKS(SENSOR_SAMPLE_INTERVAL_MS));
    }
}

/* ==================== 主函数 ==================== */

void app_main(void)
{
    ESP_LOGI(TAG_MAIN, "========================================");
    ESP_LOGI(TAG_MAIN, "  ESP32-S3 Sensor Demo Starting...");
    ESP_LOGI(TAG_MAIN, "========================================");
    ESP_LOGI(TAG_MAIN, "ESP-IDF version: %s", esp_get_idf_version());
    ESP_LOGI(TAG_MAIN, "Target chip: %s", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG_MAIN, "CPU cores: %d", portNUM_PROCESSORS);
    ESP_LOGI(TAG_MAIN, "");
    ESP_LOGI(TAG_MAIN, "Sensor Configuration:");
    ESP_LOGI(TAG_MAIN, "  FSR402: ADC1_CH0 (GPIO1)");
    ESP_LOGI(TAG_MAIN, "  MPU6050: I2C0 (SDA=GPIO8, SCL=GPIO9, Addr=0x68)");
    ESP_LOGI(TAG_MAIN, "");

    /* 创建传感器采集任务 */
    BaseType_t ret = xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK_SIZE, NULL,
                      SENSOR_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG_MAIN, "Failed to create sensor task");
    }

    ESP_LOGI(TAG_MAIN, "Tasks created, waiting for sensors...");
}
