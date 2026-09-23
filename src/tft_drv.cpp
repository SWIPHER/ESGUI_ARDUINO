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
#include <string.h>
#if TFT_USE_FRAME_BUF
#include <esp_heap_caps.h>
#endif

/* TFT_eSPI 全局实例（C 文件看不见它，所以屏相关操作都封装在本文件里） */
TFT_eSPI tft;

/* 主题：白字黑底（最像原来的单色 OLED Demo）。
 * 换主题举例：深蓝底青字 → { .fg = 0x07FF, .bg = 0x0010 } */
esgui_theme_t  g_theme  = { .fg = 0xFFFF, .bg = 0x0000 };
esgui_pal_cb_t g_pal_cb = NULL;

/* 条带缓冲最多按 32 逻辑行分配（即使 ESGUI_STRIP_H 写成整屏高，也不会把整屏缓冲塞进内部 RAM） */
#define TFT_STRIP_BUF_ROWS  ((ESGUI_STRIP_H) > 32 ? 32 : (ESGUI_STRIP_H))

/* 显示缓冲一行宽（已放大） */
#define TFT_DISP_ROW_W      (ESGUI_LOGIC_W * TFT_ZOOM)

/* RGB565 条带缓冲：最多 32 逻辑行 × 放大倍数（112x32 → 224x64 → 28672 B，内部 RAM 够用） */
static uint16_t s_strip[TFT_DISP_ROW_W * TFT_STRIP_BUF_ROWS * TFT_ZOOM];

#if TFT_USE_FRAME_BUF
/* 整帧缓冲：224x256 → 114688 B，放 PSRAM（ESP32-S3 的 PSRAM 支持 DMA） */
static uint16_t *s_frame = NULL;
#endif

/* ==================================================================
 * 1bpp 页式 → RGB565（按页展开 + 整数倍放大，最快路径）
 *   buf   : 条带显存，其第 0 页对应该条带的第 0 行（框架给的就是这个）
 *   w     : 逻辑宽（也是每页的字节数）
 *   pages : 本次要展开的页数
 *   pal   : pal[0]=背景色, pal[1]=前景色
 *   dst   : 目标 RGB565，行优先；第 p 页写到 dst 的第 p*8*Z .. (p*8+8)*Z-1 行
 * 放大方式：每个逻辑像素写成 Z x Z 的实心方块（整数倍 → 像素网格不糊、不抖）
 * ================================================================== */
static void expand_pages(const uint8_t *buf, int w, int pages,
                         const uint16_t pal[2], uint16_t *dst)
{
    const int z  = TFT_ZOOM;
    const int dw = w * z;                                  /* 放大后的行宽 */

    for (int p = 0; p < pages; p++) {
        const uint8_t *page = buf + (size_t)p * w;         /* 第 p 页的字节起点 */

        for (int n = 0; n < 8; n++) {                      /* 页内 8 行逐行展开 */
            uint16_t *out = dst + (size_t)(p * 8 + n) * z * dw;

            /* ① 横向：每个像素写 z 个连续像素 */
            for (int x = 0; x < w; x++) {
                uint16_t c = pal[(page[x] >> n) & 1];
                for (int k = 0; k < z; k++) {
                    out[x * z + k] = c;
                }
            }
            /* ② 纵向：这一行复制 z-1 份到下面几行 */
            for (int k = 1; k < z; k++) {
                memcpy(out + (size_t)k * dw, out, (size_t)dw * sizeof(uint16_t));
            }
        }
    }
}

/* ==================================================================
 * 兜底：逐行展开（只有"逻辑高不是 8 的倍数"时才会走到）
 *   y_base : 条带第 0 行在屏幕上的绝对 Y —— 算"条带内相对页号"必须用它
 *   y_start: 本块要展开的第一行的绝对 Y
 *   rows   : 本块行数；结果写到 dst 的第 0 行起（已含放大）
 * ================================================================== */
