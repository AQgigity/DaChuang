/**
 * @file fsr402.c
 * @brief FSR402 压力传感器驱动实现
 */

#include "fsr402.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <math.h>

static const char *TAG = TAG_FSR402;

/* 静态变量 */
static adc_oneshot_unit_handle_t adc1_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static fsr402_config_t fsr_config = {0};

/* ADC校准初始化 */
static esp_err_t adc_calibration_init(adc_unit_t unit, adc_atten_t atten,
                                       adc_cali_handle_t *out_handle)
{
    esp_err_t ret = ESP_FAIL;

#if CONFIG_IDF_TARGET_ESP32S3
    /* ESP32-S3 使用曲线拟合校准 */
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, out_handle);
#elif CONFIG_IDF_TARGET_ESP32
    /* ESP32 使用线性校准 */
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_config, out_handle);
#endif

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration initialized");
    } else {
        ESP_LOGW(TAG, "ADC calibration skipped (eFuse not present)");
    }
    return ret;
}

/* 初始化FSR402 */
esp_err_t fsr402_init(fsr402_config_t *config)
{
    esp_err_t ret;

    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    /* ADC单元初始化 */
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
    };
    ret = adc_oneshot_new_unit(&init_config, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 配置ADC通道 */
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = config->atten,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ret = adc_oneshot_config_channel(adc1_handle, config->channel, &channel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 初始化校准 */
    adc_calibration_init(ADC_UNIT_1, config->atten, &adc_cali_handle);

    /* 保存配置 */
    fsr_config = *config;
    fsr_config.adc_unit = adc1_handle;

    ESP_LOGI(TAG, "FSR402 initialized on ADC Channel %d (GPIO%d)",
             config->channel, 1 + config->channel);
    return ESP_OK;
}

/* 反初始化 */
esp_err_t fsr402_deinit(void)
{
    if (adc_cali_handle) {
#if CONFIG_IDF_TARGET_ESP32S3
        adc_cali_delete_scheme_curve_fitting(adc_cali_handle);
#elif CONFIG_IDF_TARGET_ESP32
        adc_cali_delete_scheme_line_fitting(adc_cali_handle);
#endif
        adc_cali_handle = NULL;
    }
    if (adc1_handle) {
        adc_oneshot_del_unit(adc1_handle);
        adc1_handle = NULL;
    }
    return ESP_OK;
}

/* 读取FSR402数据 */
esp_err_t fsr402_read(fsr402_data_t *data)
{
    int raw_value;
    int voltage_mv;

    if (!data || !adc1_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 读取原始值 */
    esp_err_t ret = adc_oneshot_read(adc1_handle, fsr_config.channel, &raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 转换为电压 */
    if (adc_cali_handle) {
        adc_cali_raw_to_voltage(adc_cali_handle, raw_value, &voltage_mv);
    } else {
        /* 无校准时使用近似计算: 3.3V / 4095 * raw_value */
        voltage_mv = (int)((float)raw_value * 3300.0f / 4095.0f);
    }

    /* 填充数据结构 */
    data->raw_value = (uint16_t)raw_value;
    data->voltage_mv = (float)voltage_mv;
    data->pressure_kg = fsr402_voltage_to_kg(data->voltage_mv);
    data->timestamp = esp_timer_get_time() / 1000;  /* 转换为毫秒 */

    return ESP_OK;
}

/* 读取原始ADC值 */
uint16_t fsr402_read_raw(void)
{
    int raw_value = 0;
    if (adc1_handle) {
        adc_oneshot_read(adc1_handle, fsr_config.channel, &raw_value);
    }
    return (uint16_t)raw_value;
}

/* 获取电压值 */
float fsr402_get_voltage(void)
{
    fsr402_data_t data;
    if (fsr402_read(&data) == ESP_OK) {
        return data.voltage_mv;
    }
    return 0.0f;
}

/* 电压转压力估算函数 (需根据实际传感器特性校准) */
float fsr402_voltage_to_kg(float voltage_mv)
{
    /*
     * FSR402是非线性传感器，需要根据实际测试建立映射
     *
     * 电路说明:
     * - 使用分压电路测量FSR402的电阻变化
     * - 假设固定电阻 R_fixed = 10kΩ
     * - V_out = 3.3V * R_fixed / (R_fixed + R_fsr)
     *
     * 估算公式示例（需根据实际硬件标定）:
     * - 导电性 Conductance = voltage_mv / (3300 - voltage_mv)
     * - 压力 Force (kg) ≈ Conductance * 校准系数
     */

    /* 无压力阈值 */
    if (voltage_mv < 100) {
        return 0.0f;
    }

    /* 防止溢出 */
    if (voltage_mv >= 3200) {
        return 100.0f;  /* 最大压力 */
    }

    /* 计算导电性 */
    float conductance = voltage_mv / (3300.0f - voltage_mv);

    /*
     * 压力估算系数 - 需要根据实际测试调整
     * FSR402的典型响应:
     * - 轻压力 (< 1kg): 导电性 0.01 ~ 0.1
     * - 中压力 (1-10kg): 导电性 0.1 ~ 1.0
     * - 重压力 (> 10kg): 导电性 > 1.0
     *
     * 这是一个粗略估算，建议使用以下方法校准:
     * 1. 测量已知重量下的电压值
     * 2. 建立电压-压力映射表
     * 3. 使用插值或拟合曲线计算压力
     */
    float force_kg = conductance * 2.0f;  /* 粗略估算系数 */

    /* 非线性补偿 (可选) */
    if (force_kg > 0 && force_kg < 1.0f) {
        force_kg *= 0.5f;  /* 轻压力区域补偿 */
    } else if (force_kg >= 1.0f && force_kg < 10.0f) {
        force_kg *= 1.0f;  /* 中压力区域 */
    } else {
        force_kg *= 1.2f;  /* 重压力区域补偿 */
    }

    return (force_kg > 0) ? force_kg : 0.0f;
}
