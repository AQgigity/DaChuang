/**
 * @file gait_inference.h
 * @brief Edge Impulse 推理模块（C++，推理任务 + 信号回调 + 标签映射）
 */

#ifndef GAIT_INFERENCE_H
#define GAIT_INFERENCE_H

#include "gait_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Edge Impulse 推理 FreeRTOS 任务
 *
 * 等待 ei_sem 信号量 → run_classifier → 组装中文结果 → BLE 发送。
 * 需要在 .cpp 文件中实现（run_classifier 是 C++ 函数）。
 */
void inference_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* GAIT_INFERENCE_H */
