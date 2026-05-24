#include "bme280.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "BME280";

/* Register addresses */
#define BME280_REG_CHIP_ID      0xD0
#define BME280_REG_RESET        0xE0
#define BME280_REG_STATUS       0xF3
#define BME280_REG_CTRL_HUM     0xF2
#define BME280_REG_CTRL_MEAS    0xF4
#define BME280_REG_CONFIG       0xF5
#define BME280_REG_PRESS_MSB    0xF7

#define BME280_REG_CALIB_T1_LSB 0x88
#define BME280_REG_CALIB_H1     0xA1
#define BME280_REG_CALIB_H2_LSB 0xE1

#define BME280_SOFT_RESET_CMD   0xB6

#define BME280_OS_1X            0x01
#define BME280_OS_SKIP          0x00
#define BME280_MODE_FORCED      0x01

static esp_err_t bme280_write_reg(bme280_handle_t *handle, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(handle->i2c_dev, buf, 2, 100);
}

static esp_err_t bme280_read_regs(bme280_handle_t *handle, uint8_t reg,
                                   uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, data, len, 100);
}

static esp_err_t bme280_read_calib_data(bme280_handle_t *handle)
{
    uint8_t buf[26];
    esp_err_t ret;

    ret = bme280_read_regs(handle, BME280_REG_CALIB_T1_LSB, buf, 26);
    if (ret != ESP_OK) return ret;

    handle->calib.dig_T1 = (uint16_t)(buf[1] << 8 | buf[0]);
    handle->calib.dig_T2 = (int16_t)(buf[3] << 8 | buf[2]);
    handle->calib.dig_T3 = (int16_t)(buf[5] << 8 | buf[4]);

    handle->calib.dig_P1 = (uint16_t)(buf[7] << 8 | buf[6]);
    handle->calib.dig_P2 = (int16_t)(buf[9] << 8 | buf[8]);
    handle->calib.dig_P3 = (int16_t)(buf[11] << 8 | buf[10]);
    handle->calib.dig_P4 = (int16_t)(buf[13] << 8 | buf[12]);
    handle->calib.dig_P5 = (int16_t)(buf[15] << 8 | buf[14]);
    handle->calib.dig_P6 = (int16_t)(buf[17] << 8 | buf[16]);
    handle->calib.dig_P7 = (int16_t)(buf[19] << 8 | buf[18]);
    handle->calib.dig_P8 = (int16_t)(buf[21] << 8 | buf[20]);
    handle->calib.dig_P9 = (int16_t)(buf[23] << 8 | buf[22]);

    handle->calib.dig_H1 = buf[25];

    if (handle->sensor_type == BME280_SENSOR_BME280) {
        uint8_t h_buf[7];
        ret = bme280_read_regs(handle, BME280_REG_CALIB_H2_LSB, h_buf, 7);
        if (ret != ESP_OK) return ret;

        handle->calib.dig_H2 = (int16_t)(h_buf[1] << 8 | h_buf[0]);
        handle->calib.dig_H3 = h_buf[2];
        handle->calib.dig_H4 = (int16_t)((h_buf[3] << 4) | (h_buf[4] & 0x0F));
        handle->calib.dig_H5 = (int16_t)((h_buf[5] << 4) | ((h_buf[4] >> 4) & 0x0F));
        handle->calib.dig_H6 = (int8_t)h_buf[6];
    }

    return ESP_OK;
}

static esp_err_t bme280_configure(bme280_handle_t *handle)
{
    esp_err_t ret;

    uint8_t hum_os = (handle->sensor_type == BME280_SENSOR_BME280) ? BME280_OS_1X : BME280_OS_SKIP;
    ret = bme280_write_reg(handle, BME280_REG_CTRL_HUM, hum_os);
    if (ret != ESP_OK) return ret;

    uint8_t ctrl_meas = (BME280_OS_1X << 5) | (BME280_OS_1X << 2) | BME280_MODE_FORCED;
    ret = bme280_write_reg(handle, BME280_REG_CTRL_MEAS, ctrl_meas);
    if (ret != ESP_OK) return ret;

    return bme280_write_reg(handle, BME280_REG_CONFIG, 0x00);
}

/* Bosch compensation: returns temperature in 0.01 deg C units, sets t_fine */
static int32_t bme280_compensate_temperature(bme280_handle_t *handle, int32_t adc_T)
{
    int32_t var1, var2, T;

    var1 = ((((adc_T >> 3) - ((int32_t)handle->calib.dig_T1 << 1)))
            * ((int32_t)handle->calib.dig_T2)) >> 11;

    var2 = (((((adc_T >> 4) - ((int32_t)handle->calib.dig_T1))
              * ((adc_T >> 4) - ((int32_t)handle->calib.dig_T1))) >> 12)
            * ((int32_t)handle->calib.dig_T3)) >> 14;

    handle->t_fine = var1 + var2;
    T = (handle->t_fine * 5 + 128) >> 8;

    return T;
}

/* Bosch compensation: returns pressure in Q24.8 format (Pa * 256) */
static uint32_t bme280_compensate_pressure(bme280_handle_t *handle, int32_t adc_P)
{
    int64_t var1, var2, p;

    var1 = ((int64_t)handle->t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)handle->calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)handle->calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)handle->calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)handle->calib.dig_P3) >> 8)
           + ((var1 * (int64_t)handle->calib.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)handle->calib.dig_P1) >> 33;

    if (var1 == 0) return 0;

    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)handle->calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)handle->calib.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)handle->calib.dig_P7) << 4);

    return (uint32_t)p;
}

