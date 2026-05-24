#include "max30102.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <limits.h>

static const char *TAG = "MAX30102";

/* Register addresses */
#define MAX30102_REG_INT_STATUS_1    0x00
#define MAX30102_REG_INT_STATUS_2    0x01
#define MAX30102_REG_INT_ENABLE_1    0x02
#define MAX30102_REG_INT_ENABLE_2    0x03
#define MAX30102_REG_FIFO_WR_PTR     0x04
#define MAX30102_REG_OVF_COUNTER     0x05
#define MAX30102_REG_FIFO_RD_PTR     0x06
#define MAX30102_REG_FIFO_DATA       0x07
#define MAX30102_REG_FIFO_CONFIG     0x08
#define MAX30102_REG_MODE_CONFIG     0x09
#define MAX30102_REG_SPO2_CONFIG     0x0A
#define MAX30102_REG_LED1_PA         0x0C
#define MAX30102_REG_LED2_PA         0x0D
#define MAX30102_REG_PART_ID         0xFF

#define MAX30102_PART_ID_VALUE       0x15
#define MAX30102_MIN_RANGE           400
#define MAX30102_REFRACTORY_MS       350
#define MAX30102_PEAK_TIMEOUT_MS     3000
#define MAX30102_MODE_HEART_RATE     0x02
#define MAX30102_DEFAULT_LED_CURRENT 0x1F

static esp_err_t max30102_write_reg(max30102_handle_t *handle, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(handle->i2c_dev, buf, 2, 100);
}

static esp_err_t max30102_read_regs(max30102_handle_t *handle, uint8_t reg,
                                     uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, data, len, 100);
}

