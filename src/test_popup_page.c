/*
 * test_popup_page.c —— 弹窗集合测试页（ESGUI 默认虚函数表里的全部弹窗）
 *
 * 一个条目对应一种弹窗，逐项点开即可把整套弹窗走一遍：
 *   消息（带/无按钮）、滚动标题消息、长文本（自动换行+滚动+进度条）、
 *   布尔、滚动标题布尔、值（进度条数值）、滚动标题值、
 *   文本列表、滚动标题文本列表、图片列表、滚动标题图片列表、以及多层弹窗叠放。
 *
 * ★ 弹窗数据是框架内的静态池（按弹窗实例分配，关闭时归还），
 *   所以同一个静态弹窗实例可以反复 Create 复用；只有"同时叠放"才需要多个实例。
 */
#include "test_popup_page.h"

#include <stdint.h>
#include <stdio.h>

#include "ESGUI.h"
#include "ESGUI_PageDefaltVtbl.h"
#include "test_assets.h"

/* 条目编号（与条目表顺序一致，作为条目 arg 传给弹窗分发器） */
enum {
    POP_MSG = 0,
    POP_MSG_NOBTN,
    POP_MSG_SCROLL,
    POP_LONGTEXT,
    POP_BOOL,
    POP_BOOL_SCROLL,
    POP_VALUE,
    POP_VALUE_SCROLL,
    POP_TEXTLIST,
    POP_TEXTLIST_SCROLL,
    POP_BMPLIST,
    POP_BMPLIST_SCROLL,
    POP_STACK,
    POP_BACK,
};

static ESGUI_MenuPage_T  popup_page;
static ESGUI_PopWindow_T popup_a;           /* 单层弹窗复用实例 */
static ESGUI_PopWindow_T popup_s1, popup_s2, popup_s3;   /* 叠放测试专用 */

/* ---- 布尔弹窗绑定的变量 ---- */
static bool pop_bool_val = true;

/* ---- 值弹窗（0~1000）---- */
static eui_int16_t pop_value = 420;

static eui_uint16_t pop_value_get_permille(void *ctx)
{
    eui_int16_t v = *(eui_int16_t *)ctx;
    if (v < 0) return 0;
    if (v > 1000) return 1000;
    return (eui_uint16_t)v;
}

static eui_uint8_t pop_value_to_string(void *ctx, char *buf, eui_uint16_t size)
{
    return (eui_uint8_t)snprintf(buf, size, "%d", (int)(*(eui_int16_t *)ctx));
}

static bool pop_value_step(void *ctx, eui_int8_t direction)
{
    eui_int16_t v = *(eui_int16_t *)ctx;
    if (direction > 0) {
        if (v >= 1000) return false;
        v += 10;                                   /* 步长 10：1000 格刚好 100 步 */
    } else {
        if (v <= 0) return false;
        v -= 10;
    }
    *(eui_int16_t *)ctx = v;
    return true;
}

static const ESGUI_ValueDesc_T pop_value_desc = {
    .ctx          = &pop_value,
    .get_permille = pop_value_get_permille,
    .to_string    = pop_value_to_string,
    .step         = pop_value_step,
};

/* ---- 文本列表弹窗条目（逻辑屏 216 宽，一行 ≈7 个汉字）---- */
static ESGUI_MenuItem_T pop_text_items[] = {
    {0, 0, "列表项 1", ESGUI_NULL, ESGUI_NULL, ESGUI_NULL},
    {0, 0, "列表项 2", ESGUI_NULL, ESGUI_NULL, ESGUI_NULL},
    {0, 0, "列表项 3", ESGUI_NULL, ESGUI_NULL, ESGUI_NULL},
    {0, 0, "超长文本项会横向滚动", ESGUI_NULL, ESGUI_NULL, ESGUI_NULL},
    {0, 0, "列表项 5", ESGUI_NULL, ESGUI_NULL, ESGUI_NULL},
    {0, 0, "列表项 6", ESGUI_NULL, ESGUI_NULL, ESGUI_NULL},
    {0, 0, "列表项 7", ESGUI_NULL, ESGUI_NULL, ESGUI_NULL},
    {0, 0, "列表项 8", ESGUI_NULL, ESGUI_NULL, ESGUI_NULL},
};