/* Bosch compensation: returns humidity in %RH * 1024 */
static uint32_t bme280_compensate_humidity(bme280_handle_t *handle, int32_t adc_H)
{
    int32_t v_x1_u32r;

    v_x1_u32r = (handle->t_fine - ((int32_t)76800));

    v_x1_u32r = (((((adc_H << 14) - (((int32_t)handle->calib.dig_H4) << 20)
                    - ((int32_t)handle->calib.dig_H5 * v_x1_u32r))
                   + ((int32_t)16384)) >> 15)
                 * (((((((v_x1_u32r * ((int32_t)handle->calib.dig_H6)) >> 10)
                        * (((v_x1_u32r * ((int32_t)handle->calib.dig_H3)) >> 11)
                           + ((int32_t)32768))) >> 10)
                      + ((int32_t)2097152)) * ((int32_t)handle->calib.dig_H2) + 8192) >> 14));

    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7)
                                * ((int32_t)handle->calib.dig_H1)) >> 4));

    v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
    v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;

    return (uint32_t)(v_x1_u32r >> 12);
}

esp_err_t bme280_init(i2c_bus_handle_t bus, bme280_handle_t *handle)
{
    if (!bus || !handle) return ESP_ERR_INVALID_ARG;
    memset(handle, 0, sizeof(bme280_handle_t));

    esp_err_t ret;

    ret = i2c_bus_add_device(bus, BME280_I2C_ADDR, BME280_I2C_CLK_SPEED, &handle->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device to bus: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t chip_id;
    ret = bme280_read_regs(handle, BME280_REG_CHIP_ID, &chip_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %s", esp_err_to_name(ret));
        return ret;
    }

    if (chip_id == BME280_CHIP_ID_BME280) {
        handle->sensor_type = BME280_SENSOR_BME280;
        ESP_LOGI(TAG, "BME280 detected (ID=0x%02X), humidity available", chip_id);
    } else if (chip_id == BME280_CHIP_ID_BMP280) {
        handle->sensor_type = BME280_SENSOR_BMP280;
        ESP_LOGI(TAG, "BMP280 detected (ID=0x%02X), no humidity", chip_id);
    } else {
        ESP_LOGE(TAG, "Unknown chip ID: 0x%02X (expected 0x60 or 0x58)", chip_id);
        return ESP_ERR_NOT_FOUND;
    }

    ret = bme280_write_reg(handle, BME280_REG_RESET, BME280_SOFT_RESET_CMD);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    for (int i = 0; i < 10; i++) {
        uint8_t status;
        bme280_read_regs(handle, BME280_REG_STATUS, &status, 1);
        if ((status & 0x01) == 0) break;
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    ret = bme280_read_calib_data(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration data");
        return ret;
    }

    ret = bme280_configure(handle);
    if (ret != ESP_OK) return ret;

    handle->initialized = true;
    ESP_LOGI(TAG, "Sensor initialized successfully");
    return ESP_OK;
}

esp_err_t bme280_read_data(bme280_handle_t *handle, bme280_data_t *data)
{
    if (!handle || !handle->initialized || !data) return ESP_ERR_INVALID_ARG;
    esp_err_t ret;

    uint8_t ctrl_meas = (BME280_OS_1X << 5) | (BME280_OS_1X << 2) | BME280_MODE_FORCED;
    ret = bme280_write_reg(handle, BME280_REG_CTRL_MEAS, ctrl_meas);
    if (ret != ESP_OK) return ret;

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t raw[8];
    ret = bme280_read_regs(handle, BME280_REG_PRESS_MSB, raw, 8);
    if (ret != ESP_OK) return ret;

    int32_t adc_P = ((int32_t)raw[0] << 12) | ((int32_t)raw[1] << 4) | ((int32_t)raw[2] >> 4);
    int32_t adc_T = ((int32_t)raw[3] << 12) | ((int32_t)raw[4] << 4) | ((int32_t)raw[5] >> 4);
    int32_t adc_H = ((int32_t)raw[6] << 8)  | ((int32_t)raw[7]);

    int32_t temp_raw = bme280_compensate_temperature(handle, adc_T);
    data->temperature = temp_raw / 100.0f;

    uint32_t press_raw = bme280_compensate_pressure(handle, adc_P);
    data->pressure = press_raw / 25600.0f;

    if (handle->sensor_type == BME280_SENSOR_BME280) {
        uint32_t hum_raw = bme280_compensate_humidity(handle, adc_H);
        data->humidity = hum_raw / 1024.0f;
    } else {
        data->humidity = 0.0f;
    }

    data->sensor_type = handle->sensor_type;
    return ESP_OK;
}

esp_err_t bme280_soft_reset(bme280_handle_t *handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    return bme280_write_reg(handle, BME280_REG_RESET, BME280_SOFT_RESET_CMD);
}

bme280_sensor_type_t bme280_get_sensor_type(bme280_handle_t *handle)
{
    return handle ? handle->sensor_type : BME280_SENSOR_UNKNOWN;
}

bool bme280_has_humidity(bme280_handle_t *handle)
{
    return handle && (handle->sensor_type == BME280_SENSOR_BME280);
}
