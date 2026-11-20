#include "player.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

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
#define AUDIO_CHUNK    2048          /* mono samples per read */
#define AUDIO_VOLUME   6             /* /10 */

static uint16_t *s_fb;
static uint8_t *s_rle;
static int16_t *s_pcm;
static int16_t *s_stereo;

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
    uint32_t last_seq = s_restart_seq;
    bool started = false;

    while (1) {
        if (s_restart_seq != last_seq) {
            last_seq = s_restart_seq;
            pos = 0;
            started = false;
            k10_audio_flush();
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
        if (pos >= m->audio_bytes) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t n = 0;
        if (media_read_audio(m, pos, s_pcm, AUDIO_CHUNK * sizeof(int16_t), &n) != ESP_OK ||
            n == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int samples = (int)(n / sizeof(int16_t));
        for (int i = 0; i < samples; i++) {
            int16_t v = (int16_t)((int32_t)s_pcm[i] * AUDIO_VOLUME / 10);
            s_stereo[i * 2] = v;
            s_stereo[i * 2 + 1] = v;
        }
        k10_audio_write(s_stereo, samples);
        pos += (uint32_t)n;
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
    s_fb = heap_caps_malloc(fb_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    s_rle = heap_caps_malloc(RLE_CAP, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_pcm = heap_caps_malloc(AUDIO_CHUNK * sizeof(int16_t),
                             MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_stereo = heap_caps_malloc(AUDIO_CHUNK * 2 * sizeof(int16_t),
                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_fb || !s_rle || !s_pcm || !s_stereo) {
        ESP_LOGE(TAG, "buffer allocation failed (fb=%u)", (unsigned)fb_bytes);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "buffers: fb=%u rle=%u audio=%u, free internal=%u dma=%u",
             (unsigned)fb_bytes, RLE_CAP,
             (unsigned)(AUDIO_CHUNK * 2 * sizeof(int16_t)),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

    start_set(esp_timer_get_time() + 500000);
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
