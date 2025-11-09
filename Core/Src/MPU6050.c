/**
  ******************************************************************************
  * @file    MPU6050.c
  * @brief   MPU6050陀螺仪加速度计传感器驱动库
  *          提供完整的传感器初始化、数据读取、校准和运动检测功能
  ******************************************************************************
  */

#include "MPU6050.h"
#include "i2c.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* 外部变量声明 --------------------------------------------------------------*/

// 声明在main.c中定义的I2C2外设句柄，用于与MPU6050通信
extern I2C_HandleTypeDef hi2c2;

/* 全局变量定义 --------------------------------------------------------------*/

// 校准数据结构，存储各轴的零偏偏移量
CalibrationData_t cal_data = {0};

// MPU6050初始化状态标志：0=未初始化，1=已初始化
uint8_t mpu6050_initialized = 0;

// 串口输出缓冲区，用于格式化调试信息
static char uart_buf[150];

/* 私有函数声明 --------------------------------------------------------------*/

// 注：此处没有私有函数，所有函数都在头文件中声明为公开接口

/* 函数实现 ------------------------------------------------------------------*/

/**
  * @brief  向MPU6050指定寄存器写入数据
  * @param  RegAddress: 目标寄存器地址
  * @param  Data: 要写入的数据
  * @retval HAL状态: HAL_OK=成功, 其他=失败
  * @note   使用I2C存储器写操作，8位地址模式，100ms超时
  */
uint8_t MPU6050_WriteReg(uint8_t RegAddress, uint8_t Data)
{
    return HAL_I2C_Mem_Write(&hi2c2, MPU6050_ADDRESS, RegAddress, 
                            I2C_MEMADD_SIZE_8BIT, &Data, 1, 100);
}

/**
  * @brief  从MPU6050指定寄存器读取数据
  * @param  RegAddress: 源寄存器地址
  * @retval 读取到的寄存器数据
  * @note   使用I2C存储器读操作，8位地址模式，100ms超时
  */
uint8_t MPU6050_ReadReg(uint8_t RegAddress)
{
    uint8_t data = 0;
    HAL_I2C_Mem_Read(&hi2c2, MPU6050_ADDRESS, RegAddress, 
                    I2C_MEMADD_SIZE_8BIT, &data, 1, 100);
    return data;
}

/**
  * @brief  MPU6050传感器初始化
  * @param  无
  * @retval 无
  * @note   配置设备唤醒、时钟源、陀螺仪和加速度计量程
  *         初始化后设备进入正常工作模式
  */
void MPU6050_Init(void)
{
    // 等待设备上电稳定
    HAL_Delay(100);
    
    // 配置电源管理寄存器：唤醒设备，选择内部8MHz时钟源
    MPU6050_WriteReg(MPU6050_PWR_MGMT_1, 0x01);
    HAL_Delay(50);
    
    // 配置陀螺仪量程为±250°/s (0x00 = ±250dps)
    MPU6050_WriteReg(MPU6050_GYRO_CONFIG, 0x00);
    
    // 配置加速度计量程为±2g (0x00 = ±2g)
    MPU6050_WriteReg(MPU6050_ACCEL_CONFIG, 0x00);
    
    // 等待配置生效
    HAL_Delay(50);
}

/**
  * @brief  获取MPU6050设备ID
  * @param  无
  * @retval 设备ID值（正常应为0x68）
  * @note   读取WHO_AM_I寄存器(0x75)验证设备通信是否正常
  */
uint8_t MPU6050_GetID(void)
{
    return MPU6050_ReadReg(MPU6050_WHO_AM_I);
}

/**
  * @brief  读取原始传感器数据（6轴）
  * @param  AccX, AccY, AccZ: 加速度计三轴原始数据输出指针
  * @param  GyroX, GyroY, GyroZ: 陀螺仪三轴原始数据输出指针
  * @retval 无
  * @note   一次性读取14个连续寄存器，包含：
  *         - 加速度计XYZ（6字节）
  *         - 温度数据（2字节，被跳过）
  *         - 陀螺仪XYZ（6字节）
  *         数据范围为-32768到+32767
  */
