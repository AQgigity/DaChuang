/**
 * @file gait_sensor.c
 * @brief 传感器数据采集 + 步频检测 + 发力模式检测（50Hz）
 */

#include "gait_sensor.h"
#include <string.h>

static const char *TAG = "SENSOR";

/* ==================== 步频检测状态（模块私有） ==================== */

static bool       cadence_prev_pressed = false;
static TickType_t cadence_last_step_tick = 0;
static float      cadence_spm_history[5] = {0};
static int        cadence_spm_idx = 0;
static int        cadence_spm_count = 0;

/* ==================== 发力检测状态（模块私有） ==================== */

static bool       gait_toe_prev_pressed = false;
static TickType_t gait_t_heel_rise = 0;
static TickType_t gait_t_toe_rise  = 0;
static int        gait_step_who_first = 0;  /* 0=未定, 1=heel先, 2=toe先 */

/* ==================== 传感器数据任务 ==================== */

void sensor_data_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(20);  /* 50Hz */
    int sample_count = 0;

    ESP_LOGI(TAG, "Sensor data task started (50Hz)");

    /* 清零推理缓冲区 */
    memset(ei_buf, 0, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE * sizeof(float));

    while (g_task_running) {
        /* 读取两个 FSR 传感器 */
        bool heel_pressed = fsr402_is_pressed();
        bool toe_pressed  = fsr402_toe_is_pressed();

        /* ---- 上升沿检测：记录时间戳 + 步频 ---- */
        if (heel_pressed && !cadence_prev_pressed) {
            TickType_t now = xTaskGetTickCount();
            gait_t_heel_rise = now;

            /* 步频计算 */
            TickType_t elapsed = now - cadence_last_step_tick;
            if (elapsed >= pdMS_TO_TICKS(300) && cadence_last_step_tick != 0) {
                float interval_s = (float)elapsed * portTICK_PERIOD_MS / 1000.0f;
                float spm = 60.0f / interval_s;
                if (spm >= 40.0f && spm <= 220.0f) {
                    cadence_spm_history[cadence_spm_idx] = spm;
                    cadence_spm_idx = (cadence_spm_idx + 1) % 5;
                    if (cadence_spm_count < 5) cadence_spm_count++;
                    float sum = 0;
                    for (int i = 0; i < cadence_spm_count; i++) sum += cadence_spm_history[i];
                    cadence_avg_spm = sum / cadence_spm_count;
                }
            }
            cadence_last_step_tick = now;

            if (toe_pressed) {
                gait_step_who_first = 2;
            }
        }
        cadence_prev_pressed = heel_pressed;

        if (toe_pressed && !gait_toe_prev_pressed) {
            gait_t_toe_rise = xTaskGetTickCount();
            if (heel_pressed) {
                gait_step_who_first = 1;
            }
        }
        gait_toe_prev_pressed = toe_pressed;

        /* ---- 持续发力状态更新 ---- */
        if (!heel_pressed && !toe_pressed) {
            gait_step_who_first = 0;
            snprintf(gait_style, sizeof(gait_style), "未发力");
        } else if (heel_pressed && !toe_pressed) {
            gait_step_who_first = 1;
            snprintf(gait_style, sizeof(gait_style), "后脚跟发力");
        } else if (!heel_pressed && toe_pressed) {
            gait_step_who_first = 2;
            snprintf(gait_style, sizeof(gait_style), "前脚掌发力");
        } else {
            snprintf(gait_style, sizeof(gait_style), "全掌发力");
        }

        /* 读取 MPU6050 */
        float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;
        mpu6050_data_t mpu_data;
        if (mpu6050_read_data(&g_mpu6050_handle, &mpu_data) == ESP_OK) {
            ax = mpu_data.accel_g[0];
            ay = mpu_data.accel_g[1];
            az = mpu_data.accel_g[2];
            gx = mpu_data.gyro_dps[0];
            gy = mpu_data.gyro_dps[1];
            gz = mpu_data.gyro_dps[2];
        }

        /* 滑动窗口：左移 + 追加新样本 */
        memmove(ei_buf,
                ei_buf + EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME,
                (EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE - EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME) * sizeof(float));
        float *dst = ei_buf + EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE
                     - EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;
        dst[0] = ax; dst[1] = ay; dst[2] = az;
        dst[3] = gx; dst[4] = gy; dst[5] = gz;
        dst[6] = (float)heel_pressed;

        /* 每 5 组新数据唤醒推理任务 */
        sample_count++;
        if (sample_count >= 5) {
            sample_count = 0;
            xSemaphoreGive(ei_sem);
        }

        vTaskDelayUntil(&last_wake, period);
    }

    /* 重置步频状态 */
    cadence_prev_pressed = false;
    cadence_last_step_tick = 0;
    cadence_spm_count = 0;
    cadence_spm_idx = 0;
    cadence_avg_spm = 0.0f;
    memset(cadence_spm_history, 0, sizeof(cadence_spm_history));

    /* 重置发力检测状态 */
    gait_toe_prev_pressed = false;
    gait_t_heel_rise = 0;
    gait_t_toe_rise = 0;
    gait_step_who_first = 0;
    snprintf(gait_style, sizeof(gait_style), "未发力");

    ESP_LOGI(TAG, "Sensor data task stopping");
    g_sim_task_handle = NULL;
    vTaskDelete(NULL);
}
