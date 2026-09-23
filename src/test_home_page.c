/*
 * test_home_page.c —— ESGUI 测试工程首页
 *
 * 首页把「ESGUI 里每一个 UI」都挂成一个入口条目，进页面后可以逐项点开验证：
 *   文本菜单 / 图形菜单 / 3D 菜单 / 弹窗集合 / 键盘与编辑 / 绘图图元 / 触摸测试 / 覆盖层动画 / 系统信息
 *
 * 框架铁律（与其它页面一致）：
 *   · 静态页面实例复用：每次进入前调用 xxx_page_create() 重新填表；
 *   · 条目数组必须常驻（不能是栈上变量），on_enter 回调里才安全。
 */
#include "test_home_page.h"

#include <stdio.h>
#include <string.h>

#include <Arduino.h>                /* millis()（本文件是 C，Arduino.h 可被 C 包含） */

#include "ESGUI.h"
#include "ESGUI_PageDefaltVtbl.h"
#include "ESGUI_UseCanvas.h"
#include "esgui_port.h"
#include "tft_drv.h"
#include "touch_cst816d.h"
#include "touch_input.h"

/* 芯片/内存信息用 ESP-IDF 的 C 接口取（ESP.xxx 是 C++ 对象，C 文件里用不了） */
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_flash.h"

#include "test_text_menu_page.h"
#include "test_bmp_menu_page.h"
#include "test_3d_menu_page.h"
#include "test_popup_page.h"
#include "test_edit_page.h"
#include "test_draw_page.h"
#include "test_touch_page.h"
#include "test_overlay_page.h"

ESGUI_MenuPage_T test_home_page;

/* ==================== 子页面入口包装 ====================
 * 条目回调签名是 (page, arg)，而各测试页的 create 无参，故统一包一层。
 */
#define ENTER_WRAP(name, target)                                        \
    static ESGUI_MenuAction_T name(ESGUI_MenuPage_T *page, void *arg)   \
    {                                                                   \
        (void)page;                                                     \
        (void)arg;                                                      \
        return target();                                                \
    }

ENTER_WRAP(enter_text_menu, test_text_menu_page_create)
ENTER_WRAP(enter_bmp_menu, test_bmp_menu_page_create)
ENTER_WRAP(enter_3d_menu, test_3d_menu_page_create)
ENTER_WRAP(enter_popup, test_popup_page_create)
ENTER_WRAP(enter_edit, test_edit_page_create)
ENTER_WRAP(enter_draw, test_draw_page_create)
ENTER_WRAP(enter_touch, test_touch_page_create)
ENTER_WRAP(enter_overlay, test_overlay_page_create)

/* ==================== 系统信息页（自定义绘制 + 自定义输入） ====================
 * 页面 item_num = 0（不走菜单条目机制），on_draw 自己排文本行。
 * 逻辑屏只有 112x128（约 8 个汉字/行、7 行），所以信息拆成 2 页：
 * 轻点/滑动翻页，长按返回。
 * on_page_chenge 留空 → 进出页面不跑过渡动画，返回是"立即生效"的。
 */
static ESGUI_MenuPage_T about_page;
static uint8_t about_page_idx;          /* 0 = 第 1 页，1 = 第 2 页 */

