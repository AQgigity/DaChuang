#ifndef __MPU6050_H
#define __MPU6050_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>


// MPU6050寄存器地址定义
#define MPU6050_SMPLRT_DIV      0x19  // 采样率分频寄存器
#define MPU6050_CONFIG          0x1A  // 配置寄存器
#define MPU6050_GYRO_CONFIG     0x1B  // 陀螺仪配置寄存器
#define MPU6050_ACCEL_CONFIG    0x1C  // 加速度计配置寄存器
#define MPU6050_ACCEL_XOUT_H    0x3B  // 加速度计X轴数据高字节
#define MPU6050_ACCEL_XOUT_L    0x3C  // 加速度计X轴数据低字节
#define MPU6050_ACCEL_YOUT_H    0x3D  // 加速度计Y轴数据高字节
#define MPU6050_ACCEL_YOUT_L    0x3E  // 加速度计Y轴数据低字节
#define MPU6050_ACCEL_ZOUT_H    0x3F  // 加速度计Z轴数据高字节
#define MPU6050_ACCEL_ZOUT_L    0x40  // 加速度计Z轴数据低字节
#define MPU6050_TEMP_OUT_H      0x41  // 温度数据高字节
#define MPU6050_TEMP_OUT_L      0x42  // 温度数据低字节
#define MPU6050_GYRO_XOUT_H     0x43  // 陀螺仪X轴数据高字节
#define MPU6050_GYRO_XOUT_L     0x44  // 陀螺仪X轴数据低字节
#define MPU6050_GYRO_YOUT_H     0x45  // 陀螺仪Y轴数据高字节
#define MPU6050_GYRO_YOUT_L     0x46  // 陀螺仪Y轴数据低字节
#define MPU6050_GYRO_ZOUT_H     0x47  // 陀螺仪Z轴数据高字节
#define MPU6050_GYRO_ZOUT_L     0x48  // 陀螺仪Z轴数据低字节
#define MPU6050_PWR_MGMT_1      0x6B  // 电源管理寄存器1
#define MPU6050_PWR_MGMT_2      0x6C  // 电源管理寄存器2
#define MPU6050_WHO_AM_I        0x75  // 设备ID寄存器

// MPU6050设备地址 - I2C从机地址左移1位(0x68 << 1 = 0xD0)
#define MPU6050_ADDRESS         0xD0

// 校准数据结构
typedef struct {
    int16_t accel_offset[3];
    int16_t gyro_offset[3];
    uint8_t calibrated;
} CalibrationData_t;

// 传感器数据类型
typedef struct {
    float accel_g[3];      // 加速度 (g)
    float gyro_dps[3];     // 角速度 (度/秒)
    float accel_magnitude; // 加速度矢量幅度
    uint8_t is_moving;     // 运动检测标志
} SensorData_t;

// 基础函数声明
uint8_t MPU6050_WriteReg(uint8_t RegAddress, uint8_t Data);
uint8_t MPU6050_ReadReg(uint8_t RegAddress);
void MPU6050_Init(void);
uint8_t MPU6050_GetID(void);
void MPU6050_GetData(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                    int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ);
void MPU6050_Calibrate(UART_HandleTypeDef *huart);
void MPU6050_ApplyCalibration(int16_t *AccX, int16_t *AccY, int16_t *AccZ,
                             int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ);
void MPU6050_GetCalibratedData(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                              int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ);

// 高级功能函数声明
uint8_t MPU6050_AutoInit(UART_HandleTypeDef *huart);
void MPU6050_ReadProcessedData(SensorData_t *sensor_data);
void MPU6050_PrintData(UART_HandleTypeDef *huart, SensorData_t *data);
char* MPU6050_GetDataString(SensorData_t *data);
uint8_t MPU6050_IsInitialized(void);

// 外部变量声明
extern CalibrationData_t cal_data;
extern uint8_t mpu6050_initialized;

#ifdef __cplusplus
}
#endif

#endif /* __MPU6050_H */
