idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES i2c_bus bme280 max30102 display UI bt nvs_flash mqtt esp_wifi esp_event esp_adc)
