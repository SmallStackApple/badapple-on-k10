#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t k10_audio_init(uint32_t sample_rate);
esp_err_t k10_audio_write(const int16_t *stereo_interleaved, size_t frames);
void      k10_audio_tone(float freq_hz, int duration_ms);
void      k10_audio_stop(void);
void      k10_audio_flush(void);
