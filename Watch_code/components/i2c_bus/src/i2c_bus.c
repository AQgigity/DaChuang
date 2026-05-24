#include "i2c_bus.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "I2C_BUS";

struct i2c_bus_t {
    i2c_master_bus_handle_t master_bus;
    i2c_port_t port;
};

esp_err_t i2c_bus_init(const i2c_bus_config_t *config, i2c_bus_handle_t *out_handle)
{
    if (!config || !out_handle) return ESP_ERR_INVALID_ARG;

    struct i2c_bus_t *bus = calloc(1, sizeof(struct i2c_bus_t));
    if (!bus) return ESP_ERR_NO_MEM;

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = config->port,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &bus->master_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
        free(bus);
        return ret;
    }

    bus->port = config->port;
    *out_handle = bus;

    ESP_LOGI(TAG, "I2C bus initialized (port=%d, SDA=%d, SCL=%d, %lukHz)",
             (int)config->port, (int)config->sda_pin, (int)config->scl_pin,
             (unsigned long)(config->clk_speed / 1000));
    return ESP_OK;
}

esp_err_t i2c_bus_add_device(i2c_bus_handle_t bus, uint16_t addr,
                             uint32_t speed, i2c_master_dev_handle_t *out_dev)
{
    if (!bus || !out_dev) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = speed,
    };

    esp_err_t ret = i2c_master_bus_add_device(bus->master_bus, &dev_cfg, out_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device 0x%02X: %s", addr, esp_err_to_name(ret));
    }
    return ret;
}

i2c_master_bus_handle_t i2c_bus_get_handle(i2c_bus_handle_t bus)
{
    return bus ? bus->master_bus : NULL;
}

esp_err_t i2c_bus_deinit(i2c_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = i2c_del_master_bus(bus->master_bus);
    free(bus);
    return ret;
}
