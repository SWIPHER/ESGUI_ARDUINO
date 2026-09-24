/*
 * test_edit_page.c —— 键盘输入弹窗 + 多行编辑页测试
 *
 * 覆盖：
 *   · 键盘输入弹窗（ESGUI_KeyBoard + ESGUI_EditBox）：字母页/数字符号页切换、大小写、
 *     光标移动、退格、空格；按"确定"写回目标缓冲，按"取消"丢弃编辑
 *   · 多行编辑页（ESGUI_MultiLineEditBox）：换行、上下左右移光标、行首退格合并、
 *     显示区自动纵向滚动、返回即保存
 *   · 单行/多行内容在条目右侧实时显示（特殊标记 \x03/0、\x03/1）
 *
 * ★ 一个框架坑（实测踩到）：多行编辑页 on_create 里会先
 *   ESGUI_MultiLineEditBoxInit(work_buf)（**清空** work_buf），再用 init_text 回填。
 *   所以 init_text 不能直接传 work_buf（回填时它已经是空串了，内容会丢）。
 *   这里用一份"进入前的快照"当 init_text，工作缓冲专门保存编辑结果。
 */
#include "test_edit_page.h"

#include <stdio.h>
#include <string.h>

#include "ESGUI.h"
#include "ESGUI_PageDefaltVtbl.h"
#include "ESGUI_Widget.h"

#define EDIT_TEXT_MAX   64
#define EDIT_ML_MAX     192

static ESGUI_MenuPage_T  edit_page;
static ESGUI_MenuPage_T  edit_ml_page;

static char edit_text[EDIT_TEXT_MAX] = "ESGUI";
static char edit_ml_work[EDIT_ML_MAX];
static char edit_ml_src[EDIT_ML_MAX];       /* 进编辑页前的快照（作为 init_text） */

static ESGUI_PopWindow_T edit_kb_popup;
static ESGUI_PopWindow_T edit_msg_popup;

static ESGUI_MenuAction_T edit_show_msg(const char *msg)
{
    ESGUI_DefaultMessagePopWindowCreate(&edit_msg_popup, msg, 180, 110, 1);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &edit_msg_popup};
}

/* ==================== 右侧内容显示（特殊标记） ====================
 *   '\x03/0' → 直接显示 arg 指向的字符串（**裁剪到半屏宽**，避免和左侧标签叠字）
 *   '\x03/1' → 显示多行缓冲的摘要："N行/M字"
 */
static eui_uint16_t edit_page_special_draw(ESGUI_MenuPage_T *page, eui_uint16_t indx, bool measure)
{
    if (page == ESGUI_NULL || page->render_ctx == ESGUI_NULL) return 0;

    char marker = '\0';
    if (!ESGUI_WidgetCheckMarker(page->items[indx].label, ESGUI_WIDGET_DEFAULT_MARK,
                                 ESGUI_NULL, &marker)) {
        return 0;
    }
    if (page->items[indx].arg == ESGUI_NULL) return 0;

    CanvasStripIter *c_it = (CanvasStripIter *)page->render_ctx;
    eui_uint16_t x_right = (eui_uint16_t)(c_it->canvas->width - ESGUI_PROGRESS_BAR_W);
    eui_uint16_t max_w   = (eui_uint16_t)(c_it->canvas->width / 2);
    char buf[32];
    const char *text;

    switch (marker) {
        case '0':
            text = (const char *)page->items[indx].arg;
            if (text[0] == '\0') text = "(空)";
            break;

        case '1': {
            const char *ml = (const char *)page->items[indx].arg;
            uint16_t lines = 1;
            for (const char *p = ml; *p; p++) {
                if (*p == '\n') lines++;
            }
            snprintf(buf, sizeof(buf), "%u行/%u字", (unsigned)lines,
                     (unsigned)strlen(ml));
            text = buf;
            break;
        }

        default:
            return 0;
    }

    eui_uint16_t w = (eui_uint16_t)eui_get_text_width(&ESGUI_DEFAULT_FONT, text);
    if (w > max_w) w = max_w;
    if (measure) {
        return w;
    }
    eui_draw_text_clip(c_it->canvas, (int)(x_right - w), page->items[indx].y,
                       &ESGUI_DEFAULT_FONT, text, EUI_MODE_SET, (int)w);
    return w;
}

