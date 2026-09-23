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
 *   所以本文件不依赖 TFT_eSPI 的补偿，改用 TFT_OFFSET_X/Y 自己把画面摆到正确位置。
 */
#define TFT_SCREEN_W   240
#define TFT_SCREEN_H   284

/* =================== ② UI 放大倍数 + 逻辑分辨率 + 安全区居中 ===================
 * 这块屏是"圆角面板"：四角有圆弧，贴边的文字会被圆角切掉。所以不要铺满整屏，
 * 而是留一圈安全边距，把逻辑区居中放进去（本文件就是干这个的）。
 *
 * 做法（比换大字号字库更省事、更统一）：
 *   ① ESGUI 逻辑区缩小到 112x128（仍是 8 的倍数，画布按 8 行一页组织）；
 *   ② 驱动把每个逻辑像素扩成 2x2 的方块送出屏 → 字与图形**整体放大 2 倍**；
 *   ③ 224x256 的显示区在 240x284 面板上水平居中、垂直居中 → 四周留出安全边距，
 *      圆角怎么切都切不到内容（上下各留 14 行，左右各留 8 列）。
 *
 * 想让字更大/更小：只改 TFT_ZOOM 并同步调整 ESGUI_LOGIC_W/H，使「逻辑尺寸 × 缩放」
 * 在面板内且尽量大（保持 8 的倍数）：
 *   TFT_ZOOM=1 → 逻辑 224x264（最小字，一屏能看到的内容最多）
 *   TFT_ZOOM=2 → 逻辑 112x128（当前：字放大 2 倍，触控目标 36 行高，好点）
 *   TFT_ZOOM=3 → 逻辑  72x 88（字超大，一屏只剩 4~5 行）
 */
#define TFT_ZOOM       2            /* 整数放大倍数（1/2/3） */

#define ESGUI_LOGIC_W  112          /* ← 逻辑宽（必须 8 的倍数）*/
#define ESGUI_LOGIC_H  128          /* ← 逻辑高（必须 8 的倍数）*/

/* 画面在面板上的起点 = 把 ESGUI_LOGIC_W*TFT_ZOOM x ESGUI_LOGIC_H*TFT_ZOOM 居中 */
#define TFT_OFFSET_X   ((TFT_SCREEN_W - ESGUI_LOGIC_W * TFT_ZOOM) / 2)
#define TFT_OFFSET_Y   ((TFT_SCREEN_H - ESGUI_LOGIC_H * TFT_ZOOM) / 2)

#if (ESGUI_LOGIC_H % 8) != 0 || (ESGUI_LOGIC_W % 8) != 0
  #error "ESGUI_LOGIC_W/H 必须是 8 的倍数（框架画布是按 8 行一页组织的）"
#endif

#if (TFT_ZOOM < 1) || (TFT_ZOOM > 3)
  #error "TFT_ZOOM 只支持 1~3（整数倍放大，非整数会破坏像素网格）"
#endif

#if (ESGUI_LOGIC_W * TFT_ZOOM) > TFT_SCREEN_W || (ESGUI_LOGIC_H * TFT_ZOOM) > TFT_SCREEN_H
  #error "逻辑分辨率 x TFT_ZOOM 超出面板：请调小 ESGUI_LOGIC_W/H 或 TFT_ZOOM"
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

/* 把逻辑区之外的"面板留边"（上下各 TFT_OFFSET_Y 行）刷成底色。
 * 框架永远不画这几行，所以任何绕过框架直写屏幕的代码收尾时调它一下就不会留残影；
 * 正常刷新时 esgui_flush_area 每帧也会自动补一次（护栏）。 */
void tft_drv_cover_margins(void);

/* =================== 显示自检（绕过 1bpp 画布，直接写 RGB565） ===================
 * 用途：验证"屏幕 + 接线 + 像素偏移"本身没问题（ESGUI 画错时用来分清是驱动还是框架）。
 * 调用会**阻塞** 1~2 秒（连续写几屏图案），期间不刷新 ESGUI；返回后请 ACT_REFRESH 重绘。
 * 结束时会把整块面板清成底色，所以不会在"逻辑区外的留边"上留下残影。
 * 图案清单（idx 从 0 开始）：
 *   0 纯色轮播（红/绿/蓝/白/黑）—— 检查三原色与亮暗
 *   1 RGB 基本色竖条（8 色）    —— 检查颜色映射（R/B 反了就是 RGB/BGR 问题）
 *   2 RGB565 三色渐变（R→G→B）  —— 检查色深/渐变是否有台阶断层
 *   3 棋盘格 + 1px 红边框 + 四角 —— 检查像素对齐、有无行列偏移（配 TFT_OFFSET_X/Y）
 *      · 屏幕顶/底各一条红实线 + 白黑相间的 8px 方格（看上去像白虚线）= 本图案的正常样子
 */
int         tft_drv_selftest_count(void);
const char *tft_drv_selftest_name(int idx);
void        tft_drv_selftest_show(int idx);


#ifdef __cplusplus
}
#endif
#endif /* TFT_DRV_H */