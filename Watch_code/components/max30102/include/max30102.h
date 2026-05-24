#ifndef MAX30102_H
#define MAX30102_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "i2c_bus.h"

#ifndef MAX30102_I2C_ADDR
#define MAX30102_I2C_ADDR       0x57
#endif

#ifndef MAX30102_I2C_CLK_SPEED
#define MAX30102_I2C_CLK_SPEED  400000
#endif

typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    bool                    initialized;

    /* Peak detection state */
    int32_t  dc_offset;
    int32_t  prev_ac;
    bool     rising;
    int32_t  local_max;
    int32_t  local_min;
    uint32_t last_peak_tick;
    uint32_t refractory_end_tick;
    float    bpm_history[5];
    int      bpm_idx;
    int      bpm_count;
    float    current_bpm;

    uint8_t led_current_red;
    uint8_t led_current_ir;
} max30102_handle_t;

esp_err_t max30102_init(i2c_bus_handle_t bus, max30102_handle_t *handle);
esp_err_t max30102_read_fifo(max30102_handle_t *handle, int32_t *red_sample);
void      max30102_process_sample(max30102_handle_t *handle, int32_t sample);
float     max30102_get_bpm(max30102_handle_t *handle);
esp_err_t max30102_set_led_current(max30102_handle_t *handle, uint8_t red_ma, uint8_t ir_ma);
esp_err_t max30102_soft_reset(max30102_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* MAX30102_H */
