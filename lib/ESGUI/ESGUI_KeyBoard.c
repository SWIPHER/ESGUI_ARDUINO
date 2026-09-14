//
// Created by E_LJF on 2026/8/20.
//

/**
 * @file ESGUI_KeyBoard.c
 * @brief 键盘组件实现
 *
 * 设计要点：
 *  - 布局数据驱动：键宽 = 区域宽 ÷ 本行键数，每行自动均分，不写死坐标；
 *  - 内置两页布局：字母页（QWERTY + 功能行）、数字/符号页；
 *  - 垂直滚动：键盘总高超过区域高时，焦点行越界自动调整 view_offset，焦点始终可见；
 *  - 输入适配：按钮/编码器方向键移动焦点，确认键触发当前键动作。
 */

#include "ESGUI_KeyBoard.h"
#include <string.h>

/* 键描述快捷宏 */
#define K(ch)   { (ch), ESGUI_KEY_CHAR }
#define KA(act) { 0, (act) }

/* ========== 字母页布局（QWERTY + 功能行） ========== */
static const ESGUI_Key_T kb_abc_row1[] = {
    K('q'), K('w'), K('e'), K('r'), K('t'), K('y'), K('u'), K('i'), K('o'), K('p')
};
static const ESGUI_Key_T kb_abc_row2[] = {
    K('a'), K('s'), K('d'), K('f'), K('g'), K('h'), K('j'), K('k'), K('l')
};
static const ESGUI_Key_T kb_abc_row3[] = {
    KA(ESGUI_KEY_SHIFT), K('z'), K('x'), K('c'), K('v'), K('b'), K('n'), K('m'), KA(ESGUI_KEY_BACKSPACE)
};
static const ESGUI_Key_T kb_abc_row4[] = {
    KA(ESGUI_KEY_PAGE_NUM), KA(ESGUI_KEY_SPACE), KA(ESGUI_KEY_CURSOR_LEFT),
    KA(ESGUI_KEY_CURSOR_RIGHT), KA(ESGUI_KEY_CURSOR_UP), KA(ESGUI_KEY_CURSOR_DOWN),
    KA(ESGUI_KEY_ENTER), KA(ESGUI_KEY_CANCEL)
};

/* ========== 数字/符号页布局 ========== */
static const ESGUI_Key_T kb_num_row1[] = {
    K('1'), K('2'), K('3'), K('4'), K('5'), K('6'), K('7'), K('8'), K('9'), K('0')
};
static const ESGUI_Key_T kb_num_row2[] = {
    K('!'), K('@'), K('#'), K('$'), K('%'), K('^'), K('&'), K('*'), K('('), K(')')
};
static const ESGUI_Key_T kb_num_row3[] = {
    K('-'), K('_'), K('='), K('+'), K('['), K(']'), K('{'), K('}'), K(';'), K(':')
};
static const ESGUI_Key_T kb_num_row4[] = {
    K('\''), K('"'), K(','), K('.'), K('/'), K('?'), K('`'), K('~')
};
static const ESGUI_Key_T kb_num_row5[] = {
    KA(ESGUI_KEY_PAGE_ABC), KA(ESGUI_KEY_SPACE), KA(ESGUI_KEY_CURSOR_LEFT),
    KA(ESGUI_KEY_CURSOR_RIGHT), KA(ESGUI_KEY_CURSOR_UP), KA(ESGUI_KEY_CURSOR_DOWN),
    KA(ESGUI_KEY_ENTER), KA(ESGUI_KEY_CANCEL)
};

static const ESGUI_KeyRow_T kb_abc_layout[] = {
    {kb_abc_row1, sizeof(kb_abc_row1) / sizeof(kb_abc_row1[0])},
    {kb_abc_row2, sizeof(kb_abc_row2) / sizeof(kb_abc_row2[0])},
    {kb_abc_row3, sizeof(kb_abc_row3) / sizeof(kb_abc_row3[0])},
    {kb_abc_row4, sizeof(kb_abc_row4) / sizeof(kb_abc_row4[0])},
};

