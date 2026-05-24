#include "display.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DISPLAY";

#define DRAW_BUF_LINES  40
#define DRAW_BUF_SIZE   (DISPLAY_WIDTH * DRAW_BUF_LINES * sizeof(lv_color_t))

static esp_lcd_panel_io_handle_t s_io = NULL;
static lv_disp_draw_buf_t        s_draw_buf;
static lv_disp_drv_t             s_disp_drv;
static lv_color_t               *s_buf1;
static lv_color_t               *s_buf2;

/* ---------- Low-level NV3030B commands ---------- */

static void nv_cmd(uint8_t cmd)
{
    esp_lcd_panel_io_tx_param(s_io, cmd, NULL, 0);
}

static void nv_cmd_p1(uint8_t cmd, uint8_t p)
{
    esp_lcd_panel_io_tx_param(s_io, cmd, &p, 1);
}

static void nv_cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    esp_lcd_panel_io_tx_param(s_io, cmd, data, len);
}

static void nv_data(const void *data, size_t len)
{
    esp_lcd_panel_io_tx_color(s_io, -1, data, len);
}

/* ---------- Hardware reset ---------- */

static void hw_reset(void)
{
    gpio_set_direction(DISPLAY_PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(DISPLAY_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(DISPLAY_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(DISPLAY_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

/* ---------- NV3030B init (merchant reference) ---------- */

static void nv3030b_init(void)
{
    hw_reset();

    /* EXTC Command Set Enable */
    {
        uint8_t d[] = {0x06, 0x08};
        nv_cmd_data(0xFD, d, 2);
    }

    {
        uint8_t d[] = {0x07, 0x04};
        nv_cmd_data(0x61, d, 2);
    }

    {
        uint8_t d[] = {0x00, 0x44, 0x45};
        nv_cmd_data(0x62, d, 3);
    }

    {
        uint8_t d[] = {0x41, 0x07, 0x12, 0x12};
        nv_cmd_data(0x63, d, 4);
    }

    nv_cmd_p1(0x64, 0x37);

    /* VSP */
    {
        uint8_t d[] = {0x09, 0x10, 0x21};
        nv_cmd_data(0x65, d, 3);
    }

    /* VSN */
    {
        uint8_t d[] = {0x09, 0x10, 0x21};
        nv_cmd_data(0x66, d, 3);
    }

    {
        uint8_t d[] = {0x21, 0x40};
        nv_cmd_data(0x67, d, 2);
    }

    /* gamma vap/van */
    {
        uint8_t d[] = {0x90, 0x4C, 0x50, 0x70};
        nv_cmd_data(0x68, d, 4);
    }

    /* frame rate */
    {
        uint8_t d[] = {0x0F, 0x02, 0x01};
        nv_cmd_data(0xB1, d, 3);
    }

    nv_cmd_p1(0xB4, 0x01);

    /* porch */
    {
        uint8_t d[] = {0x02, 0x02, 0x0A, 0x14};
        nv_cmd_data(0xB5, d, 4);
    }

    {
        uint8_t d[] = {0x04, 0x01, 0x9F, 0x00, 0x02};
        nv_cmd_data(0xB6, d, 5);
    }

    nv_cmd_p1(0xDF, 0x11);

    /* GAMMA */
    {
        uint8_t d[] = {0x03, 0x00, 0x00, 0x30, 0x33, 0x3F};
        nv_cmd_data(0xE2, d, 6);
    }
    {
        uint8_t d[] = {0x3F, 0x33, 0x30, 0x00, 0x00, 0x03};
        nv_cmd_data(0xE5, d, 6);
    }
    {
        uint8_t d[] = {0x05, 0x67};
        nv_cmd_data(0xE1, d, 2);
    }
    {
        uint8_t d[] = {0x67, 0x06};
        nv_cmd_data(0xE4, d, 2);
    }
    {
        uint8_t d[] = {0x05, 0x06, 0x0A, 0x0C, 0x0B, 0x0B, 0x13, 0x19};
        nv_cmd_data(0xE0, d, 8);
    }
    {
        uint8_t d[] = {0x18, 0x13, 0x0D, 0x09, 0x0B, 0x0B, 0x05, 0x06};
        nv_cmd_data(0xE3, d, 8);
    }

    /* source */
    {
        uint8_t d[] = {0x00, 0xFF};
        nv_cmd_data(0xE6, d, 2);
    }
    {
        uint8_t d[] = {0x01, 0x04, 0x03, 0x03, 0x00, 0x12};
        nv_cmd_data(0xE7, d, 6);
    }
    {
        uint8_t d[] = {0x00, 0x70, 0x00};
        nv_cmd_data(0xE8, d, 3);
    }

    /* gate */
    nv_cmd_p1(0xEC, 0x52);

    {
        uint8_t d[] = {0x01, 0x01, 0x02};
        nv_cmd_data(0xF1, d, 3);
    }
    {
        uint8_t d[] = {0x01, 0x30, 0x00, 0x00};
        nv_cmd_data(0xF6, d, 4);
    }

    {
        uint8_t d[] = {0xFA, 0xFC};
        nv_cmd_data(0xFD, d, 2);
    }

    /* COLMOD: 16bit/pixel */
    nv_cmd_p1(0x3A, 0x55);

    /* Tearing Effect */
    nv_cmd_p1(0x35, 0x00);

    /* MADCTL: portrait, BGR */
    nv_cmd_p1(0x36, 0x08);

    /* Display Inversion ON */
    nv_cmd(0x21);

    nv_cmd(0x11); /* exit sleep */
    vTaskDelay(pdMS_TO_TICKS(200));
    nv_cmd(0x29); /* display on */
    vTaskDelay(pdMS_TO_TICKS(20));
    nv_cmd(0x2C); /* memory write */

    ESP_LOGI(TAG, "NV3030B init done");
}

/* ---------- Set draw window (16-bit addr, y+20 offset) ---------- */

static void set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    uint16_t ys = y1 + 20, ye = y2 + 20;
    uint8_t ca[] = {(x1 >> 8) & 0xFF, x1 & 0xFF, (x2 >> 8) & 0xFF, x2 & 0xFF};
    nv_cmd_data(0x2A, ca, 4);

    uint8_t ra[] = {(ys >> 8) & 0xFF, ys & 0xFF, (ye >> 8) & 0xFF, ye & 0xFF};
    nv_cmd_data(0x2B, ra, 4);

    nv_cmd(0x2C); /* RAMWR */
}

/* ---------- Backlight ---------- */

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

/* ---------- LVGL flush callback (with byte swap for NV3030B) ---------- */

static uint16_t s_swap_buf[DISPLAY_WIDTH * DRAW_BUF_LINES];

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint16_t x1 = area->x1, y1 = area->y1;
    uint16_t x2 = area->x2, y2 = area->y2;
    uint32_t pixels = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1);

    /* RGB565 → BGR565: swap R and B channels */
    for (uint32_t i = 0; i < pixels; i++) {
        uint16_t c = color_p[i].full;
        uint16_t r = (c >> 11) & 0x1F;
        uint16_t b = c & 0x1F;
        s_swap_buf[i] = (b << 11) | (c & 0x07E0) | r;
    }

    set_window(x1, y1, x2, y2);
    nv_data(s_swap_buf, pixels * 2);

    lv_disp_flush_ready(drv);
}

/* ---------- LVGL tick ---------- */

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

/* ---------- Public API ---------- */

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing NV3030B display...");

    /* 1. SPI bus */
    spi_bus_config_t bus_cfg = {
        .sclk_io_num     = DISPLAY_PIN_SCLK,
        .mosi_io_num     = DISPLAY_PIN_MOSI,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = DISPLAY_WIDTH * DRAW_BUF_LINES * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    /* 2. SPI IO */
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num       = DISPLAY_PIN_DC,
        .cs_gpio_num       = DISPLAY_PIN_CS,
        .pclk_hz           = 1 * 1000 * 1000,
        .lcd_cmd_bits       = 8,
        .lcd_param_bits     = 8,
        .spi_mode           = 0,
        .trans_queue_depth  = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST,
                                             &io_cfg, &s_io));

    /* 3. NV3030B panel init */
    nv3030b_init();

    ESP_LOGI(TAG, "NV3030B ready (%dx%d)", DISPLAY_WIDTH, DISPLAY_HEIGHT);

    /* 4. LVGL */
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

    /* 5. Backlight */
    backlight_init();
    display_set_brightness(80);

    ESP_LOGI(TAG, "Display init complete");
}
