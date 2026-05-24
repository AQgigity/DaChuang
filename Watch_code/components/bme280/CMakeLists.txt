idf_component_register(SRCS "src/bme280.c"
                       INCLUDE_DIRS "include"
                       REQUIRES i2c_bus driver)
