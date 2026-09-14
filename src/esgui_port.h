/*
 * esgui_port.h —— 平台装配层的 C 门面
 *
 * .ino 只会 include 这一个头，所以：
 *   ① 必须自带 extern "C" 保护（否则 C++ 会把函数名修饰掉，链接报 mangled 错误）；
 *   ② 只暴露 2 个函数，里外都不用关心 ESGUI 的头文件。
 */
#ifndef ESGUI_PORT_H
#define ESGUI_PORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* setup() 里调用一次：屏幕初始化 → 框架初始化 → 页面 → 输入 →（可选）UI 任务 */
void esgui_port_init(void);

/* loop() 里反复调用：投递按键事件；单线程模式下同时负责 10ms 一次的 ESGUI_Tick */
void esgui_port_poll(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif /* ESGUI_PORT_H */