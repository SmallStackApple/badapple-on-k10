#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "k10_audio.h"
#include "k10_board.h"
#include "k10_lcd.h"
#include "media.h"
#include "player.h"

static const char *TAG = "badapple";

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Bad Apple on UNIHIKER K10");

    ESP_ERROR_CHECK(k10_board_init());
    ESP_ERROR_CHECK(k10_lcd_init());
    ESP_ERROR_CHECK(k10_audio_init(16000));

    static media_t media;
    esp_err_t err = media_open(&media);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "media open failed: %s", esp_err_to_name(err));
        k10_lcd_fill(k10_rgb565(255, 0, 0));
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    k10_lcd_fill(k10_rgb565(0, 0, 0));
    ESP_LOGI(TAG, "starting player");
    ESP_ERROR_CHECK(player_start(&media));
}
