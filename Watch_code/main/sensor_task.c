/**
 * @file sensor_task.c
 * @brief 传感器 FreeRTOS 任务（BME280 环境 + MAX30102 心率）
 */

#include "sensor_task.h"

static const char *TAG = "SENSOR";

void environment_sensor_task(void *arg)
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
                ESP_LOGI(TAG, "Temp: %.2f C, Press: %.2f hPa",
                         data.temperature, data.pressure);
                log_cnt = 0;
            }
        }
        vTaskDelayUntil(&last_wake, period);
    }
}

void heart_rate_task(void *arg)
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
                ESP_LOGI(TAG, "BPM: %.1f", handle->current_bpm);
                log_cnt = 0;
            }
        }
        vTaskDelayUntil(&last_wake, period);
    }
}
