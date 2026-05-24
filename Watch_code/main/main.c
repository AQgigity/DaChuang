#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "i2c_bus.h"
#include "bme280.h"
#include "max30102.h"

static const char *TAG = "MAIN";

static i2c_bus_handle_t  g_i2c_bus;
static bme280_handle_t   g_bme280;
static max30102_handle_t g_max30102;

/* ---------- Sensor tasks ---------- */

static void environment_sensor_task(void *arg)
{
    bme280_handle_t *handle = (bme280_handle_t *)arg;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(100);
    bme280_data_t data;

    while (1) {
        if (bme280_read_data(handle, &data) == ESP_OK) {
            if (data.sensor_type == BME280_SENSOR_BME280) {
                ESP_LOGI("ENV", "Temp: %.2f C, Hum: %.2f %%, Press: %.2f hPa",
                         data.temperature, data.humidity, data.pressure);
            } else {
                ESP_LOGI("ENV", "Temp: %.2f C, Press: %.2f hPa",
                         data.temperature, data.pressure);
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
    int dbg_cnt = 0;

    while (1) {
        int32_t red_sample;
        if (max30102_read_fifo(handle, &red_sample) == ESP_OK) {
            max30102_process_sample(handle, red_sample);
            float bpm = max30102_get_bpm(handle);
            if (bpm > 0.0f) {
                ESP_LOGI("HR", "BPM: %.1f", bpm);
            }
            if (++dbg_cnt >= 50) {
                ESP_LOGI("HR", "raw=%ld dir=%s max=%ld min=%ld bpm=%.0f",
                         (long)red_sample,
                         handle->rising ? "UP" : "DN",
                         (long)handle->local_max,
                         (long)handle->local_min,
                         handle->current_bpm);
                dbg_cnt = 0;
            }
        }
        vTaskDelayUntil(&last_wake, period);
    }
}

/* ---------- App entry ---------- */

void app_main(void)
{
    ESP_LOGI(TAG, "Smart Sports Watch starting...");

    /* I2C bus */
    i2c_bus_config_t bus_cfg = {
        .port      = I2C_NUM_0,
        .sda_pin   = GPIO_NUM_5,
        .scl_pin   = GPIO_NUM_4,
        .clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_bus_init(&bus_cfg, &g_i2c_bus));

    /* Sensors */
    ESP_ERROR_CHECK(bme280_init(g_i2c_bus, &g_bme280));
    ESP_ERROR_CHECK(max30102_init(g_i2c_bus, &g_max30102));

    /* Sensor tasks */
    xTaskCreate(environment_sensor_task, "env_sensor", 4096, &g_bme280, 5, NULL);
    xTaskCreate(heart_rate_task, "heart_rate", 4096, &g_max30102, 4, NULL);

    ESP_LOGI(TAG, "All tasks created");
}
