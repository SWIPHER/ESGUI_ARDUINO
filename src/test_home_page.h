/*
 * test_home_page.h —— ESGUI 测试工程首页（所有测试页面的入口菜单）
 *
 * 首页是「框架第一页」，所以这里导出页面实例，由 esgui_port.c 交给 ESGUI_Init。
 */
#ifndef TEST_HOME_PAGE_H
#define TEST_HOME_PAGE_H

#include "ESGUI.h"

/* 首页页面实例（ESGUI_Init 的 first_page） */
extern ESGUI_MenuPage_T test_home_page;

/* 填好首页的条目表与虚函数表（setup 阶段、ESGUI_Init 之前调用一次） */
void test_home_page_init(void);

#endif /* TEST_HOME_PAGE_H */
