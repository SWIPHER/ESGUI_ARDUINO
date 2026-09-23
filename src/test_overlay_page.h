/*
 * test_overlay_page.h —— 覆盖层（Overlay）与动画测试页
 *
 * 覆盖：ESGUI_OverlayAdd / Remove / SetVisible（常驻组件叠加在页面与弹窗之上）、
 *       always_dirty 强制每帧重绘、anim_* 全部内置缓动曲线（线性/缓入/缓出/缓入缓出/
 *       冲过/弹跳/步进/自定义）、往返与无限循环动画。
 */
#ifndef TEST_OVERLAY_PAGE_H
#define TEST_OVERLAY_PAGE_H

#include "ESGUI.h"

/* 覆盖层 + 动画演示页 */
ESGUI_MenuAction_T test_overlay_page_create(void);

#endif /* TEST_OVERLAY_PAGE_H */
