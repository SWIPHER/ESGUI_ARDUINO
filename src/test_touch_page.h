/*
 * test_touch_page.h —— 触摸测试页
 *
 * 覆盖：CST816D 原始采样（坐标/手势码/触点数）、触摸→ESGUI 事件翻译结果统计
 *       （轻点、上下左右滑动、长按返回）、I2C 通信成功率、坐标网格与轨迹显示。
 *
 * 页面靠一个无限循环的"脉冲动画"让框架每 Tick 都重绘，从而实时显示触点在哪儿。
 */
#ifndef TEST_TOUCH_PAGE_H
#define TEST_TOUCH_PAGE_H

#include "ESGUI.h"

/* 触摸测试页（点击=清空计数，长按=返回首页） */
ESGUI_MenuAction_T test_touch_page_create(void);

#endif /* TEST_TOUCH_PAGE_H */
