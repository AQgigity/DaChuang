/**
 * @file data_fusion.h
 * @brief 数据融合 FreeRTOS 任务（合并脚踝 BLE 数据 + 本地传感器数据）
 */

#ifndef DATA_FUSION_H
#define DATA_FUSION_H

#include "watch_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 数据融合任务
 *
 * 从 g_ankle_data_queue 接收脚踝数据，更新 g_latest_ankle 供 MQTT 和 UI 读取。
 */
void data_fusion_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* DATA_FUSION_H */
