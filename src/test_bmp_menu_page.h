/*
 * test_bmp_menu_page.h —— 图形（BMP）菜单测试页
 *
 * 覆盖：位图条目横向排列 + 焦点居中滚动、焦点框生长动画、顶部进度条、
 *       位图列表弹窗（BMP List Popup）、GIF 动图条目（ESGUI_ENABLE_GIF）。
 */
#ifndef TEST_BMP_MENU_PAGE_H
#define TEST_BMP_MENU_PAGE_H

#include "ESGUI.h"

/* BMP 菜单页（图标 + 一个 GIF 动图条目） */
ESGUI_MenuAction_T test_bmp_menu_page_create(void);

/* 图片列表弹窗（普通 + 滚动标题 + 含 GIF 条目三种） */
ESGUI_MenuAction_T test_bmp_list_popup_create(int kind);

#endif /* TEST_BMP_MENU_PAGE_H */
