/*
 * touch_input.h —— 触摸手势 → ESGUI 事件
 *
 * 为什么需要这一层：ESGUI 的输入只有事件码（没有坐标），
 * 把触摸翻译成事件后，所有页面/弹窗（默认虚函数表）都能直接用触摸操作，无需改动。
 */
#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <stdint.h>
#include "ESGUI_Event.h"        /* ESGUI_EventCode_t / EVT_xxx（只有枚举，C/C++ 都能 include） */

#ifdef __cplusplus
extern "C" {
#endif

void touch_input_init(void);     /* setup 里调一次（内部会调 touch_drv_init） */

/* 每 10ms 调一次；返回本次要投递给框架的事件（EVT_NONE = 本次没有） */
ESGUI_EventCode_t touch_input_poll(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* TOUCH_INPUT_H */