static void expand_rows(const uint8_t *buf, int w,
                        int y_base, int y_start, int rows,
                        const uint16_t pal[2], uint16_t *dst)
{
    const int z  = TFT_ZOOM;
    const int dw = w * z;

    for (int r = 0; r < rows; r++) {
        int y = y_start + r;
        const uint8_t *page = buf + (size_t)((y - y_base) >> 3) * w;   /* ★ 相对页号 */
        uint8_t mask = (uint8_t)(1u << (y & 7));
        uint16_t *out = dst + (size_t)r * z * dw;

        for (int x = 0; x < w; x++) {
            uint16_t c = pal[(page[x] & mask) ? 1 : 0];
            for (int k = 0; k < z; k++) {
                out[x * z + k] = c;
            }
        }
        for (int k = 1; k < z; k++) {
            memcpy(out + (size_t)k * dw, out, (size_t)dw * sizeof(uint16_t));
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
 * 显示自检：绕过 ESGUI 的 1bpp 画布，直接用 TFT_eSPI 写 RGB565
 * 目的：把"屏幕/接线/颜色/偏移"的问题和"框架绘制"的问题分开定位。
 * 注意：会阻塞 1~2 秒（期间不刷新界面），返回后由调用方请求重绘。
 * ================================================================== */
static const char *const s_selftest_names[] = {
    "纯色轮播(红绿蓝白黑)",
    "RGB基本色竖条(8色)",
    "RGB565三色渐变",
    "棋盘格+边框+四角(像素自检)",
};

int tft_drv_selftest_count(void)
{
    return (int)(sizeof(s_selftest_names) / sizeof(s_selftest_names[0]));
}

const char *tft_drv_selftest_name(int idx)
{
    if (idx < 0 || idx >= tft_drv_selftest_count()) return "";
    return s_selftest_names[idx];
}

void tft_drv_selftest_show(int idx)
{
    tft.startWrite();

    switch (idx) {
        case 0: {
            static const uint16_t cols[5] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000};
            for (int i = 0; i < 5; i++) {
                tft.fillScreen(cols[i]);
                tft.endWrite();
                delay(300);
                tft.startWrite();
            }
            break;
        }

        case 1: {
            static const uint16_t cols[8] = {0xF800, 0x07E0, 0x001F, 0xFFE0,
                                             0x07FF, 0xF81F, 0xFFFF, 0x0000};
            int w = TFT_SCREEN_W / 8;
            for (int i = 0; i < 8; i++) {
                int x = i * w;
                int ww = (i == 7) ? (TFT_SCREEN_W - x) : w;
                tft.fillRect(x, 0, ww, TFT_SCREEN_H, cols[i]);
            }
            break;
        }

        case 2: {
            /* 横向渐变：上半段 R→G、下半段 G→B，每行一个颜色 */
            int half = TFT_SCREEN_H / 2;
            for (int y = 0; y < TFT_SCREEN_H; y++) {
                uint8_t r = 0, g = 0, b = 0;
                if (y < half) {
                    r = (uint8_t)(255 - (255 * y) / (half > 0 ? half : 1));
                    g = (uint8_t)((255 * y) / (half > 0 ? half : 1));
                } else {
                    int t = y - half;
                    int span = TFT_SCREEN_H - half;
                    g = (uint8_t)(255 - (255 * t) / (span > 0 ? span : 1));
                    b = (uint8_t)((255 * t) / (span > 0 ? span : 1));
                }
                tft.drawFastHLine(0, y, TFT_SCREEN_W, tft.color565(r, g, b));
            }
            break;
        }

        case 3: {
            /* 棋盘格：每 8x8 一格 + 面板 1px 红框 + 四角红块 + 安全区绿框
             * 用途：① 看四角有没有被面板圆角切掉；② 绿框就是 ESGUI 逻辑区（内容都在框内） */
            tft.fillScreen(TFT_BLACK);
            for (int y = 0; y < TFT_SCREEN_H; y += 8) {
                for (int x = 0; x < TFT_SCREEN_W; x += 8) {
                    if (((x / 8) + (y / 8)) & 1) {
                        int ww = (x + 8 > TFT_SCREEN_W) ? (TFT_SCREEN_W - x) : 8;
                        int hh = (y + 8 > TFT_SCREEN_H) ? (TFT_SCREEN_H - y) : 8;
                        tft.fillRect(x, y, ww, hh, TFT_WHITE);
                    }
                }
            }
            /* 面板边框（红）+ 四角红块：判断圆角是否切到画面 */
            tft.drawRect(0, 0, TFT_SCREEN_W, TFT_SCREEN_H, TFT_RED);
            tft.fillRect(0, 0, 8, 8, TFT_RED);
            tft.fillRect(TFT_SCREEN_W - 8, 0, 8, 8, TFT_RED);
            tft.fillRect(0, TFT_SCREEN_H - 8, 8, 8, TFT_RED);
            tft.fillRect(TFT_SCREEN_W - 8, TFT_SCREEN_H - 8, 8, 8, TFT_RED);
            /* 安全区（ESGUI 逻辑区放大后的实际显示区域）：绿框 + 中心十字 */
            int vx = TFT_OFFSET_X;
            int vy = TFT_OFFSET_Y;
            int vw = ESGUI_LOGIC_W * TFT_ZOOM;
            int vh = ESGUI_LOGIC_H * TFT_ZOOM;
            tft.drawRect(vx, vy, vw, vh, TFT_GREEN);
            tft.drawFastVLine(TFT_SCREEN_W / 2, TFT_SCREEN_H / 2 - 10,
                              TFT_SCREEN_H / 2 + 10, TFT_GREEN);
            tft.drawFastHLine(TFT_SCREEN_W / 2 - 10, TFT_SCREEN_H / 2,
                              20, TFT_GREEN);
            break;
        }

        default:
            break;
    }

    tft.endWrite();
    delay(idx == 3 ? 1500 : 900);

    /* ★ 收尾：把**整块面板**（含逻辑区外的上下留边）清成底色。
     *   原因：ESGUI 只覆盖 TFT_OFFSET_Y ~ TFT_OFFSET_Y+ESGUI_LOGIC_H-1 这段，
     *   留边那几行框架永远不重绘；自检图案（红框/棋盘格）要是画到留边上就会永久残影。 */
    tft.fillScreen(g_theme.bg);
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
    size_t frame_bytes = (size_t)ESGUI_LOGIC_W * TFT_ZOOM * ESGUI_LOGIC_H * TFT_ZOOM * 2;
    s_frame = (uint16_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (s_frame == NULL) {       /* 没 PSRAM 就退到内部 RAM（可能失败） */
        s_frame = (uint16_t *)heap_caps_malloc(frame_bytes, MALLOC_CAP_DMA);
    }
#endif
}

/* ==================================================================
 * 面板安全区护栏：逻辑区（含放大）之外的留边框架永不覆盖，
 * 每条整帧刷新的第一条带里顺手用底色补一遍（上下留边 + 左右留边）。
 * 作用：任何"绕过框架直写屏幕"的代码（显示自检、自己调 TFT_eSPI 画画等）
 *       都不会在留边上留下永久残影（典型症状：顶部/底部出现彩色实线或虚线）。
 * 代价：每帧 4 次小 fillRect（共约 2KB 传输），可忽略。
 * ================================================================== */

/* 填充本体：调用方需已处于 SPI 事务中（startWrite/endWrite） */
static void cover_margins_raw(void)
{
    const int view_w = ESGUI_LOGIC_W * TFT_ZOOM;
    const int view_h = ESGUI_LOGIC_H * TFT_ZOOM;

    if (TFT_OFFSET_Y > 0) {                                     /* 上留边 */
        tft.fillRect(0, 0, TFT_SCREEN_W, TFT_OFFSET_Y, g_theme.bg);
    }
    if (TFT_OFFSET_X > 0) {                                     /* 左留边 */
        tft.fillRect(0, 0, TFT_OFFSET_X, TFT_SCREEN_H, g_theme.bg);
    }
    if (TFT_OFFSET_X + view_w < TFT_SCREEN_W) {                 /* 右留边 */
        tft.fillRect(TFT_OFFSET_X + view_w, 0,
                     TFT_SCREEN_W - TFT_OFFSET_X - view_w, TFT_SCREEN_H, g_theme.bg);
    }
    if (TFT_OFFSET_Y + view_h < TFT_SCREEN_H) {                 /* 下留边 */
        int y = TFT_OFFSET_Y + view_h;
        tft.fillRect(0, y, TFT_SCREEN_W, TFT_SCREEN_H - y, g_theme.bg);
    }
}

/* 自包含版本：自己开关 SPI 事务，供外部（尚未在事务中的代码）调用 */
void tft_drv_cover_margins(void)
{
    tft.startWrite();
    cover_margins_raw();
    tft.endWrite();
}

/* ==================================================================
 * 送屏：框架每刷新一个条带就调一次
 *
 * 参数含义（框架的定义，别自己改）：
 *   x0,y0,x1,y1 : 条带的屏幕区域，**y0/y1 已按 8 行对齐**；x0=0、x1=屏宽-1
 *   buf1bpp     : 条带显存；它的第 0 页 = 屏幕第 y0 行开始的那一页
 *
 * 三个必须记住的点：
 *   ① 取行用"条带内相对页号"（y - y0），不是绝对页号；
 *   ② y0/y1/rows 全是**逻辑行**；真正上屏的位置要乘 TFT_ZOOM；
 *   ③ 逻辑区超出面板可用显示区时必须钳位，否则 pushImage 越界。
 * ================================================================== */
void esgui_flush_area(int x0, int y0, int x1, int y1, const uint8_t *buf1bpp)
{
    (void)x0;
    (void)x1;                              /* 框架永远是整屏宽，用不到 */

    if (buf1bpp == NULL) return;

    int rows = y1 - y0 + 1;                /* 本段逻辑行数（推荐配置下必是 8 的倍数） */
    if (rows <= 0) return;

    /* 面板上还能放下多少逻辑行（放大后不越界）；正常配置下不会触发钳位 */
    const int max_rows = (TFT_SCREEN_H - TFT_OFFSET_Y) / TFT_ZOOM;
    if (y0 >= max_rows) return;

    uint16_t pal[2];

#if TFT_USE_FRAME_BUF
    /* 整帧路径：单条带整屏 + 没开"按行换色" → 展开一帧，一次 pushImage（事务最少、无撕裂） */
    if (s_frame != NULL && g_pal_cb == NULL && y0 == 0 && rows >= ESGUI_LOGIC_H) {
        pal[0] = g_theme.bg;
        pal[1] = g_theme.fg;
        expand_pages(buf1bpp, ESGUI_LOGIC_W, ESGUI_LOGIC_H >> 3, pal, s_frame);

        int h = rows;                      /* 面板比逻辑高小 → 钳位（逻辑行） */
        if (h > max_rows) h = max_rows;

        tft_drv_cover_margins();           /* 安全区护栏（此处尚未开事务，用自包含版本） */
        tft.startWrite();                  /* 一段连续写共用一个 SPI 事务 */
        tft.pushImage(TFT_OFFSET_X, TFT_OFFSET_Y,
                      ESGUI_LOGIC_W * TFT_ZOOM, h * TFT_ZOOM, s_frame);
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

            int y = y0 + (done << 3);      /* 逻辑行 */
            int h = n << 3;                /* 逻辑行数 */

            if (y >= max_rows) break;
            if (y + h > max_rows) h = max_rows - y;      /* ★钳位（逻辑行） */
            if (h <= 0) break;

            /* 配色：默认全屏一套，g_pal_cb 可按行区间换色 */
            if (g_pal_cb) {
                g_pal_cb(y, pal);
            } else {
                pal[0] = g_theme.bg;
                pal[1] = g_theme.fg;
            }

            expand_pages(buf1bpp + (size_t)done * ESGUI_LOGIC_W, ESGUI_LOGIC_W, n, pal, s_strip);

            if (y == 0) {
                /* 整帧的第一条带：顺手补一遍安全区外的留边（护栏），
                 * 避免别人直写屏幕后那些地方永久留着残影 */
                cover_margins_raw();
            }

            tft.pushImage(TFT_OFFSET_X, TFT_OFFSET_Y + y * TFT_ZOOM,
                          ESGUI_LOGIC_W * TFT_ZOOM, h * TFT_ZOOM, s_strip);
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
            if (y >= max_rows) break;
            if (y + n > max_rows) n = max_rows - y;
            if (n <= 0) break;

            if (g_pal_cb) {
                g_pal_cb(y, pal);
            } else {
                pal[0] = g_theme.bg;
                pal[1] = g_theme.fg;
            }

            expand_rows(buf1bpp, ESGUI_LOGIC_W, y0, y0 + done, n, pal, s_strip);
            if (y == 0) {
                cover_margins_raw();       /* 安全区护栏（同快速路径） */
            }
            tft.pushImage(TFT_OFFSET_X, TFT_OFFSET_Y + y * TFT_ZOOM,
                          ESGUI_LOGIC_W * TFT_ZOOM, n * TFT_ZOOM, s_strip);
            done += n;
        }
    }

    tft.endWrite();
}