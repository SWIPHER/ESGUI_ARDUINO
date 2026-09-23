/*
 * test_edit_page.h —— 键盘与文本编辑测试页
 *
 * 覆盖：键盘输入弹窗（ESGUI_KeyBoard + ESGUI_EditBox：字母页/数字符号页、大小写、
 *       光标移动、退格、确定写入目标缓冲/取消丢弃）、
 *       多行编辑页（ESGUI_MultiLineEditBox：换行、上下左右光标、纵向滚动）。
 */
#ifndef TEST_EDIT_PAGE_H
#define TEST_EDIT_PAGE_H

#include "ESGUI.h"

/* 键盘/编辑测试页 */
ESGUI_MenuAction_T test_edit_page_create(void);

#endif /* TEST_EDIT_PAGE_H */
