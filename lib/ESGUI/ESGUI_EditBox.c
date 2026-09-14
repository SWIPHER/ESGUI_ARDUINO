//
// Created by E_LJF on 2026/8/20.
//

/**
 * @file ESGUI_EditBox.c
 * @brief 单行文本框组件实现
 *
 * 设计要点：
 *  - 缓冲区由用户提供，组件只维护 len/cursor/scroll；
 *  - 光标前宽度通过"临时截断"计算（UI 单线程安全，无需额外缓冲）；
 *  - 文本超宽时自动水平滚动，保证光标始终可见。
 */

#include "ESGUI_EditBox.h"
#include <string.h>

void ESGUI_EditBoxInit(ESGUI_EditBox_T *eb, char *buffer, eui_uint16_t max_len)
{
    if (eb == ESGUI_NULL) return;
    eb->buffer = buffer;
    eb->max_len = (max_len > 0) ? max_len : 1;
    eb->len = 0;
    eb->cursor = 0;
    eb->scroll = 0;
    if (eb->buffer) eb->buffer[0] = '\0';
}

bool ESGUI_EditBoxInsert(ESGUI_EditBox_T *eb, char ch)
{
    if (eb == ESGUI_NULL || eb->buffer == ESGUI_NULL) return false;
    if (eb->len + 1 >= eb->max_len) return false;   /* 预留 1 字节给 '\0' */
    memmove(&eb->buffer[eb->cursor + 1], &eb->buffer[eb->cursor],
            (size_t)(eb->len - eb->cursor + 1));
    eb->buffer[eb->cursor] = ch;
    eb->cursor++;
    eb->len++;
    return true;
}

bool ESGUI_EditBoxBackspace(ESGUI_EditBox_T *eb)
{
    if (eb == ESGUI_NULL || eb->buffer == ESGUI_NULL) return false;
    if (eb->cursor == 0) return false;
    memmove(&eb->buffer[eb->cursor - 1], &eb->buffer[eb->cursor],
            (size_t)(eb->len - eb->cursor + 1));
    eb->cursor--;
    eb->len--;
    return true;
}

void ESGUI_EditBoxCursorMove(ESGUI_EditBox_T *eb, eui_int8_t dir)
{
    if (eb == ESGUI_NULL) return;
    if (dir < 0) {
        if (eb->cursor > 0) eb->cursor--;
    } else if (dir > 0) {
        if (eb->cursor < eb->len) eb->cursor++;
    }
}

void ESGUI_EditBoxDraw(Canvas *c, ESGUI_EditBox_T *eb, int x, int y,
                       eui_uint16_t w, const Font *font, bool caret_on)
{
    if (c == ESGUI_NULL || eb == ESGUI_NULL || font == ESGUI_NULL ||
        eb->buffer == ESGUI_NULL) return;

    int font_h = eui_get_text_height(font, "0");
    int h = font_h + 4;

    /* 先填黑底，保证输入框区域背景不透明（与弹窗风格一致） */
    eui_draw_rect_fill(c, x, y, x + (int)w - 1, y + h - 1, EUI_MODE_CLER);

    /* 边框（黑底上 SET = 白框） */
    eui_draw_round_rect_stroke(c, x, y, x + (int)w - 1, y + h - 1, 2, EUI_MODE_SET);

    /* 文本区裁剪 */
    Area a = { x + 2, y + 1, x + (int)w - 3, y + h - 2 };
    canvas_clip_push(c, &a);

    /* 光标前宽度（临时截断计算，UI 单线程安全） */
    char save = eb->buffer[eb->cursor];
    eb->buffer[eb->cursor] = '\0';
    int w_before = eui_get_text_width(font, eb->buffer);
    eb->buffer[eb->cursor] = save;

    /* 水平滚动：保证光标始终可见 */
    int avail = (int)w - 4;
    if (w_before - eb->scroll > avail) eb->scroll = (eui_int16_t)(w_before - avail);
    if (w_before < eb->scroll)         eb->scroll = (eui_int16_t)w_before;
    if (eb->scroll < 0)                eb->scroll = 0;

    /* 黑底上 SET = 白字 */
    eui_draw_text(c, x + 2 - eb->scroll, y + 2, font, eb->buffer, EUI_MODE_SET);

    if (caret_on) {
        eui_draw_vline(c, x + 2 + w_before - eb->scroll, y + 1, y + h - 2, EUI_MODE_SET);
    }

    canvas_clip_pop(c);
}
