idf_component_register(
    SRCS "src/display.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd esp_timer lvgl
)