esp_err_t max30102_init(i2c_bus_handle_t bus, max30102_handle_t *handle)
{
    if (!bus || !handle) return ESP_ERR_INVALID_ARG;
    memset(handle, 0, sizeof(max30102_handle_t));

    esp_err_t ret;

    ret = i2c_bus_add_device(bus, MAX30102_I2C_ADDR, MAX30102_I2C_CLK_SPEED, &handle->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t part_id;
    ret = max30102_read_regs(handle, MAX30102_REG_PART_ID, &part_id, 1);
    if (ret != ESP_OK) return ret;
    if (part_id != MAX30102_PART_ID_VALUE) {
        ESP_LOGE(TAG, "Unknown part ID: 0x%02X (expected 0x15)", part_id);
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "MAX30102 detected, part ID: 0x%02X", part_id);

    /* Soft reset */
    ret = max30102_write_reg(handle, MAX30102_REG_MODE_CONFIG, 0x40);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    for (int i = 0; i < 10; i++) {
        uint8_t mode;
        max30102_read_regs(handle, MAX30102_REG_MODE_CONFIG, &mode, 1);
        if ((mode & 0x40) == 0) break;
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    /* Clear FIFO pointers */
    max30102_write_reg(handle, MAX30102_REG_FIFO_WR_PTR, 0x00);
    max30102_write_reg(handle, MAX30102_REG_OVF_COUNTER, 0x00);
    max30102_write_reg(handle, MAX30102_REG_FIFO_RD_PTR, 0x00);

    /* FIFO: 4x averaging + rollover enable */
    max30102_write_reg(handle, MAX30102_REG_FIFO_CONFIG, 0x50);

    /* SpO2 config: ADC range 4096nA, 100sps, 18-bit */
    max30102_write_reg(handle, MAX30102_REG_SPO2_CONFIG, 0x27);

    /* LED currents */
    handle->led_current_red = MAX30102_DEFAULT_LED_CURRENT;
    handle->led_current_ir  = MAX30102_DEFAULT_LED_CURRENT;
    max30102_write_reg(handle, MAX30102_REG_LED1_PA, handle->led_current_red);
    max30102_write_reg(handle, MAX30102_REG_LED2_PA, handle->led_current_ir);

    /* Disable interrupts (polling mode) */
    max30102_write_reg(handle, MAX30102_REG_INT_ENABLE_1, 0x00);
    max30102_write_reg(handle, MAX30102_REG_INT_ENABLE_2, 0x00);

    /* Clear pending interrupts */
    uint8_t dummy;
    max30102_read_regs(handle, MAX30102_REG_INT_STATUS_1, &dummy, 1);
    max30102_read_regs(handle, MAX30102_REG_INT_STATUS_2, &dummy, 1);

    /* Set Heart Rate mode */
    max30102_write_reg(handle, MAX30102_REG_MODE_CONFIG, MAX30102_MODE_HEART_RATE);

    handle->dc_offset = 0;
    handle->prev_ac = 0;
    handle->rising = true;
    handle->local_max = 0;
    handle->local_min = 0;
    handle->last_peak_tick = 0;
    handle->refractory_end_tick = 0;

    handle->initialized = true;
    ESP_LOGI(TAG, "MAX30102 initialized in Heart Rate mode");
    return ESP_OK;
}

esp_err_t max30102_read_fifo(max30102_handle_t *handle, int32_t *red_sample)
{
    if (!handle || !handle->initialized || !red_sample) return ESP_ERR_INVALID_ARG;

    uint8_t wr_ptr, rd_ptr;
    esp_err_t ret;

    ret = max30102_read_regs(handle, MAX30102_REG_FIFO_WR_PTR, &wr_ptr, 1);
    if (ret != ESP_OK) return ret;
    ret = max30102_read_regs(handle, MAX30102_REG_FIFO_RD_PTR, &rd_ptr, 1);
    if (ret != ESP_OK) return ret;

    int num_available;
    if (wr_ptr >= rd_ptr) {
        num_available = wr_ptr - rd_ptr;
    } else {
        num_available = (32 - rd_ptr) + wr_ptr;
    }

    if (num_available == 0) return ESP_ERR_NOT_FOUND;

    int32_t latest_sample = 0;
    for (int i = 0; i < num_available; i++) {
        uint8_t fifo_buf[3];
        ret = max30102_read_regs(handle, MAX30102_REG_FIFO_DATA, fifo_buf, 3);
        if (ret != ESP_OK) return ret;

        int32_t raw = ((int32_t)fifo_buf[0] << 16) |
                      ((int32_t)fifo_buf[1] << 8)  |
                      ((int32_t)fifo_buf[2]);
        raw &= 0x03FFFF;
        latest_sample = raw;
    }

    *red_sample = latest_sample;
    return ESP_OK;
}

void max30102_process_sample(max30102_handle_t *handle, int32_t sample)
{
    if (!handle || !handle->initialized) return;

    /* DC offset: exponential moving average (alpha = 1/64) */
    if (handle->dc_offset == 0) {
        handle->dc_offset = sample;
    } else {
        handle->dc_offset = handle->dc_offset + ((sample - handle->dc_offset) >> 6);
    }

    int32_t ac = sample - handle->dc_offset;
    bool now_rising = (ac > handle->prev_ac);

    /* Track local max during rising phase */
    if (now_rising) {
        if (ac > handle->local_max) {
            handle->local_max = ac;
        }
    }

    /* Track local min during falling phase */
    if (!now_rising) {
        if (ac < handle->local_min) {
            handle->local_min = ac;
        }
    }

    /* Rising → Falling transition: candidate peak */
    if (handle->rising && !now_rising) {
        TickType_t now = xTaskGetTickCount();
        int32_t range = handle->local_max - handle->local_min;

        if (range >= MAX30102_MIN_RANGE && now >= handle->refractory_end_tick) {
            if (handle->last_peak_tick != 0) {
                uint32_t interval_ms = (now - handle->last_peak_tick) * portTICK_PERIOD_MS;

                /* Valid HR interval: 300ms (200 BPM) to 1500ms (40 BPM) */
                if (interval_ms >= 300 && interval_ms <= 1500) {
                    float bpm = 60000.0f / interval_ms;

                    /* Outlier rejection: skip if deviates >40% from current */
                    if (handle->bpm_count > 0) {
                        float dev = bpm - handle->current_bpm;
                        if (dev < 0) dev = -dev;
                        if (dev > handle->current_bpm * 0.4f) {
                            goto skip_bpm;
                        }
                    }

                    handle->bpm_history[handle->bpm_idx] = bpm;
                    handle->bpm_idx = (handle->bpm_idx + 1) % 5;
                    if (handle->bpm_count < 5) handle->bpm_count++;

                    float sum = 0;
                    for (int i = 0; i < handle->bpm_count; i++) {
                        sum += handle->bpm_history[i];
                    }
                    handle->current_bpm = sum / handle->bpm_count;
                }
skip_bpm:;
            }
            handle->last_peak_tick = now;
            handle->refractory_end_tick = now + pdMS_TO_TICKS(MAX30102_REFRACTORY_MS);

            /* Reset for next cycle */
            handle->local_min = ac;
        }
    }

    /* No peak timeout: clear BPM */
    if (handle->last_peak_tick != 0) {
        TickType_t now = xTaskGetTickCount();
        uint32_t elapsed = (now - handle->last_peak_tick) * portTICK_PERIOD_MS;
        if (elapsed > MAX30102_PEAK_TIMEOUT_MS) {
            handle->current_bpm = 0.0f;
            handle->bpm_count = 0;
        }
    }

    handle->rising = now_rising;
    handle->prev_ac = ac;
}

float max30102_get_bpm(max30102_handle_t *handle)
{
    if (!handle) return 0.0f;
    return handle->current_bpm;
}

esp_err_t max30102_set_led_current(max30102_handle_t *handle, uint8_t red_ma, uint8_t ir_ma)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    esp_err_t ret;
    ret = max30102_write_reg(handle, MAX30102_REG_LED1_PA, red_ma);
    if (ret != ESP_OK) return ret;
    ret = max30102_write_reg(handle, MAX30102_REG_LED2_PA, ir_ma);
    if (ret == ESP_OK) {
        handle->led_current_red = red_ma;
        handle->led_current_ir = ir_ma;
    }
    return ret;
}

esp_err_t max30102_soft_reset(max30102_handle_t *handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    return max30102_write_reg(handle, MAX30102_REG_MODE_CONFIG, 0x40);
}