static const ESGUI_KeyRow_T kb_num_layout[] = {
    {kb_num_row1, sizeof(kb_num_row1) / sizeof(kb_num_row1[0])},
    {kb_num_row2, sizeof(kb_num_row2) / sizeof(kb_num_row2[0])},
    {kb_num_row3, sizeof(kb_num_row3) / sizeof(kb_num_row3[0])},
    {kb_num_row4, sizeof(kb_num_row4) / sizeof(kb_num_row4[0])},
    {kb_num_row5, sizeof(kb_num_row5) / sizeof(kb_num_row5[0])},
};

/* ========== 行信息辅助 ========== */

/**
 * @brief 计算焦点键所在行、列及行起始索引
 * @param kb     键盘实例
 * @param focus  焦点线性索引
 * @param row    输出：行号
 * @param col    输出：列号
 * @param start  输出：该行第一个键的线性索引
 */
static void kb_row_info(const ESGUI_KeyBoard_T *kb, eui_uint16_t focus,
                        eui_uint8_t *row, eui_uint8_t *col, eui_uint16_t *start)
{
    eui_uint16_t s = 0;
    eui_uint8_t r;
    for (r = 0; r < kb->row_num; r++) {
        eui_uint16_t n = kb->rows[r].key_num;
        if (focus < s + n) {
            if (row)   *row   = r;
            if (col)   *col   = (eui_uint8_t)(focus - s);
            if (start) *start = s;
            return;
        }
        s += n;
    }
    /* 越界兜底：指向最后一行最后一键 */
    if (row)   *row   = kb->row_num - 1;
    if (col)   *col   = (eui_uint8_t)(kb->rows[kb->row_num - 1].key_num - 1);
    if (start) *start = s - kb->rows[kb->row_num - 1].key_num;
}

/**
 * @brief 更新垂直滚动偏移，保证焦点行在可见区域内
 */
static void kb_update_scroll(ESGUI_KeyBoard_T *kb)
{
    eui_uint16_t total_h = (eui_uint16_t)(kb->row_num * kb->key_h);
    if (total_h <= kb->area_h) {
        kb->view_offset = 0;
        return;
    }
    eui_uint8_t row;
    kb_row_info(kb, kb->focus, &row, ESGUI_NULL, ESGUI_NULL);
    eui_int16_t row_y = (eui_int16_t)(row * kb->key_h);
    if (row_y < kb->view_offset) {
        kb->view_offset = row_y;
    } else if (row_y + (eui_int16_t)kb->key_h > kb->view_offset + (eui_int16_t)kb->area_h) {
        kb->view_offset = row_y + (eui_int16_t)kb->key_h - (eui_int16_t)kb->area_h;
    }
    if (kb->view_offset < 0) kb->view_offset = 0;
    eui_int16_t max_off = (eui_int16_t)total_h - (eui_int16_t)kb->area_h;
    if (kb->view_offset > max_off) kb->view_offset = max_off;
}

/* ========== 键标签 ========== */

/**
 * @brief 生成键显示文本（字母键随 shift 切换大小写）
 */
