/**
 * @file gait_inference.cpp
 * @brief Edge Impulse 推理模块（C++）
 *
 * 因 run_classifier 是 C++ 函数，此文件必须为 .cpp。
 * FreeRTOS API 通过 extern "C" 调用。
 */

#include "gait_inference.h"
#include "ble_peripheral.h"

extern "C" {
#include <cstdio>
#include <cstring>
}

/* Edge Impulse SDK (C++) */
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

static const char *TAG = "EI";

/* ==================== EI 信号回调 ==================== */

static int ei_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    memcpy(out_ptr, ei_buf + offset, length * sizeof(float));
    return 0;
}

/* ==================== 标签中文映射 ==================== */

static const char *label_to_cn(const char *label)
{
    if (strcmp(label, "jump")  == 0) return "跳";
    if (strcmp(label, "ready") == 0) return "准备";
    if (strcmp(label, "run")   == 0) return "跑";
    if (strcmp(label, "still") == 0) return "静止";
    if (strcmp(label, "walk")  == 0) return "走";
    return label;
}

/* ==================== 推理任务 ==================== */

extern "C" void inference_task(void *arg)
{
    ei_impulse_result_t result;
    signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data = &ei_signal_get_data;

    ESP_LOGI(TAG, "Inference task started (window=%d samples)",
             EI_CLASSIFIER_RAW_SAMPLE_COUNT);

    while (g_task_running) {
        if (xSemaphoreTake(ei_sem, pdMS_TO_TICKS(500)) == pdTRUE) {
            EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
            if (err == EI_IMPULSE_OK) {
                const char *best_label = "?";
                float best_score = 0;
                for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
                    if (result.classification[i].value > best_score) {
                        best_score = result.classification[i].value;
                        best_label = result.classification[i].label;
                    }
                }

                float spm = cadence_avg_spm;
                if (strcmp(best_label, "still") == 0 || strcmp(best_label, "ready") == 0) {
                    spm = 0.0f;
                    cadence_avg_spm = 0.0f;
                }

                char result_buf[96];
                int len = snprintf(result_buf, sizeof(result_buf),
                                   "行为：%s(%d%%)，步频：%d，发力：%s\n",
                                   label_to_cn(best_label), (int)(best_score * 100),
                                   (int)spm, gait_style);
                if (len > 0 && len < (int)sizeof(result_buf)) {
                    ble_uart_send_line(result_buf, len);
                }

                ESP_LOGI(TAG, "Inference: %s (%.2f) [%lu ms DSP, %lu ms NN]",
                         best_label, best_score,
                         (unsigned long)result.timing.dsp,
                         (unsigned long)result.timing.classification);
            } else {
                ESP_LOGE(TAG, "run_classifier error: %d", (int)err);
            }
        }
    }

    ESP_LOGI(TAG, "Inference task stopping");
    g_inference_handle = NULL;
    vTaskDelete(NULL);
}
