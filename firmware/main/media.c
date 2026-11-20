#include "media.h"

#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "media";

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

esp_err_t media_open(media_t *m)
{
    memset(m, 0, sizeof(*m));

    m->part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                       ESP_PARTITION_SUBTYPE_ANY, "media");
    if (!m->part) {
        ESP_LOGE(TAG, "media partition not found (flash the blob at 0x210000)");
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t hdr[36];
    ESP_RETURN_ON_ERROR(esp_partition_read(m->part, 0, hdr, sizeof(hdr)),
                        TAG, "read header");
    if (memcmp(hdr, "BAV1", 4) != 0) {
        ESP_LOGE(TAG, "bad media magic");
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t version = rd16(hdr + 4);
    m->width = rd16(hdr + 6);
    m->height = rd16(hdr + 8);
    m->fps = rd16(hdr + 10);
    m->frame_count = rd32(hdr + 12);
    m->audio_rate = rd32(hdr + 16);
    m->audio_samples = rd32(hdr + 20);
    m->frame_data_off = rd32(hdr + 24);
    m->audio_off = rd32(hdr + 28);
    m->audio_bytes = rd32(hdr + 32);

    if (version != 1 || m->width != 304 || m->height != 228 || m->fps != 30 ||
        m->audio_rate != 16000 || m->frame_count == 0) {
        ESP_LOGE(TAG, "unsupported media: v%u %ux%u@%uHz %u frames audio %uHz",
                 version, m->width, m->height, m->fps, m->frame_count, m->audio_rate);
        return ESP_ERR_INVALID_VERSION;
    }

    size_t table_size = (size_t)m->frame_count * sizeof(uint32_t);
    m->offsets = heap_caps_malloc(table_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!m->offsets) {
        ESP_LOGE(TAG, "offsets table allocation failed (%u bytes)",
                 (unsigned)table_size);
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(esp_partition_read(m->part, 36, m->offsets, table_size),
                        TAG, "read offsets");

    if (m->audio_off + m->audio_bytes > m->part->size ||
        m->frame_data_off + 4 > m->part->size) {
        ESP_LOGE(TAG, "media offsets outside partition");
        return ESP_ERR_INVALID_SIZE;
    }

    ESP_LOGI(TAG, "media: %ux%u @%ufps, %lu frames, audio %lu Hz %lu bytes, %lu s",
             m->width, m->height, m->fps, (unsigned long)m->frame_count,
             (unsigned long)m->audio_rate, (unsigned long)m->audio_bytes,
             (unsigned long)(m->audio_samples / m->audio_rate));
    return ESP_OK;
}

void media_close(media_t *m)
{
    if (m->offsets) {
        heap_caps_free(m->offsets);
        m->offsets = NULL;
    }
}

esp_err_t media_read_frame(media_t *m, uint32_t index, uint8_t *buf, size_t cap,
                           size_t *len)
{
    if (index >= m->frame_count) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t off = m->offsets[index];
    uint16_t n = 0;
    ESP_RETURN_ON_ERROR(esp_partition_read(m->part, off, &n, sizeof(n)),
                        TAG, "read frame len");
    if (n > cap) {
        ESP_LOGE(TAG, "frame %lu length %u > cap %u", (unsigned long)index, n,
                 (unsigned)cap);
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_RETURN_ON_ERROR(esp_partition_read(m->part, off + 2, buf, n),
                        TAG, "read frame data");
    *len = n;
    return ESP_OK;
}

esp_err_t media_read_audio(media_t *m, uint32_t pos, void *buf, size_t len,
                           size_t *read_len)
{
    if (pos >= m->audio_bytes) {
        *read_len = 0;
        return ESP_OK;
    }
    size_t n = len;
    if (n > m->audio_bytes - pos) {
        n = m->audio_bytes - pos;
    }
    n &= ~1u; /* whole 16-bit samples */
    if (n == 0) {
        *read_len = 0;
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_partition_read(m->part, m->audio_off + pos, buf, n),
                        TAG, "read audio");
    *read_len = n;
    return ESP_OK;
}
