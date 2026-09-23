/*
 * main.cpp —— ESGUI + 触摸 测试工程（Waveshare ESP32-S3-Touch-LCD-1.83，240x284）
 *
 * 代码分工（看 src/ 目录即可）：
 *   esgui_port.*      : 平台装配层（屏幕初始化 / 显存 / 触摸轮询 / ESGUI_Tick）
 *   tft_drv.*         : ESGUI 1bpp 画布 → RGB565 → TFT_eSPI 送屏 + 显示自检
 *   touch_cst816d.*   : CST816D 触摸 IC 采集（I2C）
 *   touch_input.*     : 触点序列 → ESGUI 事件（轻点/滑动/长按）
 *   test_*_page.*     : 各个测试页面（文本/图形/3D 菜单、弹窗、键盘、绘图、触摸、覆盖层）
 *   test_assets.*     : 位图资源（由 tools/gen_test_assets.py 生成）
 */
#include <Arduino.h>

extern "C" {
  #include "esgui_port.h"     /* 只 include 我们自己的 C 门面，别在这里 include 框架头 */
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[ESGUI] 测试工程启动：ESP32-S3 + TFT_eSPI + CST816D");
  Serial.println("[ESGUI] 首页：文本菜单/图形菜单/3D菜单/弹窗/键盘编辑/绘图/触摸/覆盖层/系统信息");
  Serial.println("[ESGUI] 操作：轻点=确定  上下滑动=移动焦点  长按=返回");

  esgui_port_init();          /* 屏幕 → 框架 → 首页 → 输入 →（可选）UI 任务，一次搞定 */
}

void loop() {
  esgui_port_poll(millis());  /* 投递按键；单线程模式下这里同时做 10ms 节拍 */
  delay(2);                   /* 让出 CPU */
}