#include "k10_board.h"
#include "k10_pins.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "k10_board";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_tca;

esp_err_t k10_tca_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_tca, &reg, 1, val, 1, 100);
}

esp_err_t k10_tca_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_tca, buf, sizeof(buf), 100);
}

void k10_i2c_scan(void)
{
    ESP_LOGI(TAG, "I2C scan on SDA=%d SCL=%d:", K10_I2C_SDA, K10_I2C_SCL);
    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(s_bus, addr, 20) == ESP_OK) {
            ESP_LOGI(TAG, "  found 0x%02X", addr);
            found++;
        }
    }
    ESP_LOGI(TAG, "I2C scan done, %d device(s)", found);
}

esp_err_t k10_board_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = K10_I2C_SDA,
        .scl_io_num = K10_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = K10_TCA9555_ADDR,
        .scl_speed_hz = K10_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_tca),
                        TAG, "tca9555 add device");

    /* Probe before touching any register. */
    esp_err_t err = i2c_master_probe(s_bus, K10_TCA9555_ADDR, 50);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TCA9555 not found at 0x%02X", K10_TCA9555_ADDR);
        return err;
    }

    /* Start with every pin as an input so the probe cannot drive anything. */
    ESP_RETURN_ON_ERROR(k10_tca_write(K10_TCA_REG_CFG0, 0xFF), TAG, "cfg0");
    ESP_RETURN_ON_ERROR(k10_tca_write(K10_TCA_REG_CFG1, 0xFF), TAG, "cfg1");
    vTaskDelay(pdMS_TO_TICKS(5));

    uint8_t in0 = 0, in1 = 0;
    k10_tca_read(K10_TCA_REG_IN0, &in0);
    k10_tca_read(K10_TCA_REG_IN1, &in1);
    ESP_LOGI(TAG, "TCA9555 all-input baseline: IN0=0x%02X IN1=0x%02X", in0, in1);

    /*
     * K10 mapping (schematic pin names / initBoard.h):
     *   P0.0 output = LCD backlight
     *   P0.1 output = camera reset
     *   P0.5 input  = Button A  (eP5_KeyA)
     *   P1.1 input  = Button B  (eP11_KeyB)
     *   P1.7 output = speaker amp enable
     * All other pins are inputs for the probe.
     */
    ESP_RETURN_ON_ERROR(k10_tca_write(K10_TCA_REG_CFG0, 0xFC), TAG, "cfg0 apply");  /* P0.0/P0.1 out */
    ESP_RETURN_ON_ERROR(k10_tca_write(K10_TCA_REG_CFG1, 0x7F), TAG, "cfg1 apply");  /* P1.7 out */
    ESP_RETURN_ON_ERROR(k10_tca_write(K10_TCA_REG_OUT0, 0xFF), TAG, "out0");  /* BL + cam reset + input pull-ups */
    ESP_RETURN_ON_ERROR(k10_tca_write(K10_TCA_REG_OUT1, 0xFF), TAG, "out1");  /* amp + input pull-ups */
    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t cfg0 = 0, cfg1 = 0, out0 = 0, out1 = 0;
    k10_tca_read(K10_TCA_REG_CFG0, &cfg0);
    k10_tca_read(K10_TCA_REG_CFG1, &cfg1);
    k10_tca_read(K10_TCA_REG_OUT0, &out0);
    k10_tca_read(K10_TCA_REG_OUT1, &out1);
    k10_tca_read(K10_TCA_REG_IN0, &in0);
    k10_tca_read(K10_TCA_REG_IN1, &in1);
    ESP_LOGI(TAG, "readback CFG0=0x%02X CFG1=0x%02X OUT0=0x%02X OUT1=0x%02X IN0=0x%02X IN1=0x%02X",
             cfg0, cfg1, out0, out1, in0, in1);
    for (int i = 0; i < 4; i++) {
        k10_tca_read(K10_TCA_REG_IN0, &in0);
        k10_tca_read(K10_TCA_REG_IN1, &in1);
        ESP_LOGI(TAG, "IN sample %d: 0x%02X 0x%02X", i, in0, in1);
    }
    return ESP_OK;
}

esp_err_t k10_set_backlight(bool on)
{
    uint8_t out = 0;
    ESP_RETURN_ON_ERROR(k10_tca_read(K10_TCA_REG_OUT0, &out), TAG, "read out0");
    if (on) {
        out |= (1u << K10_BL_BIT);
    } else {
        out &= ~(1u << K10_BL_BIT);
    }
    return k10_tca_write(K10_TCA_REG_OUT0, out);
}

esp_err_t k10_set_amp(bool on)
{
    uint8_t out = 0;
    ESP_RETURN_ON_ERROR(k10_tca_read(K10_TCA_REG_OUT1, &out), TAG, "read out1");
    if (on) {
        out |= (1u << K10_AMP_BIT);
    } else {
        out &= ~(1u << K10_AMP_BIT);
    }
    return k10_tca_write(K10_TCA_REG_OUT1, out);
}

esp_err_t k10_read_inputs(uint8_t *p0, uint8_t *p1)
{
    ESP_RETURN_ON_ERROR(k10_tca_read(K10_TCA_REG_IN0, p0), TAG, "read in0");
    ESP_RETURN_ON_ERROR(k10_tca_read(K10_TCA_REG_IN1, p1), TAG, "read in1");
    return ESP_OK;
}
