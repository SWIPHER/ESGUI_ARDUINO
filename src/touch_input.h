/*
 * touch_input.h —— 触摸手势 → ESGUI 事件
 *
 * 为什么需要这一层：ESGUI 的输入只有事件码（没有坐标），
 * 把触摸翻译成事件后，所有页面/弹窗（默认虚函数表）都能直接用触摸操作，无需改动。
 *
 * 另外本模块会保留"最近一次触点快照 + 采样统计"，
 * 供触摸测试页（src/test_touch_page.c）把原始坐标/手势/触点数画到屏幕上。
 */
#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <stdint.h>
#include "ESGUI_Event.h"        /* ESGUI_EventCode_t / EVT_xxx（只有枚举，C/C++ 都能 include） */
#include "touch_cst816d.h"      /* touch_state_t（原始触点快照，供测试页显示） */

#ifdef __cplusplus
extern "C" {
#endif

void touch_input_init(void);     /* setup 里调一次（内部会调 touch_drv_init） */

/* 每 10ms 调一次；返回本次要投递给框架的事件（EVT_NONE = 本次没有） */
ESGUI_EventCode_t touch_input_poll(uint32_t now_ms);

/* ==================== 测试/调试用（只读，不改变任何状态） ==================== */

/**
 * @brief 取最近一次成功采样的原始触点
 * @param st 输出：触点坐标/手势/触点数（不能为 NULL）
 * @return true=已有过一次成功采样；false=还没采到（I2C 未通或首帧未到）
 */
bool touch_input_get_last(touch_state_t *st);

/**
 * @brief 取采样统计（测试页显示 I2C 通信质量用）
 * @param ok  输出：成功采样次数（可为 NULL）
 * @param err 输出：I2C 读失败次数（可为 NULL）
 */
void touch_input_get_stats(uint32_t *ok, uint32_t *err);

#ifdef __cplusplus
}
#endif
#endif /* TOUCH_INPUT_H */
