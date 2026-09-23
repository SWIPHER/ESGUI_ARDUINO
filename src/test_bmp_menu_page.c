/*
 * test_bmp_menu_page.c —— 图形（BMP）菜单 + 图片列表弹窗测试页
 *
 * 覆盖：
 *   · BMP 菜单：图标横向排列、焦点居中滑动动画、焦点框生长动画、顶部进度条、
 *               底部标签随焦点跳动、GIF 动图条目（选中即播放，未选显示第 0 帧）
 *   · 图片列表弹窗：普通版 / 滚动标题版 / 内含 GIF 条目版
 *
 * 图标数据来自 src/test_assets.c（由 tools/gen_test_assets.py 生成）。
 */
#include "test_bmp_menu_page.h"

#include <stdint.h>
#include <stdio.h>

#include "ESGUI.h"
#include "ESGUI_PageDefaltVtbl.h"
#include "ESGUI_GIF.h"
#include "test_assets.h"

/* ==================== BMP 菜单 ==================== */
static ESGUI_MenuPage_T  bmp_page;
static ESGUI_GIF_T       bmp_gif;           /* 动图条目：icon 指向它 */
static ESGUI_PopWindow_T bmp_msg_popup;

static ESGUI_MenuAction_T bmp_show_msg(const char *msg)
{
    /* 弹窗只保存消息**指针**，不拷贝文本 → 必须传常驻字符串（字面量/静态缓冲） */
    ESGUI_DefaultMessagePopWindowCreate(&bmp_msg_popup, msg, 216, 110, 1);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &bmp_msg_popup};
}

/* 图标条目：点击弹窗把 arg 里的名字显示出来（确认"焦点 == 点击项"没有错位）。
 * ★ 消息文本要放在**静态**缓冲里：弹窗不拷贝字符串，栈上局部数组在回调返回后就失效。 */
static char bmp_msg_buf[40];

static ESGUI_MenuAction_T bmp_item_enter(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    snprintf(bmp_msg_buf, sizeof(bmp_msg_buf), "选中图标\n%s", (const char *)arg);
    return bmp_show_msg(bmp_msg_buf);
}

static ESGUI_MenuAction_T bmp_enter_list_popup(ESGUI_MenuPage_T *page, void *arg);
static ESGUI_MenuAction_T bmp_enter_gif_list_popup(ESGUI_MenuPage_T *page, void *arg);

/* 条目表：5 个图标条目（其中 1 个动图，label 末尾 "\x03/7" 标记为 GIF）
 *      + 3 个"图片列表弹窗"入口（BMP 菜单只画位图，所以也给了图标，
 *        焦点落在它们上时底部标签会显示名字，不会认错）
 * 逻辑屏 216 宽 ≈ 7 个汉字；图标 48x48，一屏能看到 3~4 个（横向轮播）*/
static ESGUI_MenuItem_T bmp_items[] = {
    {0, 0, "设置",       &tst_icon_settings, bmp_item_enter, (void *)"设置(齿轮)"},
    {0, 0, "音乐",       &tst_icon_music,    bmp_item_enter, (void *)"音乐(音符)"},
    {0, 0, "文件夹",     &tst_icon_folder,   bmp_item_enter, (void *)"文件夹"},
    {0, 0, "心形",       &tst_icon_heart,    bmp_item_enter, (void *)"心形"},
    {0, 0, "动图\x03/7", &bmp_gif,           bmp_item_enter, (void *)"动图(12帧)"},
    {0, 0, "列表弹窗",   &tst_icon_star,     bmp_enter_list_popup,     (void *)0},
    {0, 0, "滚动标题",   &tst_icon_settings, bmp_enter_list_popup,     (void *)1},
    {0, 0, "列表动图",   &tst_icon_wifi,     bmp_enter_gif_list_popup, (void *)0},
};


ESGUI_MenuAction_T test_bmp_menu_page_create(void)
{
    /* 动图描述符：帧数组 + 每帧间隔（数组在 Flash，播放状态只有十几字节 RAM） */
    ESGUI_GIFInit(&bmp_gif, tst_gif_frames, TST_GIF_FRAME_COUNT,
                  tst_gif_delays, 0, 0);

    ESGUI_DefaultBMPMenuCreate(&bmp_page, "图形菜单", bmp_items,
                               ESGUI_ITEM_NUM_COUNT(bmp_items));
    bmp_page.focus_idx = 0;
    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &bmp_page};
}

/* ==================== 图片列表弹窗（BMP List Popup） ==================== */
static ESGUI_PopWindow_T bmp_list_popup;
static ESGUI_GIF_T       bmp_list_gif;

static ESGUI_MenuItem_T bmp_list_items[] = {
    {0, 0, "设置",   &tst_small_settings, ESGUI_NULL, ESGUI_NULL},
    {0, 0, "音乐",   &tst_small_music,    ESGUI_NULL, ESGUI_NULL},
    {0, 0, "文件夹", &tst_small_folder,   ESGUI_NULL, ESGUI_NULL},
    {0, 0, "心形",   &tst_small_heart,    ESGUI_NULL, ESGUI_NULL},
    {0, 0, "星星",   &tst_small_star,     ESGUI_NULL, ESGUI_NULL},
    {0, 0, "WiFi",   &tst_small_wifi,     ESGUI_NULL, ESGUI_NULL},
};

static ESGUI_MenuItem_T bmp_list_gif_items[] = {
    {0, 0, "静态:星星", &tst_small_star,  ESGUI_NULL, ESGUI_NULL},
    {0, 0, "动图\x03/7", &bmp_list_gif,   ESGUI_NULL, ESGUI_NULL},
    {0, 0, "静态:心形", &tst_small_heart, ESGUI_NULL, ESGUI_NULL},
};

static ESGUI_MenuAction_T bmp_enter_list_popup(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    if ((int)(intptr_t)arg == 0) {
        /* 普通版：短标题，弹窗按 216x200 给（逻辑屏 216x272） */
        ESGUI_DefaultBMPListPopWindowCreate(&bmp_list_popup, "图标", 216, 200,
                                            bmp_list_items,
                                            ESGUI_ITEM_NUM_COUNT(bmp_list_items));
    } else {
        /* 滚动标题版：标题故意写超宽，验证会自动横向滚动 */
        ESGUI_DefaultBMPListScrollTitlePopWindowCreate(
            &bmp_list_popup,
            "滚动标题:标题超宽时自动横向滚动",
            216, 210, bmp_list_items, ESGUI_ITEM_NUM_COUNT(bmp_list_items));
    }
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &bmp_list_popup};
}

static ESGUI_MenuAction_T bmp_enter_gif_list_popup(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    ESGUI_GIFInit(&bmp_list_gif, tst_gif_frames, TST_GIF_FRAME_COUNT,
                  tst_gif_delays, 0, 0);
    ESGUI_DefaultBMPListPopWindowCreate(&bmp_list_popup, "含动图", 216, 200,
                                        bmp_list_gif_items,
                                        ESGUI_ITEM_NUM_COUNT(bmp_list_gif_items));
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &bmp_list_popup};
}

/* 供上层直接调用的通用入口（目前菜单条目已覆盖，保留给扩展用） */
ESGUI_MenuAction_T test_bmp_list_popup_create(int kind)
{
    return bmp_enter_list_popup(ESGUI_NULL, (void *)(intptr_t)kind);
}
