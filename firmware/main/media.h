#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_partition.h"

typedef struct {
    const esp_partition_t *part;
    uint16_t width;
    uint16_t height;
    uint16_t fps;
    uint32_t frame_count;
    uint32_t audio_rate;
    uint32_t audio_samples;
    uint32_t audio_bytes;
    uint32_t frame_data_off;
    uint32_t audio_off;
    uint32_t *offsets;
} media_t;

esp_err_t media_open(media_t *m);
void media_close(media_t *m);
esp_err_t media_read_frame(media_t *m, uint32_t index, uint8_t *buf, size_t cap, size_t *len);
esp_err_t media_read_audio(media_t *m, uint32_t pos, void *buf, size_t len, size_t *read_len);
