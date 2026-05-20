idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash mpu6050 fsr402 edge-impulse-sdk
)