void MPU6050_GetData(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                    int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ)
{
    uint8_t buffer[14];  // 数据缓冲区，对应14个数据寄存器
    
    // 从加速度计X轴高字节寄存器开始，连续读取14个字节
    HAL_I2C_Mem_Read(&hi2c2, MPU6050_ADDRESS, MPU6050_ACCEL_XOUT_H, 
                    I2C_MEMADD_SIZE_8BIT, buffer, 14, 100);
    
    // 将高低字节合并为16位有符号整数（加速度计数据）
    *AccX = (int16_t)((buffer[0] << 8) | buffer[1]);   // 加速度X轴
    *AccY = (int16_t)((buffer[2] << 8) | buffer[3]);   // 加速度Y轴
    *AccZ = (int16_t)((buffer[4] << 8) | buffer[5]);   // 加速度Z轴
    
    // 跳过温度数据(buffer[6], buffer[7])，读取陀螺仪数据
    *GyroX = (int16_t)((buffer[8] << 8) | buffer[9]);   // 陀螺仪X轴
    *GyroY = (int16_t)((buffer[10] << 8) | buffer[11]); // 陀螺仪Y轴
    *GyroZ = (int16_t)((buffer[12] << 8) | buffer[13]); // 陀螺仪Z轴
}

/**
  * @brief  传感器校准函数
  * @param  huart: 串口句柄指针，用于输出校准信息（可为NULL）
  * @retval 无
  * @note   采集500个样本计算各轴零偏平均值
  *         自动检测传感器方向并补偿重力影响
  *         校准数据存储在cal_data全局变量中
  */
void MPU6050_Calibrate(UART_HandleTypeDef *huart)
{
    int32_t accel_sum[3] = {0};  // 加速度计三轴累加和
    int32_t gyro_sum[3] = {0};   // 陀螺仪三轴累加和
    uint16_t samples = 500;      // 采样点数（越多越精确）
    
    // 输出校准开始信息（如果串口可用）
    if(huart != NULL) {
        HAL_UART_Transmit(huart, (uint8_t*)"Starting calibration...\r\n", 25, 100);
        HAL_UART_Transmit(huart, (uint8_t*)"Please keep MPU6050 still and level!\r\n", 38, 100);
    }
    
    // 采集样本数据循环
    for(uint16_t i = 0; i < samples; i++) {
        int16_t accel[3], gyro[3];  // 临时存储单次采样数据
        
        // 读取当前传感器数据
        MPU6050_GetData(&accel[0], &accel[1], &accel[2], 
                       &gyro[0], &gyro[1], &gyro[2]);
        
        // 累加各轴数据用于后续求平均值
        for(int j = 0; j < 3; j++) {
            accel_sum[j] += accel[j];
            gyro_sum[j] += gyro[j];
        }
        
        // 每50次采样输出一次进度信息
        if((huart != NULL) && ((i + 1) % 50 == 0)) {
            sprintf(uart_buf, "Calibrating... %d/%d\r\n", i + 1, samples);
            HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
        }
        
        // 采样间隔，避免数据相关性
        HAL_Delay(10);
    }
    
    // 计算各轴平均值作为零偏偏移量
    for(int j = 0; j < 3; j++) {
        cal_data.accel_offset[j] = accel_sum[j] / samples;
        cal_data.gyro_offset[j] = gyro_sum[j] / samples;
    }
    
    // 自动检测传感器方向（通过Z轴平均值判断）
    int16_t z_avg = accel_sum[2] / samples;
    
    // 输出Z轴平均值信息
    if(huart != NULL) {
        sprintf(uart_buf, "Z-axis average: %d (should be ~+16384 for upright, ~-16384 for inverted)\r\n", z_avg);
        HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
    }
    
    // 根据Z轴方向调整重力补偿
    if(z_avg > 0) {
        // 传感器正面朝上：期望Z轴为+16384（1g重力）
        cal_data.accel_offset[2] -= 16384;
        if(huart != NULL) {
            HAL_UART_Transmit(huart, (uint8_t*)"Detected: Sensor upright (Z-axis positive)\r\n", 46, 100);
        }
    } else {
        // 传感器正面朝下：期望Z轴为-16384（-1g重力）
        cal_data.accel_offset[2] += 16384;
        if(huart != NULL) {
            HAL_UART_Transmit(huart, (uint8_t*)"Detected: Sensor inverted (Z-axis negative)\r\n", 47, 100);
        }
    }
    
    // 标记校准完成
    cal_data.calibrated = 1;
    
    // 输出校准结果
    if(huart != NULL) {
        HAL_UART_Transmit(huart, (uint8_t*)"Calibration complete!\r\n", 23, 100);
        sprintf(uart_buf, "Accel Offsets: X:%d, Y:%d, Z:%d\r\n", 
                cal_data.accel_offset[0], cal_data.accel_offset[1], cal_data.accel_offset[2]);
        HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
        sprintf(uart_buf, "Gyro Offsets: X:%d, Y:%d, Z:%d\r\n", 
                cal_data.gyro_offset[0], cal_data.gyro_offset[1], cal_data.gyro_offset[2]);
        HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
    }
}

/**
  * @brief  应用校准偏移量到原始数据
  * @param  AccX, AccY, AccZ: 加速度计数据指针（输入输出参数）
  * @param  GyroX, GyroY, GyroZ: 陀螺仪数据指针（输入输出参数）
  * @retval 无
  * @note   只有在校准完成后才会应用偏移量
  *         直接修改传入的指针值，实现原地校准
  */
