/*
 * test_3d_menu_page.h —— 3D 线框菜单测试页
 *
 * 覆盖：ESGUI_3D 线框渲染 + 3D 菜单布局（模型自动缩放、焦点模型绕 Z 轴持续旋转、
 *       透视投影焦距/深度参数、模型尺寸自适应）。
 */
#ifndef TEST_3D_MENU_PAGE_H
#define TEST_3D_MENU_PAGE_H

#include "ESGUI.h"

/* 3D 菜单页（立方体/棱锥/棱柱/八面体等线框模型） */
ESGUI_MenuAction_T test_3d_menu_page_create(void);

#endif /* TEST_3D_MENU_PAGE_H */
