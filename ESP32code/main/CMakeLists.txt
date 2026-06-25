idf_component_register(
    SRCS "main.cpp" "ble_peripheral.c" "gait_inference.cpp" "gait_sensor.c"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash mpu6050 fsr402 edge-impulse-sdk
)
