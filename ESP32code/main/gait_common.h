/**
 * @file gait_common.h
 * @brief 步态分析固件共享数据结构、全局变量 extern、宏定义
 */

#ifndef GAIT_COMMON_H
#define GAIT_COMMON_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "host/ble_hs.h"
#include "mpu6050.h"
#include "fsr402.h"

/* EI 模型参数宏（纯 C，提供 EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE 等） */
#include "model-parameters/model_metadata.h"

/* 注意：edge-impulse-sdk 完整头文件是 C++ 库，仅在 gait_inference.cpp 中包含。
 * EI 类型（ei_impulse_result_t 等）和 run_classifier 仅在 .cpp 文件中使用。 */

/* ==================== 设备名 ==================== */
#define DEVICE_NAME "ESP32_Gait_Gatt"

/* ==================== 调试开关 ==================== */
#define FSR_TEST_MODE 0

/* ==================== 全局变量 extern ==================== */

/* BLE 状态 */
extern uint16_t g_conn_handle;
extern uint16_t g_tx_attr_handle;
extern bool g_tx_subscribed;
extern volatile bool g_task_running;
extern TaskHandle_t g_sim_task_handle;
extern TaskHandle_t g_inference_handle;

/* 传感器句柄 */
extern mpu6050_handle_t g_mpu6050_handle;

/* Edge Impulse 推理缓冲区 + 信号量 */
extern float ei_buf[];
extern SemaphoreHandle_t ei_sem;

/* 步频（由 gait_sensor.c 更新） */
extern volatile float cadence_avg_spm;

/* 发力模式（由 gait_sensor.c 更新） */
extern char gait_style[32];

#endif /* GAIT_COMMON_H */