/* ---- 图片列表弹窗条目（缩略图标来自 test_assets）---- */
static ESGUI_MenuItem_T pop_bmp_items[] = {
    {0, 0, "设置",   &tst_small_settings, ESGUI_NULL, ESGUI_NULL},
    {0, 0, "音乐",   &tst_small_music,    ESGUI_NULL, ESGUI_NULL},
    {0, 0, "文件夹", &tst_small_folder,   ESGUI_NULL, ESGUI_NULL},
    {0, 0, "心形",   &tst_small_heart,    ESGUI_NULL, ESGUI_NULL},
    {0, 0, "星星",   &tst_small_star,     ESGUI_NULL, ESGUI_NULL},
    {0, 0, "WiFi",   &tst_small_wifi,     ESGUI_NULL, ESGUI_NULL},
};

/* ---- 长文本弹窗内容（自动换行 + 可滚动；不要用全角标点，字库没有）---- */
static const char pop_long_text[] =
    "ESGUI 长文本弹窗测试:\n"
    "1. 文本按弹窗宽度自动换行,支持中文与 \\n 手动换行;\n"
    "2. 内容超过显示高度时,右侧出现进度条,上下滑动可查看全文;\n"
    "3. 点击或长按都能关闭它.\n"
    "下面继续填充内容,把总高度撑过弹窗高度:\n"
    "第一段结束.\n"
    "第二段结束.\n"
    "第三段结束.\n"
    "最后一行:看到这里说明滚动正常.";

/* ==================== 弹窗叠放（3 层）回调 ==================== */

/* 第 3 层：消息弹窗 */
static ESGUI_MenuAction_T stack3_enter(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    ESGUI_DefaultMessagePopWindowCreate(&popup_s3, "第3层弹窗\n点确定逐层退", 216, 110, 1);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &popup_s3};
}

/* 第 2 层：文本列表（含"打开第 3 层"的条目）*/
static ESGUI_MenuItem_T stack2_items[] = {
    {0, 0, "打开第 3 层", ESGUI_NULL, stack3_enter, ESGUI_NULL},
    {0, 0, "返回上一层",  ESGUI_NULL, ESGUI_NULL,    ESGUI_NULL},
};

static ESGUI_MenuAction_T stack2_enter(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    ESGUI_DefaultTextListScrollTitlePopWindowCreate(&popup_s2, "第2层弹窗", 216, 200,
                                                    stack2_items,
                                                    ESGUI_ITEM_NUM_COUNT(stack2_items));
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &popup_s2};
}

/* 第 1 层：文本列表（含"打开第 2 层"的条目）*/
static ESGUI_MenuItem_T stack1_items[] = {
    {0, 0, "打开第 2 层", ESGUI_NULL, stack2_enter, ESGUI_NULL},
    {0, 0, "关闭第 1 层", ESGUI_NULL, ESGUI_NULL,    ESGUI_NULL},
};

/* ==================== 单层弹窗分发器 ====================
 * 逻辑屏 216x272（字行高 33），弹窗尺寸按下面的经验值给：
 *   消息/布尔/值（含滚动标题版）: 112x56   文本列表 / 图片列表: 112x92
 *   键盘: 112x100                          长文本: 112x110
 */
