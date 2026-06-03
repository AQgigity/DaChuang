/*
 * display.c — ST7789V3 240x280 SPI display + LVGL
 *
 * Color fix: ST7789 defaults to BGR channel order.
 *   We send MADCTL=0x00 to force RGB order.
 *   ESP32-S3 is little-endian, ST7789 expects big-endian → byte swap needed.
 *   LV_COLOR_16_SWAP=1 handles this in LVGL.
 *   Test values are hand byte-swapped RGB565.
 */

#include "display.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DISPLAY";

#define DRAW_BUF_LINES  40
#define DRAW_BUF_SIZE   (DISPLAY_WIDTH * DRAW_BUF_LINES * sizeof(lv_color_t))

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;
static lv_disp_draw_buf_t     s_draw_buf;
static lv_disp_drv_t          s_disp_drv;
static lv_color_t            *s_buf1;
static lv_color_t            *s_buf2;

/* ==================== Backlight ==================== */

static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t ch = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .gpio_num   = DISPLAY_PIN_BL,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&ch);
}

void display_set_brightness(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)percent * 1023 / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* ==================== LVGL flush ==================== */

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    esp_lcd_panel_draw_bitmap(s_panel, area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1, color_p);
    lv_disp_flush_ready(drv);
}

/* ==================== LVGL tick ==================== */

static esp_timer_handle_t s_tick_timer = NULL;

static void tick_timer_cb(void *arg)
{
    lv_tick_inc(5);
}

static void lvgl_tick_init(void)
{
    const esp_timer_create_args_t args = {
        .callback = tick_timer_cb,
        .name     = "lvgl_tick",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_tick_timer, 5000));
}

/* ==================== display_init ==================== */

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing ST7789V3...");

    /* SPI bus */
    spi_bus_config_t bus_cfg = {
        .sclk_io_num     = DISPLAY_PIN_SCLK,
        .mosi_io_num     = DISPLAY_PIN_MOSI,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    /* SPI panel IO */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = DISPLAY_PIN_DC,
        .cs_gpio_num       = DISPLAY_PIN_CS,
        .pclk_hz           = 40 * 1000 * 1000,
        .lcd_cmd_bits       = 8,
        .lcd_param_bits     = 8,
        .spi_mode           = 0,
        .trans_queue_depth  = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                             &io_cfg, &s_io));

    /* ST7789V3 panel */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = DISPLAY_PIN_RST,
        .rgb_endian     = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));

    /* Force RGB channel order (default is BGR) */
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_set_gap(s_panel, 0, 20);
    esp_lcd_panel_disp_on_off(s_panel, true);

    /* MADCTL=0x00: RGB order, portrait, no mirror */
    uint8_t madctl = 0x00;
    esp_lcd_panel_io_tx_param(s_io, 0x36, &madctl, 1);

    ESP_LOGI(TAG, "ST7789V3 ready (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);

    /* LVGL */
    lv_init();

    s_buf1 = heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_buf2 = heap_caps_malloc(DRAW_BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(s_buf1 && s_buf2);

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2,
                          DISPLAY_WIDTH * DRAW_BUF_LINES);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = DISPLAY_WIDTH;
    s_disp_drv.ver_res  = DISPLAY_HEIGHT;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    lvgl_tick_init();

    backlight_init();
    display_set_brightness(100);

    ESP_LOGI(TAG, "Display init complete");
}

/* ==================== Test: color bars ==================== */
/*
 * 8 bars: R G B W Y C M K
 * RGB565 byte-swapped for ST7789 big-endian.
 */

void display_test_bars(void)
{
    static const uint16_t colors[8] = {
        0x00F8, /* Red    (0xF800 swapped) */
        0xE007, /* Green  (0x07E0 swapped) */
        0x1F00, /* Blue   (0x001F swapped) */
        0xFFFF, /* White  */
        0xE0FF, /* Yellow (0xFFE0 swapped) */
        0xFF07, /* Cyan   (0x07FF swapped) */
        0x1FF8, /* Magenta(0xF81F swapped) */
        0x0000, /* Black  */
    };

    uint16_t *line = heap_caps_malloc(DISPLAY_WIDTH * sizeof(uint16_t),
                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(line);

    for (int x = 0; x < DISPLAY_WIDTH; x++) {
        int bar = x / 30;
        if (bar > 7) bar = 7;
        line[x] = colors[bar];
    }

    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, y, DISPLAY_WIDTH, y + 1, line);
    }

    heap_caps_free(line);
    ESP_LOGI(TAG, "Test bars drawn");
}
