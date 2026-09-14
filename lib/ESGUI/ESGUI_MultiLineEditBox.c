//
// Created by E_LJF on 2026/8/20.
//

/**
 * @file ESGUI_MultiLineEditBox.c
 * @brief 多行文本框组件实现
 *
 * 设计要点：
 *  - 缓冲区由用户提供，以 '\n' 分行；行偏移表 line_start[] 记录每行起始偏移；
 *  - 光标用 (row, col)，col 为行内字节偏移；上下移动保持目标列并限位；
 *  - 行内文本通过"临时截断行尾"绘制（UI 单线程安全，与单行组件一致）；
 *  - 显示区自动纵向滚动，保证光标行可见。
 */

#include "ESGUI_MultiLineEditBox.h"
#include <string.h>

/** @brief 行长度（不含行尾 '\n'） */
static eui_uint16_t ml_line_len(const ESGUI_MultiLineEditBox_T *eb, eui_uint16_t row)
{
    if (eb == ESGUI_NULL || row >= eb->line_num) return 0;
    eui_uint16_t end = (row + 1 < eb->line_num) ? (eui_uint16_t)(eb->line_start[row + 1] - 1)
                                                : eb->len;
    if (end < eb->line_start[row]) end = eb->line_start[row];
    return (eui_uint16_t)(end - eb->line_start[row]);
}

void ESGUI_MultiLineEditBoxInit(ESGUI_MultiLineEditBox_T *eb, char *buffer, eui_uint16_t max_len)
{
    if (eb == ESGUI_NULL) return;
    memset(eb, 0, sizeof(*eb));
    eb->buffer  = buffer;
    eb->max_len = (max_len > 0) ? max_len : 1;
    eb->len     = 0;
    eb->row     = 0;
    eb->col     = 0;
    eb->scroll_row = 0;
    if (eb->buffer) eb->buffer[0] = '\0';
    eb->line_start[0] = 0;
    eb->line_num = 1;
}

void ESGUI_MultiLineEditBoxRebuildLines(ESGUI_MultiLineEditBox_T *eb)
{
    if (eb == ESGUI_NULL || eb->buffer == ESGUI_NULL) return;
    eui_uint16_t line = 0;
    eui_uint16_t pos = 0;
    eb->line_start[0] = 0;
    while (eb->buffer[pos] && line + 1 < ESGUI_MULTILINE_EDIT_MAX_LINES) {
        if (eb->buffer[pos] == '\n') {
            pos++;
            line++;
            eb->line_start[line] = pos;
            continue;
        }
        pos++;
    }
    eb->line_num = (eui_uint16_t)(line + 1);
    /* 光标限位 */
    if (eb->row >= eb->line_num) {
        eb->row = (eui_uint16_t)(eb->line_num - 1);
        eb->col = 0;
    }
    eui_uint16_t ll = ml_line_len(eb, eb->row);
    if (eb->col > ll) eb->col = ll;
}

bool ESGUI_MultiLineEditBoxInsert(ESGUI_MultiLineEditBox_T *eb, char ch)
{
    if (eb == ESGUI_NULL || eb->buffer == ESGUI_NULL) return false;
    if (eb->len + 1 >= eb->max_len) return false;   /* 预留 1 字节给 '\0' */

    eui_uint16_t pos = (eui_uint16_t)(eb->line_start[eb->row] + eb->col);
    if (pos > eb->len) pos = eb->len;

    memmove(&eb->buffer[pos + 1], &eb->buffer[pos], (size_t)(eb->len - pos + 1));
    eb->buffer[pos] = ch;
    eb->len++;

    if (ch == '\n') {
        eb->row++;
        eb->col = 0;
    } else {
        eb->col++;
    }
    ESGUI_MultiLineEditBoxRebuildLines(eb);
    return true;
}

bool ESGUI_MultiLineEditBoxBackspace(ESGUI_MultiLineEditBox_T *eb)
{
    if (eb == ESGUI_NULL || eb->buffer == ESGUI_NULL) return false;
    if (eb->row == 0 && eb->col == 0) return false;

    eui_uint16_t pos;
    if (eb->col == 0) {
        /* 行首退格：删除上一行行尾的 '\n'，光标移到上一行末尾 */
        eb->row--;
        pos = (eui_uint16_t)(eb->line_start[eb->row + 1] - 1);
        eb->col = (eui_uint16_t)(pos - eb->line_start[eb->row]);
    } else {
        pos = (eui_uint16_t)(eb->line_start[eb->row] + eb->col);
        eb->col--;
    }
    memmove(&eb->buffer[pos], &eb->buffer[pos + 1], (size_t)(eb->len - pos));
    eb->len--;
    ESGUI_MultiLineEditBoxRebuildLines(eb);
    return true;
}

