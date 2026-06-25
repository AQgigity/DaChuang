idf_component_register(
    SRCS "main.c" "ble_central.c" "wifi_mqtt.c" "sensor_task.c"
         "display_task.c" "emg_sensor.c" "data_fusion.c"
    INCLUDE_DIRS "."
    REQUIRES i2c_bus bme280 max30102 display UI bt nvs_flash mqtt esp_wifi esp_event esp_adc
)
