/**
  ******************************************************************************
  * @file    MPU6050.h
  * @brief   MPU6050陀螺仪加速度计传感器驱动库
  *          提供完整的传感器初始化、数据读取、校准和运动检测功能
  *          新增队列支持用于数据传递
  ******************************************************************************
  */

#ifndef __MPU6050_H
#define __MPU6050_H

#ifdef __cplusplus
extern "C" {
#endif

/* 包含头文件 ----------------------------------------------------------------*/
#include "main.h"
#include "i2c.h"
#include "usart.h"
#include <stdint.h>
#include <stdbool.h>

/* 宏定义 --------------------------------------------------------------------*/
// MPU6050 I2C地址（AD0引脚接地时为0x68，接VCC时为0x69）
#define MPU6050_ADDRESS          0x68 << 1  // STM32 HAL库需要左移1位

// MPU6050寄存器地址
#define MPU6050_WHO_AM_I         0x75       // 设备ID寄存器
#define MPU6050_PWR_MGMT_1       0x6B       // 电源管理寄存器1
#define MPU6050_GYRO_CONFIG      0x1B       // 陀螺仪配置寄存器
#define MPU6050_ACCEL_CONFIG     0x1C       // 加速度计配置寄存器
#define MPU6050_ACCEL_XOUT_H     0x3B       // 加速度计X轴高字节

// 队列相关配置
#define MPU6050_QUEUE_SIZE       50         // 队列最大容量
#define MPU6050_QUEUE_TIMEOUT    100        // 队列操作超时时间(ms)

/* 结构体定义 ----------------------------------------------------------------*/

/**
  * @brief  传感器数据结构体
  *         包含6轴原始数据和物理单位数据
  */
typedef struct {
    // 原始数据（16位有符号整数）
    int16_t accel_raw[3];    // 加速度计XYZ原始值
    int16_t gyro_raw[3];     // 陀螺仪XYZ原始值
    
    // 物理单位数据（浮点数）
    float accel_g[3];        // 加速度计XYZ（单位：g）
    float gyro_dps[3];       // 陀螺仪XYZ（单位：度/秒）
    
    // 处理后的数据
    float accel_magnitude;   // 加速度矢量幅度（减去重力）
    uint8_t is_moving;       // 运动检测标志：0=静止，1=运动
    
    // 时间戳（可选）
    uint32_t timestamp;      // 数据采集时间戳(ms)
} SensorData_t;

/**
  * @brief  校准数据结构体
  *         存储各轴的零偏偏移量
  */
typedef struct {
    int16_t accel_offset[3];  // 加速度计XYZ偏移量
    int16_t gyro_offset[3];   // 陀螺仪XYZ偏移量
    uint8_t calibrated;       // 校准状态：0=未校准，1=已校准
} CalibrationData_t;

/**
  * @brief  数据队列结构体
  *         环形缓冲区实现
  */
typedef struct {
    SensorData_t buffer[MPU6050_QUEUE_SIZE];  // 数据缓冲区
    uint16_t head;            // 队首索引
    uint16_t tail;            // 队尾索引
    uint16_t count;           // 当前元素数量
    uint16_t capacity;        // 队列容量
    uint32_t dropped;         // 丢弃的数据包计数
} SensorQueue_t;

/* 全局变量声明 --------------------------------------------------------------*/
// 校准数据
extern CalibrationData_t cal_data;

// 数据队列
extern SensorQueue_t sensor_queue;

// 初始化状态标志
extern uint8_t mpu6050_initialized;

/* 函数声明 ------------------------------------------------------------------*/

// 基础通信函数
uint8_t MPU6050_WriteReg(uint8_t RegAddress, uint8_t Data);
uint8_t MPU6050_ReadReg(uint8_t RegAddress);

// 基础功能函数
void MPU6050_Init(void);
uint8_t MPU6050_GetID(void);
void MPU6050_GetData(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                    int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ);
void MPU6050_Calibrate(UART_HandleTypeDef *huart);
void MPU6050_ApplyCalibration(int16_t *AccX, int16_t *AccY, int16_t *AccZ,
                             int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ);
void MPU6050_GetCalibratedData(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                              int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ);

// 队列操作函数
void MPU6050_Queue_Init(SensorQueue_t *queue);
uint8_t MPU6050_Queue_Push(SensorQueue_t *queue, SensorData_t *data);
uint8_t MPU6050_Queue_Pop(SensorQueue_t *queue, SensorData_t *data);
uint8_t MPU6050_Queue_IsEmpty(SensorQueue_t *queue);
uint8_t MPU6050_Queue_IsFull(SensorQueue_t *queue);
uint16_t MPU6050_Queue_Count(SensorQueue_t *queue);
void MPU6050_Queue_Clear(SensorQueue_t *queue);

// 高级功能函数（新增队列支持）
uint8_t MPU6050_AutoInit(UART_HandleTypeDef *huart);
void MPU6050_ReadProcessedData(SensorData_t *sensor_data);
uint8_t MPU6050_ReadAndEnqueue(void);
void MPU6050_PrintData(UART_HandleTypeDef *huart, SensorData_t *data);
char* MPU6050_GetDataString(SensorData_t *data);
uint8_t MPU6050_IsInitialized(void);

// 定时器回调函数（用于周期性数据采集）
void MPU6050_Timer_Callback(void);

#ifdef __cplusplus
}
#endif

#endif /* __MPU6050_H */