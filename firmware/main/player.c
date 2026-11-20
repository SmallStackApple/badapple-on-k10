#include "player.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "k10_audio.h"
#include "k10_board.h"
#include "k10_lcd.h"
#include "k10_pins.h"

static const char *TAG = "player";

#define RLE_CAP        4096
#define AUDIO_MAX_SAMPLES 2048       /* upper bound for one ADPCM block */
#define AUDIO_VOLUME   45            /* percent */
#define EQ_FREQ_HZ     3500.0f       /* tame the small-speaker harshness */
#define EQ_Q           1.2f
#define EQ_GAIN_DB     (-5.0f)

typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} biquad_t;

static biquad_t s_eq;

static void eq_init(biquad_t *q, float f0, float fs, float qv, float db)
{
    float a = powf(10.0f, db / 40.0f);
    float w0 = 2.0f * (float)M_PI * f0 / fs;
    float cosw = cosf(w0);
    float alpha = sinf(w0) / (2.0f * qv);
    float a0 = 1.0f + alpha / a;
    q->b0 = (1.0f + alpha * a) / a0;
    q->b1 = (-2.0f * cosw) / a0;
    q->b2 = (1.0f - alpha * a) / a0;
    q->a1 = (-2.0f * cosw) / a0;
    q->a2 = (1.0f - alpha / a) / a0;
    q->x1 = q->x2 = q->y1 = q->y2 = 0.0f;
}

static inline float eq_process(biquad_t *q, float x)
{
    float y = q->b0 * x + q->b1 * q->x1 + q->b2 * q->x2 -
              q->a1 * q->y1 - q->a2 * q->y2;
    q->x2 = q->x1;
    q->x1 = x;
    q->y2 = q->y1;
    q->y1 = y;
    return y;
}

static uint16_t *s_fb;
static uint8_t *s_rle;
static int16_t *s_pcm;
static int16_t *s_stereo;
static uint8_t *s_adpcm;

static volatile bool s_paused;
static volatile uint32_t s_restart_seq;
static volatile int64_t s_start_us;
static portMUX_TYPE s_start_lock = portMUX_INITIALIZER_UNLOCKED;

static int64_t start_get(void)
{
    taskENTER_CRITICAL(&s_start_lock);
    int64_t v = s_start_us;
    taskEXIT_CRITICAL(&s_start_lock);
    return v;
}

static void start_set(int64_t v)
{
    taskENTER_CRITICAL(&s_start_lock);
    s_start_us = v;
    taskEXIT_CRITICAL(&s_start_lock);
}

static void decode_frame(const uint8_t *src, size_t len, uint16_t *fb,
                         uint32_t total)
{
    size_t p = 0;
    uint32_t out = 0;
    while (p < len && out < total) {
        uint8_t token = src[p++];
        uint32_t n = token & 0x7F;
        uint16_t color = (token & 0x80) ? 0xFFFF : 0x0000;
        if (n == 0) {
            continue;
        }
        if (out + n > total) {
            n = total - out;
        }
        while (n--) {
            fb[out++] = color;
        }
    }
    while (out < total) {
        fb[out++] = 0;
    }
}