static void about_page_draw(ESGUI_MenuPage_T *page)
{
    if (page == ESGUI_NULL || page->render_ctx == ESGUI_NULL) return;

    CanvasStripIter *c_it = (CanvasStripIter *)page->render_ctx;
    Canvas *canvas = c_it->canvas;
    eui_uint8_t line = 0;
    const eui_uint8_t lh = ESGUI_DEFAULT_FONT.line_height;
    char buf[40];

    /* 行绘制宏：本页所有文字左对齐、按字体行高排 */
    #define ABOUT_LINE(...)                                            \
        do {                                                           \
            snprintf(buf, sizeof(buf), __VA_ARGS__);                    \
            eui_draw_text(canvas, 2, (int)line * lh,                    \
                          &ESGUI_DEFAULT_FONT, buf, EUI_MODE_SET);      \
            line++;                                                     \
        } while (0)

    uint32_t ok = 0, err = 0;
    touch_input_get_stats(&ok, &err);

    if (about_page_idx == 0) {
        esp_chip_info_t chip;
        esp_chip_info(&chip);
        uint32_t flash_size = 0;
        (void)esp_flash_get_size(NULL, &flash_size);

        ABOUT_LINE("系统信息 1/2");
        ABOUT_LINE("芯片 %s", CONFIG_IDF_TARGET);
        ABOUT_LINE("主频 %d MHz", (int)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
        ABOUT_LINE("内核 %d 个", (int)chip.cores);
        ABOUT_LINE("RAM 余 %luK", (unsigned long)(esp_get_free_heap_size() / 1024));
        ABOUT_LINE("PSRAM 余 %luK",
                   (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
        ABOUT_LINE("Flash %lu MB", (unsigned long)(flash_size / (1024 * 1024)));
    } else {
        ABOUT_LINE("系统信息 2/2");
        ABOUT_LINE("面板 240x284");
        ABOUT_LINE("逻辑 %dx%d", ESGUI_LOGIC_W, ESGUI_LOGIC_H);
        ABOUT_LINE("放大 %d 倍", TFT_ZOOM);
        ABOUT_LINE("显存 %d B", (int)(ESGUI_LOGIC_W * (ESGUI_LOGIC_H / 8)));
        ABOUT_LINE("触摸 %s", touch_drv_present() ? "OK" : "无");
        ABOUT_LINE("采样 %lu/%lu", (unsigned long)ok, (unsigned long)err);
    }

    /* 底部提示（最后一行） */
    line = (eui_uint8_t)((canvas->height - lh) / lh);
    eui_draw_text(canvas, 2, (int)line * lh, &ESGUI_DEFAULT_FONT,
                  "轻点翻页 长按退", EUI_MODE_SET);

    #undef ABOUT_LINE
}

static ESGUI_MenuAction_T about_page_input(ESGUI_MenuPage_T *page, ESGUI_EventCode_t e)
{
    (void)page;
    switch (e) {
        case EVT_CLICKED:
        case EVT_KEY_OK:
        case EVT_KEY_UP:
        case EVT_KEY_RIGHT:
            about_page_idx = (uint8_t)((about_page_idx + 1) & 1);   /* 翻页 */
            return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};

        case EVT_KEY_BACK:                                          /* 长按：退出 */
            return (ESGUI_MenuAction_T){ACT_POP_PAGE, ESGUI_NULL};

        default:
            return (ESGUI_MenuAction_T){ACT_NONE, ESGUI_NULL};
    }
}

static const esgui_page_vtable_t about_page_vtable = {
    .on_draw    = about_page_draw,
    .on_input   = about_page_input,
    .on_relayout = ESGUI_NULL,
};

static ESGUI_MenuAction_T enter_about(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    memset(&about_page, 0, sizeof(about_page));
    about_page.title    = "系统信息";
    about_page.vtbl     = &about_page_vtable;
    about_page.item_num = 0;
    about_page.items    = ESGUI_NULL;
    about_page_idx      = 0;
    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &about_page};
}

/* ==================== 首页条目表 ====================
 * 逻辑屏 112 宽 ≈ 8 个汉字/行：标签写得短一些；一屏 6 条，9 条会自动滚动。 */
static ESGUI_MenuItem_T home_items[] = {
    {0, 0, "文本菜单", ESGUI_NULL, enter_text_menu, ESGUI_NULL},
    {0, 0, "图形菜单", ESGUI_NULL, enter_bmp_menu,  ESGUI_NULL},
    {0, 0, "3D菜单",   ESGUI_NULL, enter_3d_menu,   ESGUI_NULL},
    {0, 0, "弹窗集合", ESGUI_NULL, enter_popup,     ESGUI_NULL},
    {0, 0, "键盘编辑", ESGUI_NULL, enter_edit,      ESGUI_NULL},
    {0, 0, "绘图显示", ESGUI_NULL, enter_draw,      ESGUI_NULL},
    {0, 0, "触摸测试", ESGUI_NULL, enter_touch,     ESGUI_NULL},
    {0, 0, "覆盖层",   ESGUI_NULL, enter_overlay,   ESGUI_NULL},
    {0, 0, "系统信息", ESGUI_NULL, enter_about,     ESGUI_NULL},
};

void test_home_page_init(void)
{
    ESGUI_DefaltTextMenuCreate(&test_home_page, home_items, "ESGUI 测试",
                               ESGUI_ITEM_NUM_COUNT(home_items));
}