void MPU6050_ApplyCalibration(int16_t *AccX, int16_t *AccY, int16_t *AccZ,
                             int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ)
{
    // 检查校准状态，只有完成校准才应用偏移
    if(cal_data.calibrated) {
        *AccX -= cal_data.accel_offset[0];  // 加速度X轴校准
        *AccY -= cal_data.accel_offset[1];  // 加速度Y轴校准
        *AccZ -= cal_data.accel_offset[2];  // 加速度Z轴校准
        
        *GyroX -= cal_data.gyro_offset[0];  // 陀螺仪X轴校准
        *GyroY -= cal_data.gyro_offset[1];  // 陀螺仪Y轴校准
        *GyroZ -= cal_data.gyro_offset[2];  // 陀螺仪Z轴校准
    }
}

/**
  * @brief  获取校准后的传感器数据
  * @param  AccX, AccY, AccZ: 校准后的加速度计数据输出指针
  * @param  GyroX, GyroY, GyroZ: 校准后的陀螺仪数据输出指针
  * @retval 无
  * @note   先读取原始数据，然后应用校准偏移量
  *         提供校准数据的便捷访问接口
  */
void MPU6050_GetCalibratedData(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                              int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ)
{
    // 读取原始传感器数据
    MPU6050_GetData(AccX, AccY, AccZ, GyroX, GyroY, GyroZ);
    // 应用校准偏移量
    MPU6050_ApplyCalibration(AccX, AccY, AccZ, GyroX, GyroY, GyroZ);
}

/* ==================== 高级功能函数实现 ==================== */

/**
  * @brief  自动初始化MPU6050并执行校准流程
  * @param  huart: 串口句柄指针，用于输出初始化信息（可为NULL）
  * @retval 初始化状态：1=成功，0=失败
  * @note   完整的初始化流程包括：
  *         1. 设备初始化
  *         2. ID验证
  *         3. 自动校准
  *         4. 状态标记
  */
uint8_t MPU6050_AutoInit(UART_HandleTypeDef *huart)
{
    // 如果已经初始化，直接返回成功
    if(mpu6050_initialized) {
        return 1;
    }
    
    // 等待系统稳定
    HAL_Delay(100);
    
    // 初始化MPU6050传感器
    MPU6050_Init();
    HAL_Delay(100);
    
    // 读取设备ID验证通信
    uint8_t id = MPU6050_GetID();
    
    // 输出设备ID信息
    if(huart != NULL) {
        sprintf(uart_buf, "MPU6050 ID: 0x%02X\r\n", id);
        HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
    }
    
    // 检查设备ID是否正确（MPU6050正常ID为0x68）
    if(id == 0x68) {
        // 输出连接成功信息
        if(huart != NULL) {
            HAL_UART_Transmit(huart, (uint8_t*)"MPU6050 Connected!\r\n", 20, 100);
            HAL_UART_Transmit(huart, (uint8_t*)"Auto-calibration starting in 3 seconds...\r\n", 45, 100);
            HAL_UART_Transmit(huart, (uint8_t*)"Please place MPU6050 level and still!\r\n\r\n", 41, 100);
            
            // 3秒倒计时，让用户有时间放置好传感器
            for(int i = 3; i > 0; i--) {
                sprintf(uart_buf, "%d...\r\n", i);
                HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
                HAL_Delay(1000);
            }
        }
        
        // 执行传感器校准
        MPU6050_Calibrate(huart);
        
        // 标记初始化完成
        mpu6050_initialized = 1;
        
        // 输出准备就绪信息
        if(huart != NULL) {
            HAL_UART_Transmit(huart, (uint8_t*)"MPU6050 Ready!\r\n\r\n", 18, 100);
        }
        
        return 1;  // 初始化成功
    } else {
        // 设备ID不正确，初始化失败
        if(huart != NULL) {
            HAL_UART_Transmit(huart, (uint8_t*)"MPU6050 Not Found!\r\n", 20, 100);
        }
        return 0;  // 初始化失败
    }
}

/**
  * @brief  读取并处理传感器数据，转换为物理单位
  * @param  sensor_data: 传感器数据结构体指针，用于存储处理后的数据
  * @retval 无
  * @note   完成以下处理：
  *         1. 读取原始数据（根据校准状态选择）
  *         2. 转换为物理单位（g和°/s）
  *         3. 计算加速度矢量幅度
  *         4. 运动状态检测
  */
