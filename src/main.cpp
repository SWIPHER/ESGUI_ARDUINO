#include <Arduino.h>

extern "C" {
  #include "esgui_port.h"     /* 只 include 我们自己的 C 门面，别在这里 include 框架头 */
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n[ESGUI] boot: ESP32-S3 + TFT_eSPI");

  esgui_port_init();          /* 屏幕 → 框架 → 页面 → 输入 →（可选）UI 任务，一次搞定 */
}

void loop() {
  esgui_port_poll(millis());  /* 投递按键；单线程模式下这里同时做 10ms 节拍 */
  delay(2);                   /* 让出 CPU */
}