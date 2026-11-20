#include "k10_audio.h"
#include "k10_pins.h"

#include <math.h>
#include <string.h>
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "k10_audio";

static i2s_chan_handle_t s_tx;
static uint32_t s_rate = 16000;

esp_err_t k10_audio_init(uint32_t sample_rate)
{
    s_rate = sample_rate;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = 128;
    chan_cfg.auto_clear_after_cb = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "i2s channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = K10_I2S_MCLK,
            .bclk = K10_I2S_BCLK,
            .ws = K10_I2S_WS,
            .dout = K10_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "i2s std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "i2s enable");

    ESP_LOGI(TAG, "I2S ready: %lu Hz, MCLK=%d BCLK=%d WS=%d DOUT=%d",
             (unsigned long)sample_rate, K10_I2S_MCLK, K10_I2S_BCLK,
             K10_I2S_WS, K10_I2S_DOUT);
    return ESP_OK;
}

esp_err_t k10_audio_write(const int16_t *stereo_interleaved, size_t frames)
{
    size_t bytes = frames * 2 * sizeof(int16_t);
    size_t written = 0;
    return i2s_channel_write(s_tx, stereo_interleaved, bytes, &written, portMAX_DELAY);
}

void k10_audio_stop(void)
{
    if (s_tx) {
        i2s_channel_disable(s_tx);
    }
}

void k10_audio_flush(void)
{
    if (s_tx) {
        i2s_channel_disable(s_tx);
        i2s_channel_enable(s_tx);
    }
}

void k10_audio_tone(float freq_hz, int duration_ms)
{
    const int chunk_frames = 256;
    int16_t buf[chunk_frames * 2];
    int total_frames = (int)((int64_t)s_rate * duration_ms / 1000);
    int done = 0;
    float phase = 0.0f;
    float step = 2.0f * (float)M_PI * freq_hz / (float)s_rate;

    while (done < total_frames) {
        int n = (total_frames - done < chunk_frames) ? (total_frames - done) : chunk_frames;
        for (int i = 0; i < n; i++) {
            int16_t s = (int16_t)(sinf(phase) * 9000.0f);
            buf[i * 2] = s;
            buf[i * 2 + 1] = s;
            phase += step;
            if (phase > 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }
        k10_audio_write(buf, n);
        done += n;
    }
}
