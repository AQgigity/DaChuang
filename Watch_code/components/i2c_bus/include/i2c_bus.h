#ifndef I2C_BUS_H
#define I2C_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifndef I2C_BUS_SDA_PIN
#define I2C_BUS_SDA_PIN     GPIO_NUM_5
#endif

#ifndef I2C_BUS_SCL_PIN
#define I2C_BUS_SCL_PIN     GPIO_NUM_4
#endif

#ifndef I2C_BUS_CLK_SPEED
#define I2C_BUS_CLK_SPEED   400000
#endif

typedef struct {
    i2c_port_t  port;
    int         sda_pin;
    int         scl_pin;
    uint32_t    clk_speed;
} i2c_bus_config_t;

/* Opaque handle */
typedef struct i2c_bus_t *i2c_bus_handle_t;

esp_err_t i2c_bus_init(const i2c_bus_config_t *config, i2c_bus_handle_t *out_handle);

esp_err_t i2c_bus_add_device(i2c_bus_handle_t bus, uint16_t addr,
                             uint32_t speed, i2c_master_dev_handle_t *out_dev);

i2c_master_bus_handle_t i2c_bus_get_handle(i2c_bus_handle_t bus);

esp_err_t i2c_bus_deinit(i2c_bus_handle_t bus);

#ifdef __cplusplus
}
#endif

#endif /* I2C_BUS_H */
