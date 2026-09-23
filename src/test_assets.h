/*
 * test_assets.h —— ESGUI 测试工程的位图资源声明（自动生成，请勿手改）
 *
 * 生成脚本：tools/gen_test_assets.py
 */
#ifndef TEST_ASSETS_H
#define TEST_ASSETS_H

#include "ESGUI_Def.h"
#include "ESGUI_BSP_BMP.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 48x48 图标（BMP 菜单条目） ---- */
extern const Bitmap tst_icon_settings;
extern const Bitmap tst_icon_music;
extern const Bitmap tst_icon_folder;
extern const Bitmap tst_icon_heart;
extern const Bitmap tst_icon_star;
extern const Bitmap tst_icon_wifi;

/* ---- 40x40 缩略图标（BMP 列表弹窗条目） ---- */
extern const Bitmap tst_small_settings;
extern const Bitmap tst_small_music;
extern const Bitmap tst_small_folder;
extern const Bitmap tst_small_heart;
extern const Bitmap tst_small_star;
extern const Bitmap tst_small_wifi;

/* ---- 动图帧序列（GIF 条目 / 位图动画测试） ---- */
#define TST_GIF_FRAME_COUNT  12
extern const Bitmap tst_gif_frames[TST_GIF_FRAME_COUNT];
extern const eui_uint16_t tst_gif_delays[TST_GIF_FRAME_COUNT];

#ifdef __cplusplus
}
#endif
#endif /* TEST_ASSETS_H */
