/*
 * test_popup_page.h —— 弹窗集合测试页
 *
 * 覆盖 ESGUI 默认虚函数表里的**全部弹窗类型**：
 *   消息弹窗（带/不带按钮）、滚动标题消息弹窗、长文本消息弹窗（自动换行 + 滚动 + 进度条）、
 *   布尔弹窗、滚动标题布尔弹窗、值弹窗（进度条数值调节）、滚动标题值弹窗、
 *   文本列表弹窗、滚动标题文本列表弹窗、图片列表弹窗、滚动标题图片列表弹窗，
 *   以及**多层弹窗叠放**（ESGUI_MAX_POPUP_DEPTH）。
 */
#ifndef TEST_POPUP_PAGE_H
#define TEST_POPUP_PAGE_H

#include "ESGUI.h"

/* 弹窗集合页（每个条目对应一种弹窗） */
ESGUI_MenuAction_T test_popup_page_create(void);

#endif /* TEST_POPUP_PAGE_H */
