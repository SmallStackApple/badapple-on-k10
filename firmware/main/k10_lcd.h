#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t k10_lcd_init(void);
esp_err_t k10_lcd_draw_bitmap(int x, int y, int w, int h, const uint16_t *rgb565);
esp_err_t k10_lcd_fill(uint16_t rgb565);
void      k10_lcd_test_pattern(void);

/* Byte-swapped RGB565, ready to be sent over SPI. */
static inline uint16_t k10_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return __builtin_bswap16(c);
}
