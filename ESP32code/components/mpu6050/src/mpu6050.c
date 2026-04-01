/**
 * @file mpu6050.c
 * @brief MPU6050 陀螺仪加速度计传感器驱动实现
 */
#include "freertos/FreeRTOS.h"
#include "mpu6050.h"
#include "mpu6050_reg.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = TAG_MPU6050;

/* I2C写寄存器 */
esp_err_t mpu6050_write_reg(mpu6050_handle_t *handle, uint8_t reg, uint8_t data)
{
    if (!handle || !handle->i2c_dev) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t write_buf[2] = {reg, data};
    return i2c_master_transmit(handle->i2c_dev, write_buf, 2, 100);
}

/* I2C读寄存器 */
uint8_t mpu6050_read_reg(mpu6050_handle_t *handle, uint8_t reg)
{
    uint8_t data = 0;
    if (handle && handle->i2c_dev) {
        i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, &data, 1, 100);
    }
    return data;
}

/* 初始化MPU6050 */
esp_err_t mpu6050_init(mpu6050_handle_t *handle)
{
    esp_err_t ret;

    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 初始化I2C主机总线 */
    i2c_master_bus_config_t i2c_bus_config = {
        .i2c_port = MPU6050_I2C_PORT,
        .sda_io_num = MPU6050_I2C_SDA,
        .scl_io_num = MPU6050_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ret = i2c_new_master_bus(&i2c_bus_config, &handle->i2c_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 配置I2C设备 */
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = MPU6050_I2C_ADDR,
        .scl_speed_hz = MPU6050_I2C_FREQ_HZ,
    };

    ret = i2c_master_bus_add_device(handle->i2c_bus, &dev_config, &handle->i2c_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(ret));
        i2c_del_master_bus(handle->i2c_bus);
        handle->i2c_bus = NULL;
        return ret;
    }

    /* 等待设备上电 */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 唤醒设备，使用内部8MHz时钟 */
    ret = mpu6050_write_reg(handle, MPU6050_PWR_MGMT_1, 0x01);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wake up MPU6050");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    /* 配置陀螺仪量程 ±250°/s */
    mpu6050_write_reg(handle, MPU6050_GYRO_CONFIG, MPU6050_GYRO_250DPS);

    /* 配置加速度计量程 ±2g */
    mpu6050_write_reg(handle, MPU6050_ACCEL_CONFIG, MPU6050_ACCEL_2G);

    /* 配置采样率分频器 (1kHz/8 = 125Hz) */
    mpu6050_write_reg(handle, MPU6050_SMPLRT_DIV, 7);

    /* 配置DLPF (5Hz, 33ms延迟) */
    mpu6050_write_reg(handle, MPU6050_CONFIG, MPU6050_DLPF_5HZ);

    vTaskDelay(pdMS_TO_TICKS(50));

    /* 验证设备ID */
    uint8_t id = mpu6050_get_id(handle);
    if (id == 0x68) {
        ESP_LOGI(TAG, "MPU6050 connected, ID: 0x%02X", id);
        handle->initialized = true;
        handle->device_addr = MPU6050_I2C_ADDR;
        memset(&handle->calibration, 0, sizeof(mpu6050_calibration_t));
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "MPU6050 not found, ID: 0x%02X", id);
        i2c_master_bus_rm_device(handle->i2c_dev);
        i2c_del_master_bus(handle->i2c_bus);
        handle->i2c_dev = NULL;
        handle->i2c_bus = NULL;
        return ESP_ERR_NOT_FOUND;
    }
}

/* 获取设备ID */
uint8_t mpu6050_get_id(mpu6050_handle_t *handle)
{
    return mpu6050_read_reg(handle, MPU6050_WHO_AM_I);
}

