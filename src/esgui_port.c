/*
 * esgui_port.c —— 把「屏幕 + ESGUI 框架 + 测试页面 + 触摸」装到一起
 *
 * 这个文件是 C 语言（.c），所以可以直接 include 框架头文件，不会有名字修饰问题。
 * 里面只有 3 件事：
 *   ① 定义显存/画布/UI 实例（全是 static，零 malloc）
 *   ② 每 10ms 问一次触摸层"有没有事件？" → ESGUI_FeedKey()
 *   ③ 唯一一处 ESGUI_Tick()（UI 的唯一消费者）
 */
#include "esgui_port.h"

#include "ESGUI.h"                  /* ESGUI_Init / ESGUI_FeedKey / ESGUI_Tick */
#include "ESGUI_UseCanvas.h"        /* ESGUI_BindCanvas / ESGUI_CanvasRefresh_CB / ESGUI_AnimTick_CB */
#include "ESGUI_PageDefaltVtbl.h"   /* 默认页面/弹窗构造（自带 extern "C"） */
#include "tft_drv.h"                /* 分辨率 / 条带高 / esgui_flush_area */
#include "touch_input.h"            /* 触摸 → ESGUI 事件（§1.7 + §1.8） */
#include "test_home_page.h"         /* 测试工程首页（所有测试页面的入口菜单） */

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


/* ==================== ① 运行模式 ====================
 *  0 = 单线程轮询：屏幕刷新、触摸、Tick 全在 Arduino loop() 里做（**先用它跑通**）
 *  1 = 独立 UI 任务：UI 在 core0 的独立任务里 Tick，loop() 只做"触摸→事件"（动画更稳）
 *
 * 两种模式都必须满足（框架的铁律）：
 *   ESGUI_Tick    只有一处调用   ← 唯一消费者
 *   ESGUI_FeedKey 只有一处调用   ← 单生产者（触摸/I2C 也只在 loop 里读，天然单生产者）
 * 模式 1 必须保持 ESGUI_ENABLE_MULTITHREAD = 1（下面有 #error 帮你会诊）。
 */
#ifndef PORT_USE_UI_TASK
#define PORT_USE_UI_TASK 0
#endif

#if PORT_USE_UI_TASK && !ESGUI_ENABLE_MULTITHREAD
  #error "PORT_USE_UI_TASK=1 需要 ESGUI_ENABLE_MULTITHREAD=1（事件要经队列跨任务投递）"
#endif

/* ==================== ② 静态对象与显存（零动态分配） ==================== */
static ESGUI_T         ui;
static Canvas          canvas;
static CanvasStripIter itr;

/* 1bpp 显存：240x280 → 240 * (280/8) = 8400 字节，放内部 RAM 即可 */
static uint8_t gram[ESGUI_LOGIC_W * (ESGUI_LOGIC_H / 8)];

/* ==================== ③ 送屏出口：覆盖框架里的弱定义 ====================
 * 框架 ESGUI/Adapter/ESGUI_UseCanvas.c 里是：
 *     __WEAK void ESGUI_UseCanvasFlush(int,int,int,int,const eui_uint8_t*,void*){ }
 * 我们在这里给一个同名强定义 → 链接时自动生效，框架一行都不用改。
 * 注意：eui_uint8_t 就是 unsigned char，和 uint8_t 是同一类型，不需要强转。
 */
void ESGUI_UseCanvasFlush(int x0, int y0, int x1, int y1,
                          const eui_uint8_t *buff, void *user)
{
    (void)user;
    esgui_flush_area(x0, y0, x1, y1, buff);
}

/* ==================== ④ 输入适配：触摸 → ESGUI 事件 ====================
 * 手势翻译全在 touch_input.c 里（§1.8），这里只负责"投递给框架"。
 *
 * ★ 为什么要连 EVT_NONE 一起投递？
 *   框架里有个"同向按键节流 + 连击加速"机制（ESGUI.c 的 FeedKey_impl）：
 *   若**连续两次相同事件**的间隔小于 repeat_delay_ms，第二次会被直接丢弃。
 *   中间插一个 EVT_NONE 会把 last_event 复位，于是"快速连滑"的每一格都能生效。
 *   （老版 EC11 例程也是每轮都投一次 EVT_NONE，所以两种输入手感一致。）
 */
static void port_input_feed(uint32_t now_ms)
{
    ESGUI_FeedKey(&ui, touch_input_poll(now_ms), now_ms);
}

#if PORT_USE_UI_TASK
/* UI 任务 = 唯一消费者 */
static void esgui_ui_task(void *arg)
{
    (void)arg;
    for (;;) {
        ESGUI_Tick(&ui, (uint32_t)millis());
        vTaskDelay(pdMS_TO_TICKS(10));          /* 10ms 一帧；想省电可改 15~20 */
    }
}
#endif

/* ==================== ⑤ 初始化（setup() 里调用） ==================== */
void esgui_port_init(void)
{
    /* 1) 屏幕：tft.init + setRotation + 清屏（内部会打印/不打印都无所谓） */
    tft_drv_init();

    /* 2) 触摸：Wire.begin(15,14,400k) + 复位芯片 + 探测 0x15（失败只打印提示，不阻塞） */
    touch_input_init();

    /* 3) 框架：首页初始化 → Init → BindCanvas，必须在第一次 Tick 之前完成
     *    最后一个参数 = 条带高：用 tft_drv.h 里的 ESGUI_STRIP_H（推荐 32） */
    test_home_page_init();                      /* 测试首页：条目表 + 虚函数表（无动态分配） */
    ESGUI_Init(&ui, &test_home_page, ESGUI_CanvasRefresh_CB, ESGUI_AnimTick_CB);
    ESGUI_BindCanvas(&ui, &canvas, &itr,
                     gram, ESGUI_LOGIC_W, ESGUI_LOGIC_H, ESGUI_STRIP_H);

#if PORT_USE_UI_TASK
    /* 栈 4096、优先级 4、core 0（高于 Arduino loopTask，低于 WiFi 系统任务） */
    xTaskCreatePinnedToCore(esgui_ui_task, "ESGUI_UI", 4096, NULL, 4, NULL, 0);
#endif
}

/* ==================== ⑦ UI 实例访问器 ====================
 * 少数测试页需要直接调 UI 级 API（覆盖层 ESGUI_OverlayAdd 等），
 * 而 ui 是本文件 static 的（不存在全局符号），所以统一从这里取。
 */
struct esgui *esgui_port_get_ui(void)
{
    return &ui;
}

/* ==================== ⑥ 主循环（loop() 里调用） ==================== */
void esgui_port_poll(uint32_t now_ms)
{
    /* 输入：单生产者，只在 loop() 这一个地方调用（触摸 I2C 也在这里读） */
    port_input_feed(now_ms);

#if !PORT_USE_UI_TASK
    /* 单线程模式：10ms 一次 Tick（唯一消费者）。
     * 框架内部有 need_refresh 判断：没有动画/没变化时不会刷屏，所以空转几乎零开销。 */
    static uint32_t last_tick = 0;
    if ((uint32_t)(now_ms - last_tick) >= 10) {
        last_tick = now_ms;
        ESGUI_Tick(&ui, now_ms);
    }
#endif
}