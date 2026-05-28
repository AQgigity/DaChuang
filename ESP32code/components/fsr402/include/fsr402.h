/**
 * @file fsr402.h
 * @brief FSR402 压力传感器驱动 (开关量模式)
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

#ifndef FSR402_ADC_UNIT
#define FSR402_ADC_UNIT        ADC_UNIT_1
#endif

#ifndef FSR402_ADC_CHANNEL
#define FSR402_ADC_CHANNEL     ADC_CHANNEL_4   // GPIO5
#endif

#ifndef FSR402_ADC_ATTEN
#define FSR402_ADC_ATTEN       ADC_ATTEN_DB_12
#endif

#ifndef FSR402_PRESS_THRESHOLD
#define FSR402_PRESS_THRESHOLD  800
#endif

#define TAG_FSR402   "FSR402"

typedef struct {
    adc_oneshot_unit_handle_t adc_unit;
    adc_channel_t channel;
    adc_atten_t atten;
} fsr402_config_t;

esp_err_t fsr402_init(fsr402_config_t *config);
esp_err_t fsr402_deinit(void);
bool fsr402_is_pressed(void);
uint16_t fsr402_read_raw(void);

/* Toe sensor (ADC_CHANNEL_0 / GPIO1) */
#ifndef FSR402_TOE_ADC_CHANNEL
#define FSR402_TOE_ADC_CHANNEL  ADC_CHANNEL_0   // GPIO1
#endif

esp_err_t fsr402_toe_init(adc_atten_t atten);
esp_err_t fsr402_toe_deinit(void);
bool      fsr402_toe_is_pressed(void);
uint16_t  fsr402_toe_read_raw(void);

#ifdef __cplusplus
}
#endif

#endif // FSR402_H
