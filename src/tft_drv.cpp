/*
 * tft_drv.cpp —— ESGUI(1bpp 页式) → RGB565 → TFT_eSPI 送屏
 *
 * 数据流：
 *   ESGUI 画布条带(buf[page*宽度 + x]，bit n = 像素(x, page*8+n))
 *        └─ expand_pages() 展开(读 1 字节写 8 行) ─→ uint16_t RGB565 行优先
 *              └─ tft.pushImage(0, y, 屏宽, 行数, 缓冲) ─→ 面板
 *
 * 一个"页" = 8 行 × 屏宽 字节；一个字节的 8 个 bit 正好是同一列的 8 行，
 * 所以按页展开时"读一次字节 → 写 8 行"，内存访问最省。
 */
#include "tft_drv.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#if TFT_USE_FRAME_BUF
#include <esp_heap_caps.h>
#endif

/* TFT_eSPI 全局实例（C 文件看不见它，所以屏相关操作都封装在本文件里） */
TFT_eSPI tft;

/* 主题：白字黑底（最像原来的单色 OLED Demo）。
 * 换主题举例：深蓝底青字 → { .fg = 0x07FF, .bg = 0x0010 } */
esgui_theme_t  g_theme  = { .fg = 0xFFFF, .bg = 0x0000 };
esgui_pal_cb_t g_pal_cb = NULL;

/* 条带缓冲最多按 32 行分配（即使 ESGUI_STRIP_H 写成整屏高，也不会把整屏缓冲塞进内部 RAM） */
#define TFT_STRIP_BUF_ROWS  ((ESGUI_STRIP_H) > 32 ? 32 : (ESGUI_STRIP_H))

/* RGB565 条带缓冲：最多 32 行 × 屏宽
 * 240x32 → 15360 B，放内部 RAM（DMA 可达，速度快） */
static uint16_t s_strip[ESGUI_LOGIC_W * TFT_STRIP_BUF_ROWS];

#if TFT_USE_FRAME_BUF
/* 整帧缓冲：240x280 → 134400 B，放 PSRAM（ESP32-S3 的 PSRAM 支持 DMA） */
static uint16_t *s_frame = NULL;
#endif

/* ==================================================================
 * 1bpp 页式 → RGB565（按页展开，最快路径）
 *   buf   : 条带显存，其第 0 页对应该条带的第 0 行（框架给的就是这个）
 *   w     : 屏宽（也是每页的字节数）
 *   pages : 本次要展开的页数
 *   pal   : pal[0]=背景色, pal[1]=前景色
 *   dst   : 目标 RGB565，行优先连续；第 p 页写到 dst 的第 p*8 .. p*8+7 行
 * ================================================================== */
static void expand_pages(const uint8_t *buf, int w, int pages,
                         const uint16_t pal[2], uint16_t *dst)
{
    for (int p = 0; p < pages; p++) {
        const uint8_t *page = buf + (size_t)p * w;        /* 第 p 页的字节起点 */

        uint16_t *r0 = dst + (size_t)p * 8 * w;           /* 8 个行指针 */
        uint16_t *r1 = r0 + w;
        uint16_t *r2 = r1 + w;
        uint16_t *r3 = r2 + w;
        uint16_t *r4 = r3 + w;
        uint16_t *r5 = r4 + w;
        uint16_t *r6 = r5 + w;
        uint16_t *r7 = r6 + w;

        for (int x = 0; x < w; x++) {
            uint8_t v = page[x];
            r0[x] = pal[(v >> 0) & 1];
            r1[x] = pal[(v >> 1) & 1];
            r2[x] = pal[(v >> 2) & 1];
            r3[x] = pal[(v >> 3) & 1];
            r4[x] = pal[(v >> 4) & 1];
            r5[x] = pal[(v >> 5) & 1];
            r6[x] = pal[(v >> 6) & 1];
            r7[x] = pal[(v >> 7) & 1];
        }
    }
}

/* ==================================================================
 * 兜底：逐行展开（只有"逻辑高不是 8 的倍数"时才会走到）
 *   y_base : 条带第 0 行在屏幕上的绝对 Y —— 算"条带内相对页号"必须用它
 *   y_start: 本块要展开的第一行的绝对 Y
 *   rows   : 本块行数；结果写到 dst 的第 0 行起
 * ================================================================== */
