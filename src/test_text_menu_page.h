/*
 * test_text_menu_page.h —— 文本菜单测试页
 *
 * 覆盖：普通条目、无回调条目、长文本自动横向滚动、特殊标记（\x03/0 字符串、\x03/1 数值）、
 *       运行时新增/删除条目（ESGUI_ENABLE_MENU_RUNTIME_ITEMS）、动态菜单（malloc 条目数组）。
 */
#ifndef TEST_TEXT_MENU_PAGE_H
#define TEST_TEXT_MENU_PAGE_H

#include "ESGUI.h"

/* 静态文本菜单（条目数组预留在静态池里，可运行时增删） */
ESGUI_MenuAction_T test_text_menu_page_create(void);

/* 动态文本菜单（条目数组由框架 malloc，可无限次增删、自动扩容/缩容） */
ESGUI_MenuAction_T test_text_dynamic_page_create(void);

#endif /* TEST_TEXT_MENU_PAGE_H */