static ESGUI_MenuAction_T popup_dispatch(int idx)
{
    switch (idx) {
        case POP_MSG:
            ESGUI_DefaultMessagePopWindowCreate(&popup_a,
                "消息弹窗\n点确定关闭", 216, 110, 1);
            break;

        case POP_MSG_NOBTN:
            ESGUI_DefaultMessagePopWindowCreate(&popup_a,
                "消息弹窗\n无按钮版", 216, 110, 0);
            break;

        case POP_MSG_SCROLL:
            ESGUI_DefaultMessageScrollTitlePopWindowCreate(&popup_a,
                "滚动标题消息弹窗:标题太长时自动横向循环滚动",
                216, 110, 1);
            break;

        case POP_LONGTEXT:
            ESGUI_DefaultMessageLongTextPopWindowCreate(&popup_a, pop_long_text, 216, 230);
            break;

        case POP_BOOL:
            ESGUI_DefaultBoolPopWindowCreate(&popup_a, "是否开启功能",
                                             "开启", "关闭", 216, 110, &pop_bool_val);
            break;

        case POP_BOOL_SCROLL:
            ESGUI_DefaultBoolScrollTitlePopWindowCreate(&popup_a,
                "布尔弹窗滚动标题版:标题太长自动横向滚动",
                "确定", "取消", 216, 110, &pop_bool_val);
            break;

        case POP_VALUE:
            ESGUI_DefaultValuePopWindowCreate(&popup_a, "值修改 0-1000", 216, 110,
                                              &pop_value_desc);
            break;

        case POP_VALUE_SCROLL:
            ESGUI_DefaultValueScrollTitlePopWindowCreate(&popup_a,
                "值弹窗滚动标题版:上下调数值,进度条跟随",
                216, 110, &pop_value_desc);
            break;

        case POP_TEXTLIST:
            ESGUI_DefaultTextListPopWindowCreate(&popup_a, 216, 200, pop_text_items,
                                                 ESGUI_ITEM_NUM_COUNT(pop_text_items));
            break;

        case POP_TEXTLIST_SCROLL:
            ESGUI_DefaultTextListScrollTitlePopWindowCreate(&popup_a,
                "文本列表滚动标题版:列表可滚动,标题自动滚",
                216, 210, pop_text_items, ESGUI_ITEM_NUM_COUNT(pop_text_items));
            break;

        case POP_BMPLIST:
            ESGUI_DefaultBMPListPopWindowCreate(&popup_a, "图标列表", 216, 200,
                                                pop_bmp_items,
                                                ESGUI_ITEM_NUM_COUNT(pop_bmp_items));
            break;

        case POP_BMPLIST_SCROLL:
            ESGUI_DefaultBMPListScrollTitlePopWindowCreate(&popup_a,
                "图片列表滚动标题版:图标横向滚动居中",
                216, 210, pop_bmp_items, ESGUI_ITEM_NUM_COUNT(pop_bmp_items));
            break;

        case POP_STACK:
            ESGUI_DefaultTextListScrollTitlePopWindowCreate(&popup_s1, "第1层弹窗",
                                                            216, 200, stack1_items,
                                                            ESGUI_ITEM_NUM_COUNT(stack1_items));
            return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &popup_s1};

        case POP_BACK:
        default:
            return (ESGUI_MenuAction_T){ACT_POP_PAGE, ESGUI_NULL};
    }

    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &popup_a};
}

static ESGUI_MenuAction_T popup_enter(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    return popup_dispatch((int)(intptr_t)arg);
}

/* ==================== 条目表 ==================== */

#define POP_ITEM(label, id) {0, 0, label, ESGUI_NULL, popup_enter, (void *)(intptr_t)(id)}

static ESGUI_MenuItem_T popup_items[] = {
    POP_ITEM("消息弹窗", POP_MSG),
    POP_ITEM("无按钮",   POP_MSG_NOBTN),
    POP_ITEM("滚动标题", POP_MSG_SCROLL),
    POP_ITEM("长文本",   POP_LONGTEXT),
    POP_ITEM("布尔弹窗", POP_BOOL),
    POP_ITEM("布尔滚动", POP_BOOL_SCROLL),
    POP_ITEM("值弹窗",   POP_VALUE),
    POP_ITEM("值滚动",   POP_VALUE_SCROLL),
    POP_ITEM("文本列表", POP_TEXTLIST),
    POP_ITEM("列表滚动", POP_TEXTLIST_SCROLL),
    POP_ITEM("图片列表", POP_BMPLIST),
    POP_ITEM("图片滚动", POP_BMPLIST_SCROLL),
    POP_ITEM("弹窗叠放", POP_STACK),
    POP_ITEM("返回",     POP_BACK),
};

ESGUI_MenuAction_T test_popup_page_create(void)
{
    ESGUI_DefaltTextMenuCreate(&popup_page, popup_items, "弹窗集合",
                               ESGUI_ITEM_NUM_COUNT(popup_items));
    popup_page.focus_idx = 0;
    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &popup_page};
}
