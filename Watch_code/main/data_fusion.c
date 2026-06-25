/**
 * @file data_fusion.c
 * @brief 数据融合 FreeRTOS 任务
 */

#include "data_fusion.h"

static const char *TAG = "FUSION";

void data_fusion_task(void *arg)
{
    ankle_data_t ankle;
    int wait_cnt = 0;

    while (1) {
        if (xQueueReceive(g_ankle_data_queue, &ankle, pdMS_TO_TICKS(1000)) == pdTRUE) {
            wait_cnt = 0;
            g_latest_ankle = ankle;  /* 更新共享变量供 MQTT 读取 */
            ESP_LOGI(TAG, "[%lu ms] 行为=%s 可信度=%d%% 发力=%s "
                     "步频=%dspm 心率=%.1fbpm 温度=%.1fC 气压=%.1fhPa",
                     (unsigned long)ankle.timestamp_ms,
                     ankle.action,
                     ankle.confidence,
                     ankle.gait_style,
                     ankle.cadence_spm,
                     g_current_bpm,
                     g_current_temp,
                     g_current_press);
        } else {
            if (++wait_cnt >= 10) {
                ESP_LOGW(TAG, "Waiting for ankle data... (connected=%d)",
                         g_ankle_connected);
                wait_cnt = 0;
            }
        }
    }
}
