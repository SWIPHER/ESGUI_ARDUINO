//
// Created by E_LJF on 2026/8/20.
//

#ifndef ESGUI_ESGUI_MULTILINE_EDITBOX_H
#define ESGUI_ESGUI_MULTILINE_EDITBOX_H

#include "ESGUI_Def.h"
#include "ESGUI_DefaultConfig.h"
#include "ESGUI_BSP_Canvas.h"
#include "ESGUI_BSP_draw.h"
#include "ESGUI_BSP_Text.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 多行文本框组件实例
 * @note  缓冲区由用户提供（char* + max_len，含 '\0'），以 '\n' 分行；
 *        支持插入（含换行）/退格/光标上下左右移动；显示区自动纵向滚动，光标行可见；
 *        行数上限 ESGUI_MULTILINE_EDIT_MAX_LINES，超出部分不参与编辑（调用方控制文本规模）。
 */
typedef struct {
    char *buffer;          /* 文本缓冲区（用户提供） */
    eui_uint16_t max_len;  /* 缓冲区容量（含 '\0'） */
    eui_uint16_t len;      /* 当前长度 */
    eui_uint16_t row;      /* 光标行 */
    eui_uint16_t col;      /* 光标列（行内字节偏移） */
    eui_uint16_t scroll_row; /* 显示区顶部行号 */
    eui_uint16_t line_start[ESGUI_MULTILINE_EDIT_MAX_LINES]; /* 每行起始偏移 */
    eui_uint16_t line_num;   /* 行数（≤ MAX_LINES） */
} ESGUI_MultiLineEditBox_T;

/**
 * @brief 初始化多行文本框
 * @param eb      文本框实例
 * @param buffer  用户缓冲区
 * @param max_len 缓冲区容量（含 '\0'，至少 1）
 */
void ESGUI_MultiLineEditBoxInit(ESGUI_MultiLineEditBox_T *eb, char *buffer, eui_uint16_t max_len);

/**
 * @brief 重建行偏移表（编辑操作后自动调用；外部改动缓冲后需手动调用）
 */
void ESGUI_MultiLineEditBoxRebuildLines(ESGUI_MultiLineEditBox_T *eb);

/**
 * @brief 在光标处插入字符（含 '\n' 换行）
 * @return true=插入成功；false=缓冲满或参数无效
 */
bool ESGUI_MultiLineEditBoxInsert(ESGUI_MultiLineEditBox_T *eb, char ch);

/**
 * @brief 删除光标前字符（行首退格合并上一行）
 * @return true=删除成功；false=全文开头或参数无效
 */
bool ESGUI_MultiLineEditBoxBackspace(ESGUI_MultiLineEditBox_T *eb);

/**
 * @brief 移动光标
 * @param dir -2=上，-1=左，+1=右，+2=下（上下保持目标列，超行尾自动限位）
 */
void ESGUI_MultiLineEditBoxCursorMove(ESGUI_MultiLineEditBox_T *eb, eui_int8_t dir);

/**
 * @brief 绘制多行文本框（黑底 + 白框 + 白字 + 可选光标，自动纵向滚动）
 * @param c        画布
 * @param eb       文本框实例（绘制时自动调整 scroll_row）
 * @param x,y      文本框区域左上角（屏幕坐标）
 * @param w        区域宽度
 * @param h        区域高度
 * @param font     文本字体
 * @param caret_on true=绘制光标（闪烁由调用方控制）
 */
void ESGUI_MultiLineEditBoxDraw(Canvas *c, ESGUI_MultiLineEditBox_T *eb, int x, int y,
                                eui_uint16_t w, eui_uint16_t h, const Font *font, bool caret_on);

#ifdef __cplusplus
}
#endif

#endif /* ESGUI_ESGUI_MULTILINE_EDITBOX_H */
