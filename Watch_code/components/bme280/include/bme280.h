#ifndef BME280_H
#define BME280_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "i2c_bus.h"

#ifndef BME280_I2C_ADDR
#define BME280_I2C_ADDR         0x76
#endif

#ifndef BME280_I2C_CLK_SPEED
#define BME280_I2C_CLK_SPEED    400000
#endif

#define BME280_CHIP_ID_BME280   0x60
#define BME280_CHIP_ID_BMP280   0x58

typedef enum {
    BME280_SENSOR_UNKNOWN = 0,
    BME280_SENSOR_BME280,
    BME280_SENSOR_BMP280,
} bme280_sensor_type_t;

typedef struct {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;

    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;

    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;
    int16_t  dig_H5;
    int8_t   dig_H6;
} bme280_calib_data_t;

typedef struct {
    float temperature;
    float pressure;
    float humidity;
    bme280_sensor_type_t sensor_type;
} bme280_data_t;

typedef struct {
    i2c_master_dev_handle_t i2c_dev;
    bme280_calib_data_t     calib;
    int32_t                 t_fine;
    bme280_sensor_type_t    sensor_type;
    bool                    initialized;
} bme280_handle_t;

esp_err_t bme280_init(i2c_bus_handle_t bus, bme280_handle_t *handle);
esp_err_t bme280_read_data(bme280_handle_t *handle, bme280_data_t *data);
esp_err_t bme280_soft_reset(bme280_handle_t *handle);
bme280_sensor_type_t bme280_get_sensor_type(bme280_handle_t *handle);
bool bme280_has_humidity(bme280_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif /* BME280_H */