void ESGUI_MultiLineEditBoxCursorMove(ESGUI_MultiLineEditBox_T *eb, eui_int8_t dir)
{
    if (eb == ESGUI_NULL) return;
    if (dir == -1) {                       /* 左 */
        if (eb->col > 0) {
            eb->col--;
        } else if (eb->row > 0) {
            eb->row--;
            eb->col = ml_line_len(eb, eb->row);
        }
    } else if (dir == 1) {                 /* 右 */
        eui_uint16_t ll = ml_line_len(eb, eb->row);
        if (eb->col < ll) {
            eb->col++;
        } else if (eb->row + 1 < eb->line_num) {
            eb->row++;
            eb->col = 0;
        }
    } else if (dir == -2) {                /* 上 */
        if (eb->row > 0) {
            eui_uint16_t ll = ml_line_len(eb, (eui_uint16_t)(eb->row - 1));
            if (eb->col > ll) eb->col = ll;
            eb->row--;
        }
    } else if (dir == 2) {                 /* 下 */
        if (eb->row + 1 < eb->line_num) {
            eui_uint16_t ll = ml_line_len(eb, (eui_uint16_t)(eb->row + 1));
            if (eb->col > ll) eb->col = ll;
            eb->row++;
        }
    }
}

void ESGUI_MultiLineEditBoxDraw(Canvas *c, ESGUI_MultiLineEditBox_T *eb, int x, int y,
                                eui_uint16_t w, eui_uint16_t h, const Font *font, bool caret_on)
{
    if (c == ESGUI_NULL || eb == ESGUI_NULL || font == ESGUI_NULL ||
        eb->buffer == ESGUI_NULL) return;

    int font_h = eui_get_text_height(font, "0");
    if (font_h < 1) font_h = 1;

    /* 黑底 + 边框 */
    eui_draw_rect_fill(c, x, y, x + (int)w - 1, y + (int)h - 1, EUI_MODE_CLER);
    eui_draw_round_rect_stroke(c, x, y, x + (int)w - 1, y + (int)h - 1, 2, EUI_MODE_SET);

    /* 可见行数 */
    eui_uint16_t visible = (eui_uint16_t)(h / font_h);
    if (visible < 1) visible = 1;
    if (visible > eb->line_num) visible = eb->line_num;

    /* 纵向滚动：光标行可见 */
    if (eb->row < eb->scroll_row) {
        eb->scroll_row = eb->row;
    } else if (eb->row >= eb->scroll_row + visible) {
        eb->scroll_row = (eui_uint16_t)(eb->row - visible + 1);
    }
    if (eb->line_num > visible && eb->scroll_row + visible > eb->line_num) {
        eb->scroll_row = (eui_uint16_t)(eb->line_num - visible);
    }

    /* 文本区裁剪 */
    Area a = { x + 2, y + 1, x + (int)w - 3, y + (int)h - 2 };
    canvas_clip_push(c, &a);

    eui_uint16_t r;
    for (r = eb->scroll_row; r < eb->line_num && r < eb->scroll_row + visible; r++) {
        eui_uint16_t start = eb->line_start[r];
        eui_uint16_t end = (r + 1 < eb->line_num) ? (eui_uint16_t)(eb->line_start[r + 1] - 1)
                                                  : eb->len;
        int ty = y + 2 + (int)(r - eb->scroll_row) * font_h;

        /* 行内文本（临时截断行尾） */
        char save = eb->buffer[end];
        eb->buffer[end] = '\0';
        eui_draw_text(c, x + 2, ty, font, eb->buffer + start, 1);
        eb->buffer[end] = save;

        /* 光标（闪烁） */
        if (caret_on && r == eb->row) {
            eui_uint16_t cpos = (eui_uint16_t)(start + eb->col);
            if (cpos > end) cpos = end;
            char save2 = eb->buffer[cpos];
            eb->buffer[cpos] = '\0';
            int cw = eui_get_text_width(font, eb->buffer + start);
            eb->buffer[cpos] = save2;
            eui_draw_vline(c, x + 2 + cw, ty, ty + font_h - 1, EUI_MODE_SET);
        }
    }
    canvas_clip_pop(c);
}