static void kb_key_label(const ESGUI_KeyBoard_T *kb, const ESGUI_Key_T *key, char *out)
{
    switch (key->action) {
        case ESGUI_KEY_CHAR: {
            char ch = key->ch;
            if (kb->shift && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
            out[0] = ch;
            out[1] = '\0';
            break;
        }
        case ESGUI_KEY_SHIFT:        out[0] = 'S';     out[1] = '\0'; break;
        case ESGUI_KEY_PAGE_ABC:     out[0] = 'A'; out[1] = 'B'; out[2] = 'C'; out[3] = '\0'; break;
        case ESGUI_KEY_PAGE_NUM:     out[0] = '1'; out[1] = '2'; out[2] = '3'; out[3] = '\0'; break;
        case ESGUI_KEY_BACKSPACE:    out[0] = '<';     out[1] = '\0'; break;
        case ESGUI_KEY_SPACE:        out[0] = '_';     out[1] = '\0'; break;
        case ESGUI_KEY_CURSOR_LEFT:  out[0] = '<';     out[1] = '\0'; break;
        case ESGUI_KEY_CURSOR_RIGHT: out[0] = '>';     out[1] = '\0'; break;
        case ESGUI_KEY_CURSOR_UP:    out[0] = '^';     out[1] = '\0'; break;
        case ESGUI_KEY_CURSOR_DOWN:  out[0] = 'v';     out[1] = '\0'; break;
        case ESGUI_KEY_ENTER:        out[0] = 'O'; out[1] = 'K'; out[2] = '\0'; break;
        case ESGUI_KEY_CANCEL:       out[0] = 'X';     out[1] = '\0'; break;
        default:                     out[0] = '\0'; break;
    }
}

/* ========== 公开 API ========== */

void ESGUI_KeyBoardInit(ESGUI_KeyBoard_T *kb, const Font *font,
                        eui_int16_t x, eui_int16_t y,
                        eui_uint16_t area_w, eui_uint16_t area_h, eui_uint8_t key_h)
{
    if (kb == ESGUI_NULL) return;
    memset(kb, 0, sizeof(*kb));
    kb->font   = font;
    kb->kb_x   = x;
    kb->kb_y   = y;
    kb->area_w = area_w;
    kb->area_h = area_h;
    kb->key_h  = (key_h == 0) ? (eui_uint8_t)ESGUI_KEY_BOARD_KEY_H : key_h;
    kb->rows   = kb_abc_layout;
    kb->row_num = (eui_uint8_t)(sizeof(kb_abc_layout) / sizeof(kb_abc_layout[0]));
    kb->page   = 0;
    kb->focus  = 0;
    kb->shift  = 0;
    kb->view_offset = 0;
}

void ESGUI_KeyBoardSetPage(ESGUI_KeyBoard_T *kb, eui_uint8_t page)
{
    if (kb == ESGUI_NULL) return;
    if (page) {
        kb->rows    = kb_num_layout;
        kb->row_num = (eui_uint8_t)(sizeof(kb_num_layout) / sizeof(kb_num_layout[0]));
        kb->page    = 1;
    } else {
        kb->rows    = kb_abc_layout;
        kb->row_num = (eui_uint8_t)(sizeof(kb_abc_layout) / sizeof(kb_abc_layout[0]));
        kb->page    = 0;
    }
    kb->focus = 0;
    kb->view_offset = 0;
    kb_update_scroll(kb);
}

bool ESGUI_KeyBoardHandleEvent(ESGUI_KeyBoard_T *kb, ESGUI_EventCode_t e,
                               char *out_ch, ESGUI_KeyAction_T *out_act)
{
    if (kb == ESGUI_NULL || kb->rows == ESGUI_NULL || kb->row_num == 0) return false;

    switch (e) {
        case EVT_KEY_LEFT: {
            eui_uint8_t row, col;
            eui_uint16_t start;
            kb_row_info(kb, kb->focus, &row, &col, &start);
            if (col > 0) {
                kb->focus = start + col - 1;
                kb_update_scroll(kb);
                return true;
            }
            return false;
        }
        case EVT_KEY_RIGHT: {
            eui_uint8_t row, col;
            eui_uint16_t start;
            kb_row_info(kb, kb->focus, &row, &col, &start);
            if (col + 1 < kb->rows[row].key_num) {
                kb->focus = start + col + 1;
                kb_update_scroll(kb);
                return true;
            }
            return false;
        }
        case EVT_KEY_UP: {
            /* 与菜单焦点方向一致（FocusUP：事件 UP = 索引 +1 = 视觉下移） */
            eui_uint16_t total = 0;
            eui_uint8_t r;
            for (r = 0; r < kb->row_num; r++) total += kb->rows[r].key_num;
            if (kb->focus + 1 < total) {
                kb->focus++;
                kb_update_scroll(kb);
                return true;
            }
            return false;
        }
        case EVT_KEY_DOWN: {
            /* 与菜单焦点方向一致（FocusDOWN：事件 DOWN = 索引 -1 = 视觉上移） */
            if (kb->focus > 0) {
                kb->focus--;
                kb_update_scroll(kb);
                return true;
            }
            return false;
        }
        case EVT_KEY_OK:
        case EVT_CLICKED: {
            eui_uint8_t row, col;
            kb_row_info(kb, kb->focus, &row, &col, ESGUI_NULL);
            const ESGUI_Key_T *key = &kb->rows[row].keys[col];
            if (out_ch) {
                *out_ch = key->ch;
                if (kb->shift && key->ch >= 'a' && key->ch <= 'z') {
                    *out_ch = (char)(key->ch - 32);
                }
            }
            if (out_act) *out_act = key->action;
            return true;
        }
        default:
            return false;
    }
}

void ESGUI_KeyBoardDraw(Canvas *c, const ESGUI_KeyBoard_T *kb)
{
    if (c == ESGUI_NULL || kb == ESGUI_NULL || kb->rows == ESGUI_NULL ||
        kb->font == ESGUI_NULL || kb->row_num == 0) return;

    /* 先填黑底，保证键盘区域背景不透明（与弹窗风格一致） */
    eui_draw_rect_fill(c, kb->kb_x, kb->kb_y,
                       kb->kb_x + (eui_int16_t)kb->area_w - 1,
                       kb->kb_y + (eui_int16_t)kb->area_h - 1, EUI_MODE_CLER);

    eui_int16_t font_h = eui_get_text_height(kb->font, "0");
    eui_uint16_t start = 0;
    eui_uint8_t r;

    for (r = 0; r < kb->row_num; r++) {
        const ESGUI_KeyRow_T *row = &kb->rows[r];
        eui_int16_t y0 = kb->kb_y + (eui_int16_t)(r * kb->key_h) - kb->view_offset;
        eui_int16_t y1 = y0 + (eui_int16_t)kb->key_h - 1;
        /* 整行在可见区外则跳过 */
        if (y1 < kb->kb_y || y0 > kb->kb_y + (eui_int16_t)kb->area_h - 1) {
            start += row->key_num;
            continue;
        }
        eui_uint16_t key_w = (row->key_num > 0) ? (kb->area_w / row->key_num) : kb->area_w;
        eui_uint8_t col;
        for (col = 0; col < row->key_num; col++) {
            const ESGUI_Key_T *key = &row->keys[col];
            eui_int16_t x0 = kb->kb_x + (eui_int16_t)(col * key_w);
            eui_int16_t x1 = x0 + (eui_int16_t)key_w - 1;

            bool focused = (start + col == kb->focus);
            bool shifted = (key->action == ESGUI_KEY_SHIFT && kb->shift);
            bool invert  = focused || shifted;

            /* 键框（1px 内缩，避免相邻键边框重叠）
             * 焦点/激活键：整键区域 XOR 反色（黑底反白），文字用 color=0 挖黑字；
             * 普通键：SET 白框 + 白字（eui_draw_text color: 1=白，0=黑） */
            eui_int16_t xa = x0 + 1, ya = y0 + 1, xb = x1 - 1, yb = y1 - 1;
            if (xa > xb) xa = xb;
            if (ya > yb) ya = yb;
            if (invert) {
                eui_draw_rect_fill(c, xa, ya, xb, yb, EUI_MODE_XOR);
            } else {
                eui_draw_rect_stroke(c, xa, ya, xb, yb, EUI_MODE_SET);
            }

            /* 键文本（居中；焦点键黑字，普通键白字） */
            char label[4];
            kb_key_label(kb, key, label);
            int tw = eui_get_text_width(kb->font, label);
            int tx = x0 + ((int)key_w - tw) / 2;
            int ty = y0 + ((int)kb->key_h - font_h) / 2;
            if (ty < 0) ty = 0;
            eui_draw_text(c, tx, ty, kb->font, label, invert ? 0 : 1);
        }
        start += row->key_num;
    }
}
