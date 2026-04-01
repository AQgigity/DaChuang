/**
 * @file mpu6050.h
 * @brief MPU6050 陀螺仪加速度计传感器驱动
 *        基于ESP-IDF的I2C Master驱动
 */

#ifndef MPU6050_H
#define MPU6050_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/* 默认I2C配置 - 可在app_config.h中覆盖 */
#ifndef MPU6050_I2C_PORT
#define MPU6050_I2C_PORT       I2C_NUM_0
#endif

#ifndef MPU6050_I2C_SDA
#define MPU6050_I2C_SDA        GPIO_NUM_8
#endif

#ifndef MPU6050_I2C_SCL
#define MPU6050_I2C_SCL        GPIO_NUM_9
#endif

#ifndef MPU6050_I2C_FREQ_HZ
#define MPU6050_I2C_FREQ_HZ    400000          // 400kHz
#endif

#ifndef MPU6050_I2C_ADDR
#define MPU6050_I2C_ADDR       0x68            // 7位地址
#endif

/* 灵敏度常量 */
#define MPU6050_ACCEL_SENSITIVITY_2G   16384.0f
#define MPU6050_GYRO_SENSITIVITY_250DPS 131.0f

/* 运动检测阈值 */
#define MPU6050_MOVE_THRESHOLD   0.1f    // g

/* 日志标签 */
#define TAG_MPU6050  "MPU6050"

/**
 * @brief 传感器数据结构体
 */
typedef struct {
    int16_t accel_raw[3];      // 加速度计XYZ原始值
    int16_t gyro_raw[3];       // 陀螺仪XYZ原始值
    float accel_g[3];          // 加速度计XYZ (单位: g)
    float gyro_dps[3];         // 陀螺仪XYZ (单位: 度/秒)
    float temperature;         // 温度 (单位: 摄氏度)
    float accel_magnitude;     // 加速度矢量幅度(减去1g)
    bool is_moving;            // 运动检测标志
    uint32_t timestamp;        // 时间戳(ms)
} mpu6050_data_t;

/**
 * @brief 校准数据结构体
 */
typedef struct {
    int16_t accel_offset[3];   // 加速度计XYZ偏移量
    int16_t gyro_offset[3];    // 陀螺仪XYZ偏移量
    bool calibrated;           // 校准状态
} mpu6050_calibration_t;

/**
 * @brief MPU6050句柄结构体
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t i2c_dev;
    uint8_t device_addr;
    mpu6050_calibration_t calibration;
    bool initialized;
} mpu6050_handle_t;

/* 函数声明 */

/**
 * @brief 初始化MPU6050传感器
 * @param handle MPU6050句柄指针
 * @return ESP_OK成功, 其他错误码
 */
esp_err_t mpu6050_init(mpu6050_handle_t *handle);

/**
 * @brief 反初始化MPU6050传感器
 * @param handle MPU6050句柄指针
 * @return ESP_OK成功
 */
esp_err_t mpu6050_deinit(mpu6050_handle_t *handle);

/**
 * @brief 获取设备ID
 * @param handle MPU6050句柄指针
 * @return 设备ID (正常为0x68)
 */
uint8_t mpu6050_get_id(mpu6050_handle_t *handle);

/**
 * @brief 读取传感器数据
 * @param handle MPU6050句柄指针
 * @param data 数据结构体指针
 * @return ESP_OK成功, 其他错误码
 */
esp_err_t mpu6050_read_data(mpu6050_handle_t *handle, mpu6050_data_t *data);

/**
 * @brief 校准传感器 (需保持静止)
 * @param handle MPU6050句柄指针
 * @return ESP_OK成功, 其他错误码
 */
esp_err_t mpu6050_calibrate(mpu6050_handle_t *handle);

/**
 * @brief 打印传感器数据
 * @param data 数据结构体指针
 */
void mpu6050_print_data(mpu6050_data_t *data);

/**
 * @brief 检查是否已初始化
 * @param handle MPU6050句柄指针
 * @return true已初始化, false未初始化
 */
bool mpu6050_is_initialized(mpu6050_handle_t *handle);

/**
 * @brief 写寄存器
 * @param handle MPU6050句柄指针
 * @param reg 寄存器地址
 * @param data 写入的数据
 * @return ESP_OK成功, 其他错误码
 */
esp_err_t mpu6050_write_reg(mpu6050_handle_t *handle, uint8_t reg, uint8_t data);

/**
 * @brief 读寄存器
 * @param handle MPU6050句柄指针
 * @param reg 寄存器地址
 * @return 寄存器值
 */
uint8_t mpu6050_read_reg(mpu6050_handle_t *handle, uint8_t reg);

#ifdef __cplusplus
}
#endif

#endif // MPU6050_H
