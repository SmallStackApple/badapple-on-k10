#pragma once

/*
 * UNIHIKER K10 pin map.
 *
 * Sources:
 *   - LCD:  K10 SDK tools/sdk/esp32s3/include/modules/lcd/who_lcd.h
 *           and framework TFT_eSPI User_Setup.h
 *   - I2S:  framework libraries/unihiker_k10/src/unihiker_k10.h
 *   - I2C:  framework variants/unihiker_k10/pins_arduino.h
 *   - TCA9555: K10 schematic + DFRobot community port notes
 *
 * The TCA9555 values below were verified on hardware:
 *   A = P1.4, B = P0.2 (active low); backlight = P0.0; amp = P1.7.
 * Input pins also need their output latch set to 1 (OUT0/OUT1 = 0xFF)
 * to enable the weak pull-ups.
 */

/* ---- LCD (ILI9341, SPI2) ---- */
#define K10_LCD_HOST            SPI2_HOST
#define K10_LCD_MOSI            21
#define K10_LCD_MISO            (-1)
#define K10_LCD_SCLK            12
#define K10_LCD_CS              14
#define K10_LCD_DC              13
#define K10_LCD_RST             (-1)   /* not connected */
#define K10_LCD_H_RES           320
#define K10_LCD_V_RES           240
#define K10_LCD_PIXEL_CLOCK_HZ  (40 * 1000 * 1000)

/* ---- I2S (NS4168 speaker amp) ---- */
#define K10_I2S_MCLK            3
#define K10_I2S_BCLK            0
#define K10_I2S_WS              38
#define K10_I2S_DOUT            45
#define K10_I2S_DIN             39     /* microphone, unused here */

/* ---- I2C (TCA9555 + sensors) ---- */
#define K10_I2C_SDA             47
#define K10_I2C_SCL             48
#define K10_I2C_FREQ_HZ         100000
#define K10_TCA9555_ADDR        0x20

/* ---- TCA9555 bits (documented, verified at runtime) ---- */
#define K10_TCA_REG_IN0         0x00
#define K10_TCA_REG_IN1         0x01
#define K10_TCA_REG_OUT0        0x02
#define K10_TCA_REG_OUT1        0x03
#define K10_TCA_REG_CFG0        0x06
#define K10_TCA_REG_CFG1        0x07

/* Port 0: P0.0 = LCD backlight, P0.1 = camera reset, P0.2 = Button B */
#define K10_BL_PORT             0
#define K10_BL_BIT              0
#define K10_CAM_RST_PORT        0
#define K10_CAM_RST_BIT         1
#define K10_BTN_B_PORT          0
#define K10_BTN_B_BIT           2

/* Port 1: P1.4 = Button A, P1.7 = speaker amp enable */
#define K10_BTN_A_PORT          1
#define K10_BTN_A_BIT           4
#define K10_AMP_PORT            1
#define K10_AMP_BIT             7

/* ---- RGB LED (WS2812 x3), unused in the player ---- */
#define K10_RGB_GPIO            46

/* ---- Display window for the 304x228 video ---- */
#define K10_VIDEO_W             304
#define K10_VIDEO_H             228
#define K10_VIDEO_X             ((K10_LCD_H_RES - K10_VIDEO_W) / 2)
#define K10_VIDEO_Y             ((K10_LCD_V_RES - K10_VIDEO_H) / 2)