/* Standard IMA ADPCM (adpcm_ima_wav) tables. */
static const int16_t ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209,
    230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876,
    963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
    10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086,
    29794, 32767,
};
static const int8_t ima_index[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

static int adpcm_decode_block(const uint8_t *src, size_t len, int16_t *out,
                              int max_samples)
{
    if (len < 4 || max_samples < 1) {
        return 0;
    }
    int32_t pred = (int16_t)(src[0] | (src[1] << 8));
    int index = src[2];
    if (index < 0) index = 0;
    if (index > 88) index = 88;

    int n = 0;
    out[n++] = (int16_t)pred;
    size_t p = 4;
    while (p < len && n < max_samples) {
        uint8_t byte = src[p++];
        for (int k = 0; k < 2 && n < max_samples; k++) {
            int nib = (k == 0) ? (byte & 0x0F) : (byte >> 4);
            int step = ima_step[index];
            int diff = step >> 3;
            if (nib & 1) diff += step >> 2;
            if (nib & 2) diff += step >> 1;
            if (nib & 4) diff += step;
            if (nib & 8) {
                pred -= diff;
            } else {
                pred += diff;
            }
            if (pred > 32767) pred = 32767;
            if (pred < -32768) pred = -32768;
            index += ima_index[nib];
            if (index < 0) index = 0;
            if (index > 88) index = 88;
            out[n++] = (int16_t)pred;
        }
    }
    return n;
}

enum {
    BTN_NONE = 0,
    BTN_A,
    BTN_B,
};

static int poll_buttons(void)
{
    uint8_t p0 = 0, p1 = 0;
    if (k10_read_inputs(&p0, &p1) != ESP_OK) {
        return BTN_NONE;
    }
    bool a = ((p1 & (1u << K10_BTN_A_BIT)) == 0);
    bool b = ((p0 & (1u << K10_BTN_B_BIT)) == 0);

    static bool a_prev, b_prev;
    static int64_t a_last, b_last;
    int64_t now = esp_timer_get_time();
    int result = BTN_NONE;

    if (a && !a_prev && now - a_last > 250000) {
        a_last = now;
        result = BTN_A;
    } else if (b && !b_prev && now - b_last > 250000) {
        b_last = now;
        result = BTN_B;
    }
    a_prev = a;
    b_prev = b;
    return result;
}

static void audio_task(void *arg)
{
    media_t *m = arg;
    uint32_t pos = 0;
    uint32_t decoded = 0;
    uint32_t last_seq = s_restart_seq;
    bool started = false;

    while (1) {
        if (s_restart_seq != last_seq) {
            last_seq = s_restart_seq;
            pos = 0;
            decoded = 0;
            started = false;
            k10_audio_flush();
            eq_init(&s_eq, EQ_FREQ_HZ, (float)m->audio_rate, EQ_Q, EQ_GAIN_DB);
        }
        if (s_paused) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (!started) {
            if (esp_timer_get_time() < start_get()) {
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            started = true;
        }
        if (pos >= m->audio_bytes || decoded >= m->audio_samples) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t read = 0;
        if (media_read_audio(m, pos, s_adpcm, m->audio_block_align, &read) != ESP_OK ||
            read < 4) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int samples = adpcm_decode_block(s_adpcm, read, s_pcm,
                                         (int)m->audio_samples_per_block);
        if (samples <= 0) {
            pos += (uint32_t)read;
            continue;
        }
        if (decoded + (uint32_t)samples > m->audio_samples) {
            samples = (int)(m->audio_samples - decoded);
        }
        if (samples <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        for (int i = 0; i < samples; i++) {
            float x = (float)s_pcm[i] / 32768.0f;
            float y = eq_process(&s_eq, x) * (AUDIO_VOLUME / 100.0f);
            if (y > 0.95f) y = 0.95f;
            if (y < -0.95f) y = -0.95f;
            int16_t v = (int16_t)(y * 32767.0f);
            s_stereo[i * 2] = v;
            s_stereo[i * 2 + 1] = v;
        }
        k10_audio_write(s_stereo, samples);
        pos += (uint32_t)read;
        decoded += (uint32_t)samples;
    }
}

static void video_task(void *arg)
{
    media_t *m = arg;
    uint32_t frame = 0;
    uint32_t last_seq = s_restart_seq;
    int64_t pause_start = 0;
    const int64_t period = 1000000LL / m->fps;

    while (1) {
        int btn = poll_buttons();
        if (btn == BTN_A) {
            if (s_paused) {
                s_paused = false;
                start_set(start_get() + esp_timer_get_time() - pause_start);
                ESP_LOGI(TAG, "resume");
            } else {
                s_paused = true;
                pause_start = esp_timer_get_time();
                ESP_LOGI(TAG, "pause");
            }
        } else if (btn == BTN_B) {
            s_paused = false;
            s_restart_seq++;
            start_set(esp_timer_get_time() + 300000);
            frame = 0;
            last_seq = s_restart_seq;
            ESP_LOGI(TAG, "restart");
        }

        if (s_restart_seq != last_seq) {
            last_seq = s_restart_seq;
            frame = 0;
        }
        if (s_paused) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        int64_t now = esp_timer_get_time();
        int64_t desired = start_get() + (int64_t)frame * period;
        if (now < desired) {
            int64_t wait_ms = (desired - now) / 1000;
            if (wait_ms < 1) wait_ms = 1;
            if (wait_ms > 10) wait_ms = 10;
            vTaskDelay(pdMS_TO_TICKS(wait_ms));
            continue;
        }

        if (now - desired > 3 * period) {
            uint32_t target = (uint32_t)(((now - start_get()) * m->fps) / 1000000LL);
            if (target >= m->frame_count) {
                s_restart_seq++;
                start_set(now + 500000);
                frame = 0;
                last_seq = s_restart_seq;
                continue;
            }
            frame = target;
            continue;
        }

        size_t len = 0;
        if (media_read_frame(m, frame, s_rle, RLE_CAP, &len) == ESP_OK) {
            decode_frame(s_rle, len, s_fb, (uint32_t)m->width * m->height);
            k10_lcd_draw_bitmap(K10_VIDEO_X, K10_VIDEO_Y, m->width, m->height, s_fb);
        }

        frame++;
        if (frame >= m->frame_count) {
            s_restart_seq++;
            start_set(esp_timer_get_time() + 500000);
            frame = 0;
            last_seq = s_restart_seq;
        }
    }
}

esp_err_t player_start(media_t *m)
{
    size_t fb_bytes = (size_t)m->width * m->height * sizeof(uint16_t);
    size_t pcm_bytes = (size_t)m->audio_samples_per_block * sizeof(int16_t);
    size_t stereo_bytes = (size_t)m->audio_samples_per_block * 2 * sizeof(int16_t);
    s_fb = heap_caps_malloc(fb_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_rle = heap_caps_malloc(RLE_CAP, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_pcm = heap_caps_malloc(pcm_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_stereo = heap_caps_malloc(stereo_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_adpcm = heap_caps_malloc(m->audio_block_align,
                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_fb || !s_rle || !s_pcm || !s_stereo || !s_adpcm) {
        ESP_LOGE(TAG, "buffer allocation failed (fb=%u)", (unsigned)fb_bytes);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "buffers: fb=%u rle=%u pcm=%u stereo=%u adpcm=%u, free internal=%u dma=%u",
             (unsigned)fb_bytes, RLE_CAP, (unsigned)pcm_bytes,
             (unsigned)stereo_bytes, (unsigned)m->audio_block_align,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

    start_set(esp_timer_get_time() + 500000);
    eq_init(&s_eq, EQ_FREQ_HZ, (float)m->audio_rate, EQ_Q, EQ_GAIN_DB);

    BaseType_t ok;
    ok = xTaskCreatePinnedToCore(audio_task, "audio", 4096, m, 6, NULL, 0);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ok = xTaskCreatePinnedToCore(video_task, "video", 8192, m, 5, NULL, 1);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
