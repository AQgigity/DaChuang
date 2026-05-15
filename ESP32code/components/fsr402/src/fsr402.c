/**
 * @file fsr402.c
 * @brief FSR402 压力传感器驱动实现 (开关量模式)
 */

#include "fsr402.h"
#include "esp_log.h"

static const char *TAG = TAG_FSR402;

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static fsr402_config_t fsr_config = {0};

esp_err_t fsr402_init(fsr402_config_t *config)
{
    esp_err_t ret;
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
    };
    ret = adc_oneshot_new_unit(&init_config, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = config->atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_oneshot_config_channel(adc1_handle, config->channel, &channel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    fsr_config = *config;
    fsr_config.adc_unit = adc1_handle;

    ESP_LOGI(TAG, "FSR402 initialized on ADC Channel %d, threshold=%d",
             config->channel, FSR402_PRESS_THRESHOLD);
    return ESP_OK;
}

esp_err_t fsr402_deinit(void)
{
    if (adc1_handle) {
        adc_oneshot_del_unit(adc1_handle);
        adc1_handle = NULL;
    }
    return ESP_OK;
}

bool fsr402_is_pressed(void)
{
    int raw_value = 0;
    if (!adc1_handle) {
        return false;
    }
    adc_oneshot_read(adc1_handle, fsr_config.channel, &raw_value);
    return (raw_value < FSR402_PRESS_THRESHOLD);
}

uint16_t fsr402_read_raw(void)
{
    int raw_value = 0;
    if (adc1_handle) {
        adc_oneshot_read(adc1_handle, fsr_config.channel, &raw_value);
    }
    return (uint16_t)raw_value;
}
