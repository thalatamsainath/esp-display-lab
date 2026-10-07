// Reference ESP12F / ST7789 240x240 configuration.
// Adapted from slavka08/esp-mini_screen; see ATTRIBUTION.md.
// The build script supplies these per build; no global library edit is needed.
#define ST7789_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 240
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS -1
#define TFT_DC 0
#define TFT_RST 2
#define TFT_BL 5
#define TFT_BACKLIGHT_ON LOW
#define LOAD_GLCD
#define SMOOTH_FONT
#define SPI_FREQUENCY 20000000
