#ifndef DISPLAY_H
#define DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Pin definitions */
#define DISPLAY_PIN_SCLK    12
#define DISPLAY_PIN_MOSI    11
#define DISPLAY_PIN_DC      9
#define DISPLAY_PIN_RST     10
#define DISPLAY_PIN_CS      13
#define DISPLAY_PIN_BL      14

/* Display parameters */
#define DISPLAY_WIDTH       240
#define DISPLAY_HEIGHT      280

/**
 * @brief Initialize SPI bus, NV3030B panel, LVGL, and backlight
 * Must be called before any LVGL or UI functions.
 */
void display_init(void);

/**
 * @brief Set backlight brightness
 * @param percent Brightness 0-100
 */
void display_set_brightness(int percent);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_H */
