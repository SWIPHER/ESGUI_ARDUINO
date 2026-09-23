/*
 * test_draw_page.h —— 绘图 / 显示测试页（自定义 on_draw 的全屏演示页）
 *
 * 覆盖：全部 BSP 绘图图元（点/线/矩形/圆/三角/圆角矩形，SET-CLEAR-XOR 三种模式）、
 *       位图（普通/透明/反色）、Widget 组件（四种方向进度条、复选框、焦点框）、
 *       字体渲染与裁剪、调色板换色（tft_drv 的 g_pal_cb，验证 RGB565 输出）、
 *       像素对齐自检、以及绕过框架的直写彩条（显示自检）。
 */
#ifndef TEST_DRAW_PAGE_H
#define TEST_DRAW_PAGE_H

#include "ESGUI.h"

/* 绘图/显示测试页（上下滑动或点击切换图案） */
ESGUI_MenuAction_T test_draw_page_create(void);

#endif /* TEST_DRAW_PAGE_H */
