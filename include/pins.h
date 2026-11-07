#pragma once
// ---------------------------------------------------------------------------
// Pin map for the 4.0" "CYD" board: ESP32-32E + ST7796 320x480 + XPT2046
// (LCDWiki E32R40T / E32N40T layout). If something doesn't work on your
// board revision, this is the only file you should need to touch.
// ---------------------------------------------------------------------------

// LCD (ST7796) on HSPI - shared with the touch controller
#define PIN_LCD_SCLK   14
#define PIN_LCD_MOSI   13
#define PIN_LCD_MISO   12
#define PIN_LCD_CS     15
#define PIN_LCD_DC      2
#define PIN_LCD_RST    -1   // tied to EN
#define PIN_LCD_BL     27   // backlight, active high (PWM)

// Resistive touch (XPT2046) - same SPI bus as LCD
#define PIN_TOUCH_CS   33
#define PIN_TOUCH_IRQ  36

// micro-SD on VSPI (also brought out on the "SPI" header: 23/19/18/21)
#define PIN_SD_SCLK    18
#define PIN_SD_MISO    19
#define PIN_SD_MOSI    23
#define PIN_SD_CS       5

// RGB LED (common anode -> LOW = on)
#define PIN_LED_R      22
#define PIN_LED_G      16
#define PIN_LED_B      17
#define LED_ACTIVE_LOW  1

// Audio: internal DAC channel 2 (GPIO26) -> on-board amp -> "SPEAKER" connector
#define PIN_AUDIO_DAC  26
#define PIN_AUDIO_EN    4   // amplifier enable
#define AUDIO_EN_LEVEL LOW  // level that turns the amp ON

// Expansion headers (left free for you): I2C 32(SDA)/25(SCL), inputs 35/39
#define PIN_I2C_SDA    32
#define PIN_I2C_SCL    25

// Boot button (usable as a "home" key once running)
#define PIN_BOOT_BTN    0

// Panel colour quirks. Both can also be toggled at runtime in Settings.
#define LCD_INVERT_DEFAULT  false
#define LCD_BGR_DEFAULT     false
