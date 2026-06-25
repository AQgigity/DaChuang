/**
 * @file display_task.c
 * @brief 显示 FreeRTOS 任务（LVGL 定时器 + UI 刷新）
 */

#include "display_task.h"
#include "display.h"
#include "ui.h"
#include "screens/ui_Screen1.h"
#include <stdio.h>

#if ENABLE_EMG
#include "emg_sensor.h"
#endif

static const char *TAG = "DISPLAY";

void lvgl_task(void *arg)
{
    while (1) {
        if (g_lvgl_mutex && xSemaphoreTake(g_lvgl_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(g_lvgl_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ui_refresh_task(void *arg)
{
    char buf[32];
    while (1) {
        if (g_lvgl_mutex == NULL || xSemaphoreTake(g_lvgl_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        /* ---- 心率 ---- */
        float bpm = g_current_bpm;
        if (bpm > 0.0f) {
            snprintf(buf, sizeof(buf), "%.0f", bpm);
        } else {
            snprintf(buf, sizeof(buf), "---");
        }
        lv_label_set_text(ui_LabeHR, buf);

        /* ---- 气压 ---- */
        snprintf(buf, sizeof(buf), "%.1f", g_current_press);
        lv_label_set_text(ui_Labelbarometer, buf);

        /* ---- 温度 ---- */
        snprintf(buf, sizeof(buf), "%.1f", g_current_temp);
        lv_label_set_text(ui_LabelTemp, buf);

        /* ---- 脚踝数据 ---- */
        ankle_data_t ankle = g_latest_ankle;
        static int dbg_cnt = 0;
        if (++dbg_cnt >= 10) {
            dbg_cnt = 0;
            ESP_LOGI(TAG, "ankle.valid=%d action=%s connected=%d",
                     ankle.valid, ankle.action, g_ankle_connected);
        }
        if (ankle.valid) {
            lv_label_set_text(ui_Labelstate, ankle.action);
            snprintf(buf, sizeof(buf), "%d%%", ankle.confidence);
            lv_label_set_text(ui_Labelconfidence, buf);
            snprintf(buf, sizeof(buf), "%d", ankle.cadence_spm);
            lv_label_set_text(ui_Labelcadence, buf);
            lv_label_set_text(ui_Labelpower, ankle.gait_style);
        } else {
            if (g_ankle_connected) {
                lv_label_set_text(ui_Labelstate, "已连接");
            } else {
                lv_label_set_text(ui_Labelstate, "未连接");
            }
            lv_label_set_text(ui_Labelconfidence, "---");
            lv_label_set_text(ui_Labelcadence, "---");
            lv_label_set_text(ui_Labelpower, "---");
        }

        /* ---- 挥臂频率 ---- */
#if ENABLE_EMG
        {
            static int last_arm_freq = -1;
            static int arm_freq_stale_cnt = 0;
            int arm_freq = (g_emg_peak_count + 1) / 2;
            if (arm_freq == last_arm_freq) {
                arm_freq_stale_cnt++;
                if (arm_freq_stale_cnt >= 25) {
                    arm_freq = 0;
                    arm_freq_stale_cnt = 0;
                }
            } else {
                arm_freq_stale_cnt = 0;
            }
            last_arm_freq = arm_freq;
            snprintf(buf, sizeof(buf), "%d", arm_freq);
            lv_label_set_text(ui_Labelarmfrep, buf);
        }
#endif

        xSemaphoreGive(g_lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
