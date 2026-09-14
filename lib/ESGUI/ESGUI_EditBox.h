//
// Created by E_LJF on 2026/8/20.
//

#ifndef ESGUI_ESGUI_EDITBOX_H
#define ESGUI_ESGUI_EDITBOX_H

#include "ESGUI_Def.h"
#include "ESGUI_BSP_Canvas.h"
#include "ESGUI_BSP_draw.h"
#include "ESGUI_BSP_Text.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 单行文本框组件实例
 * @note  缓冲区由用户提供（char* + max_len，含 '\0'）；
 *        支持插入/退格/光标移动；文本超宽时自动水平滚动，光标始终可见。
 */
typedef struct {
    char *buffer;         /* 文本缓冲区（用户提供） */
    eui_uint16_t max_len; /* 缓冲区容量（含 '\0'） */
    eui_uint16_t len;     /* 当前文本长度 */
    eui_uint16_t cursor;  /* 光标位置 0..len */
    eui_int16_t  scroll;  /* 水平滚动偏移（像素） */
} ESGUI_EditBox_T;

/**
 * @brief 初始化文本框
 * @param eb      文本框实例
 * @param buffer  用户缓冲区
 * @param max_len 缓冲区容量（含 '\0'，至少 1）
 */
void ESGUI_EditBoxInit(ESGUI_EditBox_T *eb, char *buffer, eui_uint16_t max_len);

/**
 * @brief 在光标处插入字符
 * @return true=插入成功；false=缓冲区已满或参数无效
 */
bool ESGUI_EditBoxInsert(ESGUI_EditBox_T *eb, char ch);

/**
 * @brief 删除光标前一个字符
 * @return true=删除成功；false=光标在开头或参数无效
 */
bool ESGUI_EditBoxBackspace(ESGUI_EditBox_T *eb);

/**
 * @brief 移动光标
 * @param dir -1=左移，+1=右移
 */
void ESGUI_EditBoxCursorMove(ESGUI_EditBox_T *eb, eui_int8_t dir);

/**
 * @brief 绘制单行文本框（边框 + 文本 + 可选光标）
 * @param c        画布
 * @param eb       文本框实例（绘制时自动更新水平滚动，保证光标可见）
 * @param x,y      文本框区域左上角（屏幕坐标）
 * @param w        文本框区域宽度
 * @param font     文本字体
 * @param caret_on true=绘制光标（闪烁由调用方控制）
 * @note  框高 = 字体行高 + 4；调用方需保证 y+框高 在有效区域内
 */
void ESGUI_EditBoxDraw(Canvas *c, ESGUI_EditBox_T *eb, int x, int y,
                       eui_uint16_t w, const Font *font, bool caret_on);

#ifdef __cplusplus
}
#endif

#endif /* ESGUI_ESGUI_EDITBOX_H */
