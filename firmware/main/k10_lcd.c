#include "k10_lcd.h"
#include "k10_pins.h"

#include <string.h>
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "k10_lcd";

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;

/* MADCTL for landscape, 180° flipped: MX | MY | MV = 0xE0 (BGR cleared). */
#define K10_LCD_MADCTL 0xE0

static bool lcd_color_done(esp_lcd_panel_io_handle_t io,
                           esp_lcd_panel_io_event_data_t *edata,
                           void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

static esp_err_t lcd_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(s_io, cmd, data, len);
}

static esp_err_t lcd_set_window(int x0, int y0, int x1, int y1)
{
    uint8_t xs[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    uint8_t ys[4] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};
    ESP_RETURN_ON_ERROR(lcd_cmd(0x2A, xs, sizeof(xs)), TAG, "set x");
    ESP_RETURN_ON_ERROR(lcd_cmd(0x2B, ys, sizeof(ys)), TAG, "set y");
    return ESP_OK;
}

static esp_err_t lcd_wait_done(void)
{
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "color transfer timeout");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t k10_lcd_init(void)
{
    s_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_done, ESP_ERR_NO_MEM, TAG, "semaphore");

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = K10_LCD_MOSI,
        .miso_io_num = K10_LCD_MISO,
        .sclk_io_num = K10_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 32 * 1024,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(K10_LCD_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = K10_LCD_CS,
        .dc_gpio_num = K10_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = K10_LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth = 4,
        .on_color_trans_done = lcd_color_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(K10_LCD_HOST, &io_cfg, &s_io),
                        TAG, "panel io");

    /* Software reset + sleep out. */
    lcd_cmd(0x01, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    static const uint8_t ef[] = {0x03, 0x80, 0x02};
    static const uint8_t cf[] = {0x00, 0xC1, 0x30};
    static const uint8_t ed[] = {0x64, 0x03, 0x12, 0x81};
    static const uint8_t e8[] = {0x85, 0x00, 0x78};
    static const uint8_t cb[] = {0x39, 0x2C, 0x00, 0x34, 0x02};
    static const uint8_t f7[] = {0x20};
    static const uint8_t ea[] = {0x00, 0x00};
    static const uint8_t c0[] = {0x23};
    static const uint8_t c1[] = {0x10};
    static const uint8_t c5[] = {0x3E, 0x28};
    static const uint8_t c7[] = {0x86};
    static const uint8_t madctl[] = {K10_LCD_MADCTL};
    static const uint8_t pixfmt[] = {0x55};
    static const uint8_t b1[] = {0x00, 0x13};
    static const uint8_t b6[] = {0x08, 0x82, 0x27};
    static const uint8_t f2[] = {0x00};
    static const uint8_t gamma_sel[] = {0x01};
    static const uint8_t gmctrp1[] = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1,
                                      0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
    static const uint8_t gmctrn1[] = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1,
                                      0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};

    lcd_cmd(0xEF, ef, sizeof(ef));
    lcd_cmd(0xCF, cf, sizeof(cf));
    lcd_cmd(0xED, ed, sizeof(ed));
    lcd_cmd(0xE8, e8, sizeof(e8));
    lcd_cmd(0xCB, cb, sizeof(cb));
    lcd_cmd(0xF7, f7, sizeof(f7));
    lcd_cmd(0xEA, ea, sizeof(ea));
    lcd_cmd(0xC0, c0, sizeof(c0));
    lcd_cmd(0xC1, c1, sizeof(c1));
    lcd_cmd(0xC5, c5, sizeof(c5));
    lcd_cmd(0xC7, c7, sizeof(c7));
    lcd_cmd(0x36, madctl, sizeof(madctl));
    lcd_cmd(0x3A, pixfmt, sizeof(pixfmt));
    lcd_cmd(0xB1, b1, sizeof(b1));
    lcd_cmd(0xB6, b6, sizeof(b6));
    lcd_cmd(0xF2, f2, sizeof(f2));
    lcd_cmd(0x26, gamma_sel, sizeof(gamma_sel));
    lcd_cmd(0xE0, gmctrp1, sizeof(gmctrp1));
    lcd_cmd(0xE1, gmctrn1, sizeof(gmctrn1));

    lcd_cmd(0x11, NULL, 0);          /* sleep out */
    vTaskDelay(pdMS_TO_TICKS(120));
    /* No 0x21 inversion: the panel showed white as black with it enabled. */
    lcd_cmd(0x13, NULL, 0);          /* normal display mode */
    lcd_cmd(0x29, NULL, 0);          /* display on */
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "ILI9341 ready: %dx%d, MADCTL=0x%02X, SPI=%dHz",
             K10_LCD_H_RES, K10_LCD_V_RES, K10_LCD_MADCTL, K10_LCD_PIXEL_CLOCK_HZ);

    return k10_lcd_fill(k10_rgb565(0, 0, 0));
}