static void expand_rows(const uint8_t *buf, int w,
                        int y_base, int y_start, int rows,
                        const uint16_t pal[2], uint16_t *dst)
{
    for (int r = 0; r < rows; r++) {
        int y = y_start + r;
        const uint8_t *page = buf + (size_t)((y - y_base) >> 3) * w;   /* ★ 相对页号 */
        uint8_t mask = (uint8_t)(1u << (y & 7));
        uint16_t *out = dst + (size_t)r * w;
        for (int x = 0; x < w; x++) {
            out[x] = pal[(page[x] & mask) ? 1 : 0];
        }
    }
}
/* ================================================================== */
/* 背光（模块有 TFT_BL 就点亮点；没有就空函数） */
/* ================================================================== */
void tft_drv_backlight(int on)
{
    /* 需要在 tft_setup.h 里定义 TFT_BL（有背光脚的模块） */
#if defined(TFT_BL)
    pinMode(TFT_BL, OUTPUT);
  #if defined(TFT_BACKLIGHT_ON)
    digitalWrite(TFT_BL, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
  #else
    digitalWrite(TFT_BL, on ? HIGH : LOW);
  #endif
#else
    (void)on;
#endif
}

/* ==================================================================
 * 屏幕初始化：setup() 里调一次（由 esgui_port_init 调用）
 * ================================================================== */
void tft_drv_init(void)
{
    tft_drv_backlight(1);        /* 先点亮背光：全黑时能立刻区分"背光问题"还是"驱动问题" */

    tft.init();                  /* 驱动型号/引脚/频率全部来自 User_Setup.h，不用传参 */
    tft.setRotation(0);          /* 本屏 240x240（方形）：rotation 0 与面板/触摸坐标 1:1；
                                  * 不要随意改成 1/3 —— 那会套用 240x320 的偏移，画面整体跑偏 */
    tft.setSwapBytes(true);      /* pushImage 字节序：颜色不对，第一个就翻这个开关 */
    tft.fillScreen(g_theme.bg);  /* 先清屏：既证明驱动通了，也避免上电花屏 */

#if TFT_USE_FRAME_BUF
    s_frame = (uint16_t *)heap_caps_malloc((size_t)ESGUI_LOGIC_W * ESGUI_LOGIC_H * 2,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (s_frame == NULL) {       /* 没 PSRAM 就退到内部 RAM（可能失败） */
        s_frame = (uint16_t *)heap_caps_malloc((size_t)ESGUI_LOGIC_W * ESGUI_LOGIC_H * 2,
                                               MALLOC_CAP_DMA);
    }
#endif
}

/* ==================================================================
 * 送屏：框架每刷新一个条带就调一次
 *
 * 参数含义（框架的定义，别自己改）：
 *   x0,y0,x1,y1 : 条带的屏幕区域，**y0/y1 已按 8 行对齐**；x0=0、x1=屏宽-1
 *   buf1bpp     : 条带显存；它的第 0 页 = 屏幕第 y0 行开始的那一页
 *
 * 两个必须记住的点：
 *   ① 取行用"条带内相对页号"（y - y0），不是绝对页号；
 *   ② 逻辑高 > 面板高（方案B）时必须把行数钳位到面板高度，否则 pushImage 越界。
 * ================================================================== */
void esgui_flush_area(int x0, int y0, int x1, int y1, const uint8_t *buf1bpp)
{
    (void)x0;
    (void)x1;                              /* 框架永远是整屏宽，用不到 */

    if (buf1bpp == NULL) return;

    int rows = y1 - y0 + 1;                /* 本段行数（推荐配置下必是 8 的倍数） */
    if (rows <= 0 || y0 >= TFT_SCREEN_H) return;

    uint16_t pal[2];

#if TFT_USE_FRAME_BUF
    /* 整帧路径：单条带整屏 + 没开"按行换色" → 展开一帧，一次 pushImage（事务最少、无撕裂） */
    if (s_frame != NULL && g_pal_cb == NULL && y0 == 0 && rows >= ESGUI_LOGIC_H) {
        pal[0] = g_theme.bg;
        pal[1] = g_theme.fg;
        expand_pages(buf1bpp, ESGUI_LOGIC_W, ESGUI_LOGIC_H >> 3, pal, s_frame);

        int h = rows;                      /* 面板比逻辑高小 → 钳位 */
        if (TFT_OFFSET_Y + h > TFT_SCREEN_H) h = TFT_SCREEN_H - TFT_OFFSET_Y;

        tft.startWrite();                  /* 一段连续写共用一个 SPI 事务 */
        tft.pushImage(TFT_OFFSET_X, TFT_OFFSET_Y, ESGUI_LOGIC_W, h, s_frame);
        tft.endWrite();
        return;
    }
#endif

    tft.startWrite();

    if ((rows & 7) == 0) {
        /* ------------ 快速路径：按页展开（正常情况永远走这条） ------------ */
        int pages       = rows >> 3;
        int pages_chunk = TFT_STRIP_BUF_ROWS >> 3;  /* 每次最多展开几页，由缓冲大小决定 */
        if (pages_chunk < 1) pages_chunk = 1;

        for (int done = 0; done < pages; ) {
            int n = pages - done;
            if (n > pages_chunk) n = pages_chunk;

            int y = y0 + (done << 3);
            int h = n << 3;

            /* 配色：默认全屏一套，g_pal_cb 可按行区间换色 */
            if (g_pal_cb) {
                g_pal_cb(y, pal);
            } else {
                pal[0] = g_theme.bg;
                pal[1] = g_theme.fg;
            }

            expand_pages(buf1bpp + (size_t)done * ESGUI_LOGIC_W, ESGUI_LOGIC_W, n, pal, s_strip);

            if (y >= TFT_SCREEN_H) break;
            if (TFT_OFFSET_Y + y + h > TFT_SCREEN_H) h = TFT_SCREEN_H - TFT_OFFSET_Y - y;  /* ★钳位 */
            if (h <= 0) break;

            tft.pushImage(TFT_OFFSET_X, TFT_OFFSET_Y + y, ESGUI_LOGIC_W, h, s_strip);
            done += n;
        }
    } else {
        /* ------------ 兜底路径：逐行展开（逻辑高不是 8 的倍数时才走） ------------ */
        int chunk = TFT_STRIP_BUF_ROWS;
        if (chunk < 1) chunk = 1;

        for (int done = 0; done < rows; ) {
            int n = rows - done;
            if (n > chunk) n = chunk;

            int y = y0 + done;
            if (y >= TFT_SCREEN_H) break;
            if (TFT_OFFSET_Y + y + n > TFT_SCREEN_H) n = TFT_SCREEN_H - TFT_OFFSET_Y - y;
            if (n <= 0) break;

            if (g_pal_cb) {
                g_pal_cb(y, pal);
            } else {
                pal[0] = g_theme.bg;
                pal[1] = g_theme.fg;
            }

            expand_rows(buf1bpp, ESGUI_LOGIC_W, y0, y0 + done, n, pal, s_strip);
            tft.pushImage(TFT_OFFSET_X, TFT_OFFSET_Y + y, ESGUI_LOGIC_W, n, s_strip);
            done += n;
        }
    }

    tft.endWrite();
}