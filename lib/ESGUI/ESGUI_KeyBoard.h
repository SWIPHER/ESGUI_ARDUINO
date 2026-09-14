//
// Created by E_LJF on 2026/8/20.
//

#ifndef ESGUI_ESGUI_KEYBOARD_H
#define ESGUI_ESGUI_KEYBOARD_H

#include "ESGUI_Def.h"
#include "ESGUI_Event.h"
#include "ESGUI_DefaultConfig.h"
#include "ESGUI_BSP_Canvas.h"
#include "ESGUI_BSP_draw.h"
#include "ESGUI_BSP_Text.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 键盘字体，可通过编译宏覆盖以适配不同硬件
 * @note  默认与 ESGUI_DEFAULT_FONT 相同（eui_test_font）；
 *        小屏（如 128x64）建议覆盖为小号字体并调小 ESGUI_KEY_BOARD_KEY_H。
 *        例：-DESGUI_KEY_BOARD_FONT=my_small_font（需自行 include 对应字体头文件）
 */
#ifndef ESGUI_KEY_BOARD_FONT
#define ESGUI_KEY_BOARD_FONT       eui_test_font
#include "eui_test_font.h"
#endif

/* ========== 键动作 ========== */
typedef enum {
    ESGUI_KEY_CHAR = 0,      /* 普通字符输入 */
    ESGUI_KEY_SHIFT,         /* 大小写切换 */
    ESGUI_KEY_PAGE_ABC,      /* 切换到字母页 */
    ESGUI_KEY_PAGE_NUM,      /* 切换到数字/符号页 */
    ESGUI_KEY_BACKSPACE,     /* 退格 */
    ESGUI_KEY_SPACE,         /* 空格 */
    ESGUI_KEY_CURSOR_LEFT,   /* 光标左移 */
    ESGUI_KEY_CURSOR_RIGHT,  /* 光标右移 */
    ESGUI_KEY_CURSOR_UP,     /* 光标上移（多行编辑用，单行编辑时忽略） */
    ESGUI_KEY_CURSOR_DOWN,   /* 光标下移（多行编辑用，单行编辑时忽略） */
    ESGUI_KEY_ENTER,         /* 确定 / 换行（多行编辑） */
    ESGUI_KEY_CANCEL,        /* 取消 */
} ESGUI_KeyAction_T;

/** @brief 单个键描述 */
typedef struct {
    char ch;                  /* 键字符（动作键可为 0） */
    ESGUI_KeyAction_T action; /* 键动作 */
} ESGUI_Key_T;

/** @brief 一行键布局 */
typedef struct {
    const ESGUI_Key_T *keys;  /* 本行键数组 */
    eui_uint8_t key_num;      /* 本行键数 */
} ESGUI_KeyRow_T;

/**
 * @brief 键盘组件实例
 * @note  布局数据驱动：键宽 = 区域宽 ÷ 本行键数（每行自动均分）；
 *        键盘总高 = 行数 × key_h，超过 area_h 时自动垂直滚动（焦点行始终可见）。
 */
typedef struct {
    const ESGUI_KeyRow_T *rows;  /* 当前页布局（行数组） */
    eui_uint8_t row_num;         /* 行数 */
    eui_uint16_t focus;          /* 焦点键线性索引（0..总键数-1） */
    eui_uint8_t  shift;          /* 1=大写 */
    eui_uint8_t  page;           /* 0=字母页 1=数字/符号页 */
    eui_int16_t  view_offset;    /* 垂直滚动偏移（像素），键盘总高>区域高时生效 */
    eui_int16_t  kb_x, kb_y;     /* 键盘区域左上角（屏幕坐标） */
    eui_uint16_t area_w;         /* 键盘区域宽度 */
    eui_uint16_t area_h;         /* 键盘区域可见高度 */
    eui_uint8_t  key_h;          /* 键高（像素） */
    const Font  *font;           /* 键盘字体 */
} ESGUI_KeyBoard_T;

/**
 * @brief 初始化键盘组件
 * @param kb      键盘实例
 * @param font    键盘字体
 * @param x,y     键盘区域左上角（屏幕坐标）
 * @param area_w  键盘区域宽度
 * @param area_h  键盘区域可见高度
 * @param key_h   键高（0=使用 ESGUI_KEY_BOARD_KEY_H 默认值）
 */
void ESGUI_KeyBoardInit(ESGUI_KeyBoard_T *kb, const Font *font,
                        eui_int16_t x, eui_int16_t y,
                        eui_uint16_t area_w, eui_uint16_t area_h, eui_uint8_t key_h);

/**
 * @brief 切换键盘页
 * @param kb    键盘实例
 * @param page  0=字母页（QWERTY），1=数字/符号页
 */
void ESGUI_KeyBoardSetPage(ESGUI_KeyBoard_T *kb, eui_uint8_t page);

/**
 * @brief 处理输入事件（按钮/编码器方向 + 确认）
 * @param kb      键盘实例
 * @param e       事件码（EVT_KEY_UP/DOWN/LEFT/RIGHT/OK/CLICKED）
 * @param out_ch  输出：确认键触发的字符（动作键时为 0）
 * @param out_act 输出：确认键触发的动作
 * @return true=事件被处理（焦点移动或键触发，需要重绘）
 */
bool ESGUI_KeyBoardHandleEvent(ESGUI_KeyBoard_T *kb, ESGUI_EventCode_t e,
                               char *out_ch, ESGUI_KeyAction_T *out_act);

/**
 * @brief 绘制键盘（调用方需先裁剪到键盘区域）
 * @param c  画布
 * @param kb 键盘实例
 */
void ESGUI_KeyBoardDraw(Canvas *c, const ESGUI_KeyBoard_T *kb);

#ifdef __cplusplus
}
#endif

#endif /* ESGUI_ESGUI_KEYBOARD_H */
