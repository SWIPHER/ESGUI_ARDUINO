/*
 * tft_drv.h —— TFT_eSPI 送屏驱动对外接口
 *
 * 这个头文件对 C / C++ 都友好（带 extern "C" 保护）：
 *   - C 文件（esgui_port.c）可以放心 include 并调用；
 *   - 实现在 tft_drv.cpp（因为 TFT_eSPI 是 C++ 类）。
 */
#ifndef TFT_DRV_H
#define TFT_DRV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =================== ① 面板真实分辨率 ===================
 * 必须与 TFT_eSPI 的 User_Setup.h 里 TFT_WIDTH/TFT_HEIGHT、以及 tft.setRotation(0) 之后一致。
 * 本板（Waveshare ESP32-S3-Touch-LCD-1.83）面板 240 x 284。
 *
 * ★ TFT_eSPI 的一个坑（改这块屏时必看）：
 *   TFT_Drivers/ST7789_Defines.h 的偏移表只认 240x240 / 240x280 / 240x300 / 240x320，
 *   写成 284 一条都不命中 → ST7789_Rotation.h 里整块 #ifdef CGRAM_OFFSET（行偏移补偿）
 *   不生效（rowstart 恒为 0），而且 MADCTL 会取 RGB 而不是 BGR。
 *   所以本文件不依赖 TFT_eSPI 的补偿，改用 TFT_OFFSET_Y 自己把画面摆到正确行：
 *     TFT_OFFSET_Y = 面板可见区在 GRAM 里的起始行 + 上下留边
 *   校准办法：用 TFT_eSPI 直绘一屏"1px 边框 + 四角 8x8 白块"（见 README/历史提交里的自检代码），
 *   看四角白块是否正好落在屏幕四角、上下是否对称；若整体偏移，把偏移行数补进 TFT_OFFSET_Y
 *   （例如可见区从 GRAM 第 20 行开始 → 20 + 2 = 22）。
 */
#define TFT_SCREEN_W   240
#define TFT_SCREEN_H   284

/* 画面在面板上的起点（逻辑分辨率比面板小时用来居中 / 留边）
 * 本板 284 - 280 = 4 → 上下各留 2 行，画面居中，观感最好。 */
#define TFT_OFFSET_X   0
#define TFT_OFFSET_Y   2

/* =================== ② ESGUI 逻辑分辨率（必须是 8 的倍数！） ===================
 * 284 不是 8 的倍数（284 = 8*35 + 4），所以逻辑高只能二选一：
 *   方案A（推荐、简单）：向下取整 280 → 面板上下各留 2 行（配合 TFT_OFFSET_Y=2 即居中）
 *   方案B（多挤 4 行）：向上取整 288 → 送屏时按面板高度钳位，实际仍只显示 284 行
 * 两种方案的"行数钳位"代码 §1.4 里都已经写好了，选哪个都不会越界。
 */
#define ESGUI_LOGIC_W  TFT_SCREEN_W
#define ESGUI_LOGIC_H  280          /* ← 方案A（向下取整到 8）；想用方案B就写 288 */

#if (ESGUI_LOGIC_H % 8) != 0
  #error "ESGUI_LOGIC_H 必须是 8 的倍数（框架画布是按 8 行一页组织的）"
#endif

/* =================== ③ 条带高（8 的倍数） ===================
 * 8  = 最省 RAM（240x8x2  = 3840B）
 * 32 = 推荐（240x32x2 = 15360B；240 = 32*7 + 16 → 最后一条 16 行，框架/驱动都会正确处理）
 * 30 / 40 / 48 = 能整除 240（条数正好 8 / 6 / 5），想省一次 SPI 事务可以选它们
 * ESGUI_LOGIC_H = 整屏单条带（一次 push，配合 TFT_USE_FRAME_BUF=1 最佳）
 */
#define ESGUI_STRIP_H  32

/* =================== ④ 是否用整帧 PSRAM 缓冲（动画不撕裂） ===================
 * 1 = 在 PSRAM 申请一整帧 RGB565（240x240 = 115200B），整帧一次 pushImage；
 *     请同时把上面的 ESGUI_STRIP_H 改成 ESGUI_LOGIC_H（整屏单条带）。
 *     前提：板子有 PSRAM（Arduino IDE 里 PSRAM 要选 OPI PSRAM / Enabled）。
 * 0 = 只用内部 RAM 的条带缓冲（省 RAM，够用，推荐先这样跑通）。
 */
#ifndef TFT_USE_FRAME_BUF
#define TFT_USE_FRAME_BUF 0
#endif

/* =================== ⑤ 配色（RGB565） ===================
 * ESGUI 画布是 1bpp：bit=1 的像素画成 fg，bit=0 的画成 bg。
 * 焦点框 / 反白文字走的是 XOR（位取反）→ 展开后自动变成 fg/bg 互换，
 * 不需要额外代码，这就是"选中高亮"。
 * 颜色可用 tft.color565(r,g,b) 算出来填进来。
 */
typedef struct {
    uint16_t fg;   /* 前景色（文字 / 图形） */
    uint16_t bg;   /* 背景色 */
} esgui_theme_t;

/* 进阶：按行换配色（可为 NULL）。返回时 pal[0] = 背景色，pal[1] = 前景色。
 * 用途举例：顶部 24 行做"彩色状态栏"（蓝底白字），其余区域黑底白字。 */
typedef void (*esgui_pal_cb_t)(int y, uint16_t pal[2]);

extern esgui_theme_t  g_theme;
extern esgui_pal_cb_t g_pal_cb;

/* =================== 接口 ===================
 * tft_drv_init()      : 在 esgui_port_init() 第一行调用（内部 tft.init + 清屏）
 * tft_drv_backlight() : 可选，背光开/关
 * esgui_flush_area()  : 由 esgui_port.c 里的 ESGUI_UseCanvasFlush() 转发进来
 */
void tft_drv_init(void);
void tft_drv_backlight(int on);
void esgui_flush_area(int x0, int y0, int x1, int y1, const uint8_t *buf1bpp);

#ifdef __cplusplus
}
#endif
#endif /* TFT_DRV_H */