/**
 * @file emg_sensor.c
 * @brief EMG 肌电传感器模块（ADC 采集 + 波峰检测 + 挥臂频率统计）
 */

#include "emg_sensor.h"

#if ENABLE_EMG

static const char *TAG = "EMG";

/* 模块私有状态 */
static adc_oneshot_unit_handle_t s_emg_adc_handle = NULL;

/* 全局变量定义（extern 在 watch_common.h） */
volatile int g_current_emg_raw = 0;
volatile int g_emg_peak_count = 0;

void emg_init(void)
{
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = EMG_ADC_UNIT,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &s_emg_adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten   = EMG_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_emg_adc_handle, EMG_ADC_CHANNEL, &chan_cfg));

    ESP_LOGI(TAG, "EMG initialized: ADC1_CH%d (GPIO3), 12bit, 11dB", EMG_ADC_CHANNEL);
}

void emg_collect_task(void *arg)
{
    bool was_above = false;
    int peak_count = 0;
    int samples_per_window = EMG_WINDOW_SEC * 1000 / EMG_SAMPLE_MS;
    int deadzone_cnt = 0;
    int deadzone_samples = EMG_DEADZONE_MS / EMG_SAMPLE_MS;

    int tick = 0;
    while (1) {
        int raw = 0;
        adc_oneshot_read(s_emg_adc_handle, EMG_ADC_CHANNEL, &raw);
        g_current_emg_raw = raw;

        int offset = raw - EMG_BASELINE;
        bool is_above = (offset > EMG_PEAK_THRESH);

        if (is_above && !was_above && deadzone_cnt <= 0) {
            peak_count++;
            deadzone_cnt = deadzone_samples;
        }
        was_above = is_above;
        if (deadzone_cnt > 0) deadzone_cnt--;

        int peak_mark = (deadzone_cnt > 0) ? 1000 : 0;
#if EMG_VOFA_OUTPUT
        printf("%d,%d\n", offset, peak_mark);
#endif

        tick++;
        if (tick >= samples_per_window) {
            tick = 0;
            g_emg_peak_count = peak_count;
            ESP_LOGI(TAG, "挥臂频率: %d peaks/s", peak_count);
            peak_count = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(EMG_SAMPLE_MS));
    }
}

#endif /* ENABLE_EMG */