/* ==================== 条目回调 ==================== */

static ESGUI_MenuAction_T edit_enter_keyboard(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    /* 键盘弹窗 216x222：键高 36（platformio.ini 的 ESGUI_KEY_BOARD_KEY_H），
     * 字母页 4 行 = 144px、数字页 5 行 = 180px，加输入框 37px 正好 */
    ESGUI_DefaultKeyBoardPopWindowCreate(&edit_kb_popup, 216, 222,
                                         edit_text, sizeof(edit_text), edit_text);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &edit_kb_popup};
}

static ESGUI_MenuAction_T edit_enter_multiline(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    /* 快照 → init_text；工作缓冲保留上一次的编辑结果（见文件头注释） */
    memcpy(edit_ml_src, edit_ml_work, sizeof(edit_ml_src));
    ESGUI_MultiLineEditPageCreate(&edit_ml_page, "多行编辑", edit_ml_work,
                                  sizeof(edit_ml_work), edit_ml_src);
    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &edit_ml_page};
}

static ESGUI_MenuAction_T edit_enter_look(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    /* 用"长文本弹窗"把单行/多行内容完整显示出来（顺带又测一次长文本弹窗） */
    static char view[EDIT_ML_MAX + 32];
    snprintf(view, sizeof(view), "单行文本:\n%s\n\n多行文本:\n%s",
             edit_text[0] ? edit_text : "(空)",
             edit_ml_work[0] ? edit_ml_work : "(空)");
    ESGUI_DefaultMessageLongTextPopWindowCreate(&edit_msg_popup, view, 216, 230);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &edit_msg_popup};
}

static ESGUI_MenuAction_T edit_enter_clear(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    edit_text[0] = '\0';
    edit_ml_work[0] = '\0';
    return edit_show_msg("已清空\n单行与多行");
}

static ESGUI_MenuAction_T edit_enter_back(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    return (ESGUI_MenuAction_T){ACT_POP_PAGE, ESGUI_NULL};
}

/* ==================== 页面虚函数表与创建 ==================== */

static const esgui_page_vtable_t edit_page_vtable = {
    .on_create         = esgui_text_menu_defalt_on_create,
    .on_destroy        = esgui_text_menu_defalt_on_destroy,
    .on_draw           = esgui_text_menu_defalt_on_draw,
    .on_focus_change   = esgui_text_menu_defalt_on_focus_change,
    .on_input          = esgui_menu_defalt_on_input,
    .special_item_draw = edit_page_special_draw,
    .on_page_chenge    = esgui_text_menu_default_on_page_change,
#if ESGUI_ENABLE_MENU_RUNTIME_ITEMS
    .on_relayout       = esgui_text_menu_relayout,
#endif
};

static ESGUI_MenuItem_T edit_items[] = {
    {0, 0, "单行:\x03/0", ESGUI_NULL, edit_enter_keyboard, edit_text},
    {0, 0, "多行:\x03/1", ESGUI_NULL, edit_enter_multiline, edit_ml_work},
    {0, 0, "查看内容",    ESGUI_NULL, edit_enter_look, ESGUI_NULL},
    {0, 0, "清空文本",    ESGUI_NULL, edit_enter_clear, ESGUI_NULL},
    {0, 0, "返回",        ESGUI_NULL, edit_enter_back, ESGUI_NULL},
};

ESGUI_MenuAction_T test_edit_page_create(void)
{
    /* 首次进入给多行缓冲一段初始内容（之后由用户编辑、一直保留） */
    if (edit_ml_work[0] == '\0') {
        snprintf(edit_ml_work, sizeof(edit_ml_work),
                 "第一行:多行编辑测试\n第二行:OK 键换行\n第三行:长按返回");
    }
    memset(edit_ml_src, 0, sizeof(edit_ml_src));

    ESGUI_DefaltTextMenuCreate(&edit_page, edit_items, "键盘与编辑",
                               ESGUI_ITEM_NUM_COUNT(edit_items));
    edit_page.vtbl      = &edit_page_vtable;
    edit_page.focus_idx = 0;

    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &edit_page};
}
