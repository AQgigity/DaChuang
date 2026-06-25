/**
 * @file gait_sensor.h
 * @brief 传感器数据采集 + 步频检测 + 发力模式检测
 */

#ifndef GAIT_SENSOR_H
#define GAIT_SENSOR_H

#include "gait_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 传感器数据采集 FreeRTOS 任务（50Hz）
 *
 * 读取 FSR402 双传感器 + MPU6050，执行步频检测、发力模式判定、
 * 滑动窗口填充，每 5 组新数据触发 ei_sem 信号量。
 */
void sensor_data_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* GAIT_SENSOR_H */
