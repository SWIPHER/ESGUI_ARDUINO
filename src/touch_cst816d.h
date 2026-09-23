/*
 * touch_cst816d.h —— CST816D 电容触摸驱动（Waveshare ESP32-S3-Touch-LCD-1.83）
 *
 * 纯 I2C 采集层：只负责"读一次触点"，不认识 ESGUI。
 * 实现在 touch_cst816d.cpp（Wire 是 C++ 类），对外是纯 C 接口。
 */
#ifndef TOUCH_CST816D_H
#define TOUCH_CST816D_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== ① 接线（本板固定，见 §1.2 的引脚表）===== */
#define TOUCH_I2C_SDA   15
#define TOUCH_I2C_SCL   14
#define TOUCH_RST_PIN   39
#define TOUCH_INT_PIN   13
#define TOUCH_I2C_ADDR  0x15        /* CST816D 7 位地址 */
#define TOUCH_I2C_FREQ  400000      /* 400kHz（与板载 AXP2101/QMI8658/RTC 共用这条总线） */

/* ===== ② 面板坐标范围（与 tft.setRotation(0) 一致，1:1 对得上）
 *  若实测"触摸方向与显示相反"（手指在左上，读数却在右下），把下面两个镜像开关置 1
 *  即可——这是面板批次/贴合方向不同造成的常见现象，不用改其它代码：
 *    TOUCH_MIRROR_X=1 → X 反向；TOUCH_MIRROR_Y=1 → Y 反向
 *  只镜像"手指→度数"的方向：滑动方向判定会跟着一起变正确（因为都以面板坐标为准）。
 *  本板（240x284 + 同向贴合）两个都保持 0。 */
#ifndef TOUCH_MIRROR_X
#define TOUCH_MIRROR_X  0
#endif
#ifndef TOUCH_MIRROR_Y
#define TOUCH_MIRROR_Y  0
#endif

#define TOUCH_PANEL_W   240
#define TOUCH_PANEL_H   284

/* ===== ③ CST816D 手势码（寄存器 0x01，采集层只原样上报，用不用由上层决定）===== */
#define TOUCH_GESTURE_NONE         0x00
#define TOUCH_GESTURE_SLIDE_UP     0x01
#define TOUCH_GESTURE_SLIDE_DOWN   0x02
#define TOUCH_GESTURE_SLIDE_LEFT   0x03
#define TOUCH_GESTURE_SLIDE_RIGHT  0x04
#define TOUCH_GESTURE_CLICK        0x05
#define TOUCH_GESTURE_LONG_PRESS   0x0B
#define TOUCH_GESTURE_DOUBLE_CLICK 0x0C

/* ===== ④ 一次采样结果 ===== */
typedef struct {
    uint16_t x;         /* 触点 X（0..TOUCH_PANEL_W-1） */
    uint16_t y;         /* 触点 Y（0..TOUCH_PANEL_H-1） */
    uint8_t  gesture;   /* 芯片识别的手势码（见上面一组宏） */
    uint8_t  fingers;   /* 触点数；0 = 当前没有手指按着（此时 x/y 可能残留上次的值） */
} touch_state_t;

/* ===== ⑤ 接口 ===== */
void touch_drv_init(void);                /* 初始化 I2C + 复位 + 探测 0x15（setup 里一次） */
bool touch_drv_read(touch_state_t *st);   /* 读一次；false = I2C 通信失败（≠"没触摸"） */
bool touch_drv_present(void);             /* 探测结果：芯片是否在线 */

#ifdef __cplusplus
}
#endif
#endif /* TOUCH_CST816D_H */