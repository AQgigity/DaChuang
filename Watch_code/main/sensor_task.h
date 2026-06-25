/**
 * @file sensor_task.h
 * @brief 传感器 FreeRTOS 任务（BME280 环境 + MAX30102 心率）
 */

#ifndef SENSOR_TASK_H
#define SENSOR_TASK_H

#include "watch_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BME280 环境传感器任务（100ms 周期，读取温度/气压）
 */
void environment_sensor_task(void *arg);

/**
 * @brief MAX30102 心率传感器任务（20ms 周期，读取 FIFO + 峰值检测）
 */
void heart_rate_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_TASK_H */