esp_err_t k10_lcd_draw_bitmap(int x, int y, int w, int h, const uint16_t *rgb565)
{
    /* Keep each transfer under 32KB to stay within the SPI DMA limits. */
    const int max_rows = 32;
    for (int row = 0; row < h; row += max_rows) {
        int rows = (h - row < max_rows) ? (h - row) : max_rows;
        ESP_RETURN_ON_ERROR(lcd_set_window(x, y + row, x + w - 1, y + row + rows - 1),
                            TAG, "window");
        ESP_RETURN_ON_ERROR(
            esp_lcd_panel_io_tx_color(s_io, 0x2C, rgb565 + (size_t)row * w,
                                      (size_t)w * rows * sizeof(uint16_t)),
            TAG, "tx color");
        ESP_RETURN_ON_ERROR(lcd_wait_done(), TAG, "wait");
    }
    return ESP_OK;
}

esp_err_t k10_lcd_fill(uint16_t rgb565)
{
    const int rows = 24;
    uint16_t *line = heap_caps_malloc((size_t)K10_LCD_H_RES * rows * sizeof(uint16_t),
                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(line, ESP_ERR_NO_MEM, TAG, "fill buffer");
    for (size_t i = 0; i < (size_t)K10_LCD_H_RES * rows; i++) {
        line[i] = rgb565;
    }
    esp_err_t err = ESP_OK;
    for (int y = 0; y < K10_LCD_V_RES; y += rows) {
        int n = (K10_LCD_V_RES - y < rows) ? (K10_LCD_V_RES - y) : rows;
        err = k10_lcd_draw_bitmap(0, y, K10_LCD_H_RES, n, line);
        if (err != ESP_OK) {
            break;
        }
    }
    heap_caps_free(line);
    return err;
}

void k10_lcd_test_pattern(void)
{
    const int w = K10_LCD_H_RES;
    const int h = K10_LCD_V_RES;
    uint16_t *fb = heap_caps_malloc((size_t)w * h * sizeof(uint16_t),
                                    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!fb) {
        ESP_LOGE(TAG, "test framebuffer allocation failed");
        return;
    }

    const uint16_t red = k10_rgb565(255, 0, 0);
    const uint16_t green = k10_rgb565(0, 255, 0);
    const uint16_t blue = k10_rgb565(0, 0, 255);
    const uint16_t white = k10_rgb565(255, 255, 255);
    const uint16_t black = k10_rgb565(0, 0, 0);
    const uint16_t gray = k10_rgb565(128, 128, 128);

    for (int y = 0; y < h; y++) {
        uint16_t c;
        if (y < 40) c = red;
        else if (y < 80) c = green;
        else if (y < 120) c = blue;
        else if (y < 160) c = white;
        else if (y < 200) c = gray;
        else c = black;
        for (int x = 0; x < w; x++) {
            fb[y * w + x] = c;
        }
    }

    /* Orientation markers: black square top-left, white square top-right. */
    for (int y = 8; y < 32; y++) {
        for (int x = 8; x < 32; x++) fb[y * w + x] = black;
        for (int x = w - 32; x < w - 8; x++) fb[y * w + x] = white;
    }
    /* Bottom-left white, bottom-right black. */
    for (int y = h - 32; y < h - 8; y++) {
        for (int x = 8; x < 32; x++) fb[y * w + x] = white;
        for (int x = w - 32; x < w - 8; x++) fb[y * w + x] = black;
    }

    k10_lcd_draw_bitmap(0, 0, w, h, fb);
    heap_caps_free(fb);
}
