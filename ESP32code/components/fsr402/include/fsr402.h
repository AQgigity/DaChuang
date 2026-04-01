/**
 * @file fsr402.h
 * @brief FSR402 压力传感器驱动
 *        基于ESP-IDF的ADC One-Shot驱动
 */

#ifndef FSR402_H
#define FSR402_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

/* 默认ADC配置 - 可在app_config.h中覆盖 */
#ifndef FSR402_ADC_UNIT
#define FSR402_ADC_UNIT        ADC_UNIT_1
#endif

#ifndef FSR402_ADC_CHANNEL
#define FSR402_ADC_CHANNEL     ADC_CHANNEL_0   // GPIO1
#endif

#ifndef FSR402_ADC_ATTEN
#define FSR402_ADC_ATTEN       ADC_ATTEN_DB_12 // 量程0-3.3V
#endif

/* 日志标签 */
#define TAG_FSR402   "FSR402"

/**
 * @brief FSR402配置结构体
 */
typedef struct {
    adc_oneshot_unit_handle_t adc_unit;
    adc_channel_t channel;
    adc_atten_t atten;
} fsr402_config_t;

/**
 * @brief FSR402数据结构体
 */
typedef struct {
    uint16_t raw_value;      // 原始ADC值 (0-4095)
    float voltage_mv;        // 电压值
    float pressure_kg;       // 估算压力 (需校准)
    uint32_t timestamp;      // 时间戳(ms)
} fsr402_data_t;

/* 函数声明 */

/**
 * @brief 初始化FSR402传感器
 * @param config 配置结构体指针
 * @return ESP_OK成功, 其他错误码
 */
esp_err_t fsr402_init(fsr402_config_t *config);

/**
 * @brief 反初始化FSR402传感器
 * @return ESP_OK成功
 */
esp_err_t fsr402_deinit(void);

/**
 * @brief 读取FSR402数据
 * @param data 数据结构体指针
 * @return ESP_OK成功, 其他错误码
 */
esp_err_t fsr402_read(fsr402_data_t *data);

/**
 * @brief 读取原始ADC值
 * @return 原始ADC值 (0-4095)
 */
uint16_t fsr402_read_raw(void);

/**
 * @brief 获取电压值
 * @return 电压值
 */
float fsr402_get_voltage(void);

/**
 * @brief 电压转压力估算
 * @param voltage_mv 电压值
 * @return 估算压力(kg)
 */
float fsr402_voltage_to_kg(float voltage_mv);

#ifdef __cplusplus
}
#endif

#endif // FSR402_H
