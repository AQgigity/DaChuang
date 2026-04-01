/**
  ******************************************************************************
  * @file    MPU6050.c
  * @brief   MPU6050陀螺仪加速度计传感器驱动库
  *          提供完整的传感器初始化、数据读取、校准和运动检测功能
  *          新增队列支持用于数据传递
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

// 数据队列
SensorQueue_t sensor_queue;

// MPU6050初始化状态标志：0=未初始化，1=已初始化
uint8_t mpu6050_initialized = 0;

// 串口输出缓冲区，用于格式化调试信息
static char uart_buf[150];

// 采样率控制
static uint32_t last_sample_time = 0;
static uint32_t sample_interval = 10;  // 默认10ms采样间隔（100Hz）

/* 私有函数声明 --------------------------------------------------------------*/

// 内部处理函数
static void MPU6050_ProcessData(SensorData_t *data);

/* 函数实现 ------------------------------------------------------------------*/

/**
  * @brief  向MPU6050指定寄存器写入数据
  * @param  RegAddress: 目标寄存器地址
  * @param  Data: 要写入的数据
  * @retval HAL状态: HAL_OK=成功, 其他=失败
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
  */
void MPU6050_Init(void)
{
    // 等待设备上电稳定
    HAL_Delay(100);
    
    // 配置电源管理寄存器：唤醒设备，选择内部8MHz时钟源
    MPU6050_WriteReg(MPU6050_PWR_MGMT_1, 0x01);
    HAL_Delay(50);
    
    // 配置陀螺仪量程为±250°/s
    MPU6050_WriteReg(MPU6050_GYRO_CONFIG, 0x00);
    
    // 配置加速度计量程为±2g
    MPU6050_WriteReg(MPU6050_ACCEL_CONFIG, 0x00);
    
    // 配置采样率分频器
    MPU6050_WriteReg(0x19, 7);  // 1kHz采样率，8分频 = 125Hz
    
    // 配置DLPF（数字低通滤波器）
    MPU6050_WriteReg(0x1A, 0x06);  // 加速度计5Hz，陀螺仪5Hz
    
    // 等待配置生效
    HAL_Delay(50);
    
    // 初始化数据队列
    MPU6050_Queue_Init(&sensor_queue);
}

/**
  * @brief  获取MPU6050设备ID
  * @param  无
  * @retval 设备ID值（正常应为0x68）
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
  */
void MPU6050_GetData(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                    int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ)
{
    uint8_t buffer[14];
    
    HAL_I2C_Mem_Read(&hi2c2, MPU6050_ADDRESS, MPU6050_ACCEL_XOUT_H, 
                    I2C_MEMADD_SIZE_8BIT, buffer, 14, 100);
    
    *AccX = (int16_t)((buffer[0] << 8) | buffer[1]);
    *AccY = (int16_t)((buffer[2] << 8) | buffer[3]);
    *AccZ = (int16_t)((buffer[4] << 8) | buffer[5]);
    
    *GyroX = (int16_t)((buffer[8] << 8) | buffer[9]);
    *GyroY = (int16_t)((buffer[10] << 8) | buffer[11]);
    *GyroZ = (int16_t)((buffer[12] << 8) | buffer[13]);
}

/**
  * @brief  传感器校准函数
  * @param  huart: 串口句柄指针，用于输出校准信息（可为NULL）
  * @retval 无
  */
