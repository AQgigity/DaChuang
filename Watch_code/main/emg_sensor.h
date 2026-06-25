/**
 * @file emg_sensor.h
 * @brief EMG 肌电传感器模块（ADC 采集 + 波峰检测 + 挥臂频率统计）
 */

#ifndef EMG_SENSOR_H
#define EMG_SENSOR_H

#include "watch_common.h"

#if ENABLE_EMG
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== EMG 配置宏 ==================== */

#define EMG_ADC_UNIT      ADC_UNIT_1
#define EMG_ADC_CHANNEL   ADC_CHANNEL_2   // GPIO3
#define EMG_ADC_ATTEN     ADC_ATTEN_DB_11 // 量程 ~0-3.1V
#define EMG_SAMPLE_MS     10              // 采样周期 10ms (100Hz)
#define EMG_VOFA_OUTPUT   0               // 1=VOFA+ 输出, 0=关闭
#define EMG_BASELINE      1750            // 基准值（静止时 ADC 均值）
#define EMG_PEAK_THRESH   500             // 波峰阈值（偏离基准 > 500 算发力）
#define EMG_DEADZONE_MS   200             // 死区时间 200ms（同一次发力只标记一个脉冲）
#define EMG_WINDOW_SEC    2               // 统计窗口 2 秒

/**
 * @brief 初始化 EMG ADC
 */
void emg_init(void);

/**
 * @brief EMG 采集 FreeRTOS 任务（100Hz 采样 + 波峰检测 + 2秒窗口统计）
 */
void emg_collect_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* ENABLE_EMG */
#endif /* EMG_SENSOR_H */