void MPU6050_ReadProcessedData(SensorData_t *sensor_data)
{
    int16_t accel_raw[3], gyro_raw[3];  // 原始数据缓冲区
    
    // 根据校准状态选择数据读取函数
    if(cal_data.calibrated) {
        // 使用校准后的数据
        MPU6050_GetCalibratedData(&accel_raw[0], &accel_raw[1], &accel_raw[2],
                                 &gyro_raw[0], &gyro_raw[1], &gyro_raw[2]);
    } else {
        // 使用原始数据（未校准）
        MPU6050_GetData(&accel_raw[0], &accel_raw[1], &accel_raw[2],
                       &gyro_raw[0], &gyro_raw[1], &gyro_raw[2]);
    }
    
    // 转换为物理单位 - 加速度计（±2g量程，灵敏度16384 LSB/g）
    sensor_data->accel_g[0] = accel_raw[0] / 16384.0f;  // X轴加速度(g)
    sensor_data->accel_g[1] = accel_raw[1] / 16384.0f;  // Y轴加速度(g)
    sensor_data->accel_g[2] = accel_raw[2] / 16384.0f;  // Z轴加速度(g)
    
    // 转换为物理单位 - 陀螺仪（±250°/s量程，灵敏度131 LSB/°/s）
    sensor_data->gyro_dps[0] = gyro_raw[0] / 131.0f;    // X轴角速度(°/s)
    sensor_data->gyro_dps[1] = gyro_raw[1] / 131.0f;    // Y轴角速度(°/s)
    sensor_data->gyro_dps[2] = gyro_raw[2] / 131.0f;    // Z轴角速度(°/s)
    
    // 计算加速度矢量幅度（用于运动检测）
    // 公式：magnitude = sqrt(x² + y² + z²) - 1.0（减去重力影响）
    sensor_data->accel_magnitude = sqrt(sensor_data->accel_g[0]*sensor_data->accel_g[0] + 
                                       sensor_data->accel_g[1]*sensor_data->accel_g[1] + 
                                       sensor_data->accel_g[2]*sensor_data->accel_g[2]) - 1.0f;
    
    // 简单的运动检测：当加速度幅度超过0.1g时认为在运动
    // 阈值可根据应用需求调整（0.05g-0.2g）
    sensor_data->is_moving = (fabs(sensor_data->accel_magnitude) > 0.1f) ? 1 : 0;
}

/**
  * @brief  打印传感器数据到串口
  * @param  huart: 串口句柄指针
  * @param  data: 传感器数据结构体指针
  * @retval 无
  * @note   格式化输出所有传感器数据，便于调试和监控
  *         如果huart为NULL，则不执行任何操作
  */
void MPU6050_PrintData(UART_HandleTypeDef *huart, SensorData_t *data)
{
    // 检查串口句柄有效性
    if(huart == NULL) return;
    
    // 格式化并发送加速度数据
    sprintf(uart_buf, "Acc: X:%7.3fg Y:%7.3fg Z:%7.3fg | ", 
            data->accel_g[0], data->accel_g[1], data->accel_g[2]);
    HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
    
    // 格式化并发送陀螺仪数据
    sprintf(uart_buf, "Gyro: X:%7.2f Y:%7.2f Z:%7.2f dps | ", 
            data->gyro_dps[0], data->gyro_dps[1], data->gyro_dps[2]);
    HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
    
    // 格式化并发送运动检测信息
    sprintf(uart_buf, "Motion: %6.3fg %s\r\n", 
            data->accel_magnitude, data->is_moving ? "[MOVING]" : "[STILL]");
    HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
}

/**
  * @brief  获取传感器数据的格式化字符串
  * @param  data: 传感器数据结构体指针
  * @retval 格式化后的字符串指针（静态缓冲区）
  * @note   返回静态缓冲区指针，内容在下次调用时会被覆盖
  *         适用于需要自定义显示格式的应用
  */
char* MPU6050_GetDataString(SensorData_t *data)
{
    static char data_buf[200];  // 静态缓冲区，避免内存分配
    
    // 格式化所有传感器数据到字符串
    snprintf(data_buf, sizeof(data_buf), 
             "Acc: X:%7.3fg Y:%7.3fg Z:%7.3fg | "
             "Gyro: X:%7.2f Y:%7.2f Z:%7.2f dps | "
             "Motion: %6.3fg %s\r\n",
             data->accel_g[0], data->accel_g[1], data->accel_g[2],
             data->gyro_dps[0], data->gyro_dps[1], data->gyro_dps[2],
             data->accel_magnitude, data->is_moving ? "MOVING" : "STILL");
    
    return data_buf;
}

/**
  * @brief  检查MPU6050是否已初始化
  * @param  无
  * @retval 初始化状态：1=已初始化，0=未初始化
  * @note   用于在读取数据前检查传感器状态
  */
uint8_t MPU6050_IsInitialized(void)
{
    return mpu6050_initialized;
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/