/* 读取传感器数据 */
esp_err_t mpu6050_read_data(mpu6050_handle_t *handle, mpu6050_data_t *data)
{
    uint8_t buffer[14];

    if (!handle || !handle->i2c_dev || !data) {
        return ESP_ERR_INVALID_ARG;
    }

    /* 从ACCEL_XOUT_H开始连续读取14字节 */
    uint8_t reg = MPU6050_ACCEL_XOUT_H;
    esp_err_t ret = i2c_master_transmit_receive(handle->i2c_dev, &reg, 1, buffer, 14, 100);
    if (ret != ESP_OK) {
        return ret;
    }

    /* 解析加速度计数据 */
    data->accel_raw[0] = (int16_t)((buffer[0] << 8) | buffer[1]);
    data->accel_raw[1] = (int16_t)((buffer[2] << 8) | buffer[3]);
    data->accel_raw[2] = (int16_t)((buffer[4] << 8) | buffer[5]);

    /* 解析温度数据 */
    int16_t temp_raw = (int16_t)((buffer[6] << 8) | buffer[7]);
    data->temperature = temp_raw / 340.0f + 36.53f;

    /* 解析陀螺仪数据 */
    data->gyro_raw[0] = (int16_t)((buffer[8] << 8) | buffer[9]);
    data->gyro_raw[1] = (int16_t)((buffer[10] << 8) | buffer[11]);
    data->gyro_raw[2] = (int16_t)((buffer[12] << 8) | buffer[13]);

    /* 应用校准偏移 */
    if (handle->calibration.calibrated) {
        for (int i = 0; i < 3; i++) {
            data->accel_raw[i] -= handle->calibration.accel_offset[i];
            data->gyro_raw[i] -= handle->calibration.gyro_offset[i];
        }
    }

    /* 转换为物理单位 */
    for (int i = 0; i < 3; i++) {
        data->accel_g[i] = data->accel_raw[i] / MPU6050_ACCEL_SENSITIVITY_2G;
        data->gyro_dps[i] = data->gyro_raw[i] / MPU6050_GYRO_SENSITIVITY_250DPS;
    }

    /* 计算加速度幅度 (减去1g重力) */
    data->accel_magnitude = sqrtf(data->accel_g[0] * data->accel_g[0] +
                                   data->accel_g[1] * data->accel_g[1] +
                                   data->accel_g[2] * data->accel_g[2]) - 1.0f;

    /* 运动检测 */
    data->is_moving = (fabsf(data->accel_magnitude) > MPU6050_MOVE_THRESHOLD);

    /* 时间戳 */
    data->timestamp = esp_timer_get_time() / 1000;

    return ESP_OK;
}

/* 校准函数 */
esp_err_t mpu6050_calibrate(mpu6050_handle_t *handle)
{
    if (!handle || !handle->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting calibration, please keep MPU6050 still and level...");

    int32_t accel_sum[3] = {0};
    int32_t gyro_sum[3] = {0};
    uint16_t samples = 500;

    for (uint16_t i = 0; i < samples; i++) {
        mpu6050_data_t data;
        if (mpu6050_read_data(handle, &data) == ESP_OK) {
            for (int j = 0; j < 3; j++) {
                accel_sum[j] += data.accel_raw[j];
                gyro_sum[j] += data.gyro_raw[j];
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    /* 计算平均值作为偏移量 */
    for (int j = 0; j < 3; j++) {
        handle->calibration.accel_offset[j] = (int16_t)(accel_sum[j] / samples);
        handle->calibration.gyro_offset[j] = (int16_t)(gyro_sum[j] / samples);
    }

    /* 调整Z轴加速度偏移（假设传感器水平放置） */
    int16_t z_avg = accel_sum[2] / samples;
    if (z_avg > 0) {
        handle->calibration.accel_offset[2] -= 16384;  // 减去1g
    } else {
        handle->calibration.accel_offset[2] += 16384;
    }

    handle->calibration.calibrated = true;

    ESP_LOGI(TAG, "Calibration complete!");
    ESP_LOGI(TAG, "Accel offsets: X=%d, Y=%d, Z=%d",
             handle->calibration.accel_offset[0],
             handle->calibration.accel_offset[1],
             handle->calibration.accel_offset[2]);
    ESP_LOGI(TAG, "Gyro offsets: X=%d, Y=%d, Z=%d",
             handle->calibration.gyro_offset[0],
             handle->calibration.gyro_offset[1],
             handle->calibration.gyro_offset[2]);

    return ESP_OK;
}

/* 打印数据 */
void mpu6050_print_data(mpu6050_data_t *data)
{
    if (!data) {
        return;
    }
    ESP_LOGI(TAG, "Acc: X:%7.3fg Y:%7.3fg Z:%7.3fg | "
                  "Gyro: X:%7.2f Y:%7.2f Z:%7.2f dps | "
                  "Temp: %.1fC | %s",
             data->accel_g[0], data->accel_g[1], data->accel_g[2],
             data->gyro_dps[0], data->gyro_dps[1], data->gyro_dps[2],
             data->temperature,
             data->is_moving ? "MOVING" : "STILL");
}

bool mpu6050_is_initialized(mpu6050_handle_t *handle)
{
    return (handle != NULL) && (handle->initialized);
}

/* 反初始化 */
esp_err_t mpu6050_deinit(mpu6050_handle_t *handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->i2c_dev) {
        i2c_master_bus_rm_device(handle->i2c_dev);
        handle->i2c_dev = NULL;
    }
    if (handle->i2c_bus) {
        i2c_del_master_bus(handle->i2c_bus);
        handle->i2c_bus = NULL;
    }
    handle->initialized = false;
    return ESP_OK;
}
