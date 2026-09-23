#pragma once

// ============================================================
// Waveshare ESP32-S3-Touch-LCD-1.83
// ST7789P
// 240 x 284
// ============================================================

#define ST7789_DRIVER

// ============================================================
// ESP32-S3 SPI
// ============================================================

#define USE_HSPI_PORT

#define TFT_MOSI 7
#define TFT_SCLK 6

#define TFT_CS   5
#define TFT_DC   4
#define TFT_RST  38

// ============================================================
// LCD Backlight
// ============================================================

#define TFT_BL   40
#define TFT_BACKLIGHT_ON HIGH

// ============================================================
// Display
// ============================================================

#define TFT_WIDTH  240
#define TFT_HEIGHT 284

/* 颜色位序：本屏需要 BGR（否则红蓝互换，实测已验证）。
 * TFT_eSPI 的判定逻辑是：#if (TFT_RGB_ORDER == 1) → RGB，否则 → BGR
 * （见 TFT_Drivers/ST7789_Defines.h）。因为 TFT_HEIGHT=284 命不中那边的分辨率偏移表，
 * CGRAM_OFFSET 未定义 → 默认会给成 RGB，所以这里显式写成 0（= BGR）纠正过来。
 * 依赖 setSwapBytes(true) 只解决字节序，管不了红蓝互换。 */
#define TFT_RGB_ORDER 0

// ============================================================
// SPI
// ============================================================

#define SPI_FREQUENCY 80000000

// ============================================================
// Fonts
// ============================================================

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT