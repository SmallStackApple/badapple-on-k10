#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t k10_board_init(void);
void      k10_i2c_scan(void);

esp_err_t k10_tca_read(uint8_t reg, uint8_t *val);
esp_err_t k10_tca_write(uint8_t reg, uint8_t val);

esp_err_t k10_set_backlight(bool on);
esp_err_t k10_set_amp(bool on);
esp_err_t k10_read_inputs(uint8_t *p0, uint8_t *p1);