void MPU6050_Calibrate(UART_HandleTypeDef *huart)
{
    int32_t accel_sum[3] = {0};
    int32_t gyro_sum[3] = {0};
    uint16_t samples = 500;
    
    if(huart != NULL) {
        HAL_UART_Transmit(huart, (uint8_t*)"Starting calibration...\r\n", 25, 100);
        HAL_UART_Transmit(huart, (uint8_t*)"Please keep MPU6050 still and level!\r\n", 38, 100);
    }
    
    for(uint16_t i = 0; i < samples; i++) {
        int16_t accel[3], gyro[3];
        
        MPU6050_GetData(&accel[0], &accel[1], &accel[2], 
                       &gyro[0], &gyro[1], &gyro[2]);
        
        for(int j = 0; j < 3; j++) {
            accel_sum[j] += accel[j];
            gyro_sum[j] += gyro[j];
        }
        
        if((huart != NULL) && ((i + 1) % 50 == 0)) {
            sprintf(uart_buf, "Calibrating... %d/%d\r\n", i + 1, samples);
            HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
        }
        
        HAL_Delay(10);
    }
    
    for(int j = 0; j < 3; j++) {
        cal_data.accel_offset[j] = accel_sum[j] / samples;
        cal_data.gyro_offset[j] = gyro_sum[j] / samples;
    }
    
    int16_t z_avg = accel_sum[2] / samples;
    
    if(z_avg > 0) {
        cal_data.accel_offset[2] -= 16384;
        if(huart != NULL) {
            HAL_UART_Transmit(huart, (uint8_t*)"Detected: Sensor upright (Z-axis positive)\r\n", 46, 100);
        }
    } else {
        cal_data.accel_offset[2] += 16384;
        if(huart != NULL) {
            HAL_UART_Transmit(huart, (uint8_t*)"Detected: Sensor inverted (Z-axis negative)\r\n", 47, 100);
        }
    }
    
    cal_data.calibrated = 1;
    
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
  * @param  AccX, AccY, AccZ: 加速度计数据指针
  * @param  GyroX, GyroY, GyroZ: 陀螺仪数据指针
  * @retval 无
  */
void MPU6050_ApplyCalibration(int16_t *AccX, int16_t *AccY, int16_t *AccZ,
                             int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ)
{
    if(cal_data.calibrated) {
        *AccX -= cal_data.accel_offset[0];
        *AccY -= cal_data.accel_offset[1];
        *AccZ -= cal_data.accel_offset[2];
        
        *GyroX -= cal_data.gyro_offset[0];
        *GyroY -= cal_data.gyro_offset[1];
        *GyroZ -= cal_data.gyro_offset[2];
    }
}

/**
  * @brief  获取校准后的传感器数据
  * @param  AccX, AccY, AccZ: 校准后的加速度计数据输出指针
  * @param  GyroX, GyroY, GyroZ: 校准后的陀螺仪数据输出指针
  * @retval 无
  */
void MPU6050_GetCalibratedData(int16_t *AccX, int16_t *AccY, int16_t *AccZ, 
                              int16_t *GyroX, int16_t *GyroY, int16_t *GyroZ)
{
    MPU6050_GetData(AccX, AccY, AccZ, GyroX, GyroY, GyroZ);
    MPU6050_ApplyCalibration(AccX, AccY, AccZ, GyroX, GyroY, GyroZ);
}

/* ==================== 队列操作函数实现 ==================== */

/**
  * @brief  初始化数据队列
  * @param  queue: 队列指针
  * @retval 无
  */
void MPU6050_Queue_Init(SensorQueue_t *queue)
{
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->capacity = MPU6050_QUEUE_SIZE;
    queue->dropped = 0;
    
    // 可选：清空缓冲区
    memset(queue->buffer, 0, sizeof(queue->buffer));
}

/**
  * @brief  向队列推送数据（入队）
  * @param  queue: 队列指针
  * @param  data: 要推送的数据指针
  * @retval 1=成功，0=队列已满
  */
uint8_t MPU6050_Queue_Push(SensorQueue_t *queue, SensorData_t *data)
{
    // 检查队列是否已满
    if (queue->count >= queue->capacity) {
        queue->dropped++;
        return 0;  // 队列已满，数据被丢弃
    }
    
    // 复制数据到队列尾部
    memcpy(&queue->buffer[queue->tail], data, sizeof(SensorData_t));
    
    // 更新队尾索引
    queue->tail = (queue->tail + 1) % queue->capacity;
    
    // 增加元素计数
    queue->count++;
    
    return 1;  // 入队成功
}

/**
  * @brief  从队列弹出数据（出队）
  * @param  queue: 队列指针
  * @param  data: 接收数据的缓冲区指针
  * @retval 1=成功，0=队列为空
  */
uint8_t MPU6050_Queue_Pop(SensorQueue_t *queue, SensorData_t *data)
{
    // 检查队列是否为空
    if (queue->count == 0) {
        return 0;  // 队列为空
    }
    
    // 从队首复制数据
    memcpy(data, &queue->buffer[queue->head], sizeof(SensorData_t));
    
    // 更新队首索引
    queue->head = (queue->head + 1) % queue->capacity;
    
    // 减少元素计数
    queue->count--;
    
    return 1;  // 出队成功
}

/**
  * @brief  检查队列是否为空
  * @param  queue: 队列指针
  * @retval 1=空，0=非空
  */
uint8_t MPU6050_Queue_IsEmpty(SensorQueue_t *queue)
{
    return (queue->count == 0);
}

/**
  * @brief  检查队列是否已满
  * @param  queue: 队列指针
  * @retval 1=满，0=未满
  */
uint8_t MPU6050_Queue_IsFull(SensorQueue_t *queue)
{
    return (queue->count >= queue->capacity);
}

/**
  * @brief  获取队列当前元素数量
  * @param  queue: 队列指针
  * @retval 队列中的元素数量
  */
uint16_t MPU6050_Queue_Count(SensorQueue_t *queue)
{
    return queue->count;
}

/**
  * @brief  清空队列
  * @param  queue: 队列指针
  * @retval 无
  */
void MPU6050_Queue_Clear(SensorQueue_t *queue)
{
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->dropped = 0;
}

/* ==================== 高级功能函数实现 ==================== */

/**
  * @brief  内部数据处理函数
  * @param  data: 传感器数据结构体指针
  * @retval 无
  */
static void MPU6050_ProcessData(SensorData_t *data)
{
    // 转换为物理单位 - 加速度计（±2g量程，灵敏度16384 LSB/g）
    data->accel_g[0] = data->accel_raw[0] / 16384.0f;
    data->accel_g[1] = data->accel_raw[1] / 16384.0f;
    data->accel_g[2] = data->accel_raw[2] / 16384.0f;
    
    // 转换为物理单位 - 陀螺仪（±250°/s量程，灵敏度131 LSB/°/s）
    data->gyro_dps[0] = data->gyro_raw[0] / 131.0f;
    data->gyro_dps[1] = data->gyro_raw[1] / 131.0f;
    data->gyro_dps[2] = data->gyro_raw[2] / 131.0f;
    
    // 计算加速度矢量幅度（用于运动检测）
    data->accel_magnitude = sqrt(data->accel_g[0]*data->accel_g[0] + 
                               data->accel_g[1]*data->accel_g[1] + 
                               data->accel_g[2]*data->accel_g[2]) - 1.0f;
    
    // 运动检测：当加速度幅度超过0.1g时认为在运动
    data->is_moving = (fabs(data->accel_magnitude) > 0.1f) ? 1 : 0;
}

/**
  * @brief  读取并处理传感器数据，转换为物理单位
  * @param  sensor_data: 传感器数据结构体指针，用于存储处理后的数据
  * @retval 无
  */
void MPU6050_ReadProcessedData(SensorData_t *sensor_data)
{
    int16_t accel_raw[3], gyro_raw[3];
    
    // 根据校准状态选择数据读取函数
    if(cal_data.calibrated) {
        MPU6050_GetCalibratedData(&accel_raw[0], &accel_raw[1], &accel_raw[2],
                                 &gyro_raw[0], &gyro_raw[1], &gyro_raw[2]);
    } else {
        MPU6050_GetData(&accel_raw[0], &accel_raw[1], &accel_raw[2],
                       &gyro_raw[0], &gyro_raw[1], &gyro_raw[2]);
    }
    
    // 保存原始数据
    sensor_data->accel_raw[0] = accel_raw[0];
    sensor_data->accel_raw[1] = accel_raw[1];
    sensor_data->accel_raw[2] = accel_raw[2];
    sensor_data->gyro_raw[0] = gyro_raw[0];
    sensor_data->gyro_raw[1] = gyro_raw[1];
    sensor_data->gyro_raw[2] = gyro_raw[2];
    
    // 设置时间戳
    sensor_data->timestamp = HAL_GetTick();
    
    // 处理数据
    MPU6050_ProcessData(sensor_data);
}

/**
  * @brief  读取传感器数据并压入队列（生产者函数）
  * @param  无
  * @retval 1=成功，0=失败或队列已满
  * @note   这是主要的传感器数据采集函数，应在主循环或定时器中断中调用
  */
uint8_t MPU6050_ReadAndEnqueue(void)
{
    // 检查传感器是否已初始化
    if (!mpu6050_initialized) {
        return 0;
    }
    
    // 控制采样率
    uint32_t current_time = HAL_GetTick();
    if (current_time - last_sample_time < sample_interval) {
        return 0;  // 未到采样时间
    }
    last_sample_time = current_time;
    
    // 创建临时数据存储
    SensorData_t sensor_data;
    
    // 读取并处理传感器数据
    MPU6050_ReadProcessedData(&sensor_data);
    
    // 尝试将数据压入队列
    if (MPU6050_Queue_Push(&sensor_queue, &sensor_data)) {
        return 1;  // 入队成功
    } else {
        return 0;  // 队列已满
    }
}

/**
  * @brief  自动初始化MPU6050并执行校准流程
  * @param  huart: 串口句柄指针，用于输出初始化信息（可为NULL）
  * @retval 初始化状态：1=成功，0=失败
  */
uint8_t MPU6050_AutoInit(UART_HandleTypeDef *huart)
{
    // 如果已经初始化，直接返回成功
    if(mpu6050_initialized) {
        return 1;
    }
    
    // 等待系统稳定
    HAL_Delay(100);
    
    // 初始化MPU6050传感器（会同时初始化队列）
    MPU6050_Init();
    HAL_Delay(100);
    
    // 读取设备ID验证通信
    uint8_t id = MPU6050_GetID();
    
    // 输出设备ID信息
    if(huart != NULL) {
        sprintf(uart_buf, "MPU6050 ID: 0x%02X\r\n", id);
        HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
    }
    
    // 检查设备ID是否正确
    if(id == 0x68) {
        if(huart != NULL) {
            HAL_UART_Transmit(huart, (uint8_t*)"MPU6050 Connected!\r\n", 20, 100);
            HAL_UART_Transmit(huart, (uint8_t*)"Auto-calibration starting in 3 seconds...\r\n", 45, 100);
            HAL_UART_Transmit(huart, (uint8_t*)"Please place MPU6050 level and still!\r\n\r\n", 41, 100);
            
            // 3秒倒计时
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
            HAL_UART_Transmit(huart, (uint8_t*)"MPU6050 Ready! Queue initialized.\r\n\r\n", 36, 100);
        }
        
        return 1;  // 初始化成功
    } else {
        if(huart != NULL) {
            HAL_UART_Transmit(huart, (uint8_t*)"MPU6050 Not Found!\r\n", 20, 100);
        }
        return 0;  // 初始化失败
    }
}

/**
  * @brief  定时器回调函数（用于周期性数据采集）
  * @param  无
  * @retval 无
  * @note   建议在定时器中断服务程序中调用此函数
  */
void MPU6050_Timer_Callback(void)
{
    // 读取传感器数据并压入队列
    MPU6050_ReadAndEnqueue();
}

/**
  * @brief  打印传感器数据到串口
  * @param  huart: 串口句柄指针
  * @param  data: 传感器数据结构体指针
  * @retval 无
  */
void MPU6050_PrintData(UART_HandleTypeDef *huart, SensorData_t *data)
{
    if(huart == NULL) return;
    
    sprintf(uart_buf, "Acc: X:%7.3fg Y:%7.3fg Z:%7.3fg | ", 
            data->accel_g[0], data->accel_g[1], data->accel_g[2]);
    HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
    
    sprintf(uart_buf, "Gyro: X:%7.2f Y:%7.2f Z:%7.2f dps | ", 
            data->gyro_dps[0], data->gyro_dps[1], data->gyro_dps[2]);
    HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
    
    sprintf(uart_buf, "Motion: %6.3fg %s\r\n", 
            data->accel_magnitude, data->is_moving ? "[MOVING]" : "[STILL]");
    HAL_UART_Transmit(huart, (uint8_t*)uart_buf, strlen(uart_buf), 100);
}

/**
  * @brief  获取传感器数据的格式化字符串
  * @param  data: 传感器数据结构体指针
  * @retval 格式化后的字符串指针（静态缓冲区）
  */
char* MPU6050_GetDataString(SensorData_t *data)
{
    static char data_buf[200];
    
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
  */
uint8_t MPU6050_IsInitialized(void)
{
    return mpu6050_initialized;
}

/**
  * @brief  设置采样间隔
  * @param  interval_ms: 采样间隔（毫秒）
  * @retval 无
  */
void MPU6050_SetSampleInterval(uint32_t interval_ms)
{
    sample_interval = interval_ms;
}

/**
  * @brief  获取队列丢弃数据包计数
  * @param  无
  * @retval 丢弃的数据包数量
  */
uint32_t MPU6050_GetDroppedPackets(void)
{
    return sensor_queue.dropped;
}

/**
  * @brief  获取队列状态信息
  * @param  capacity: 返回队列容量指针
  * @param  count: 返回当前元素数量指针
  * @param  dropped: 返回丢弃数据包计数指针
  * @retval 无
  */
void MPU6050_GetQueueStatus(uint16_t *capacity, uint16_t *count, uint32_t *dropped)
{
    if (capacity) *capacity = sensor_queue.capacity;
    if (count) *count = sensor_queue.count;
    if (dropped) *dropped = sensor_queue.dropped;
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
