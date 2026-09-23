/*
 * test_text_menu_page.c —— 文本菜单 / 动态菜单 / 运行时条目增删 测试页
 *
 * 这个页面把文本菜单的"全部能力"都挂成条目：
 *   · 普通条目（点击弹消息弹窗）
 *   · 无回调条目（点击无反应，验证 on_enter == NULL 不崩）
 *   · 超长文本条目（焦点选中后自动水平环形滚动）
 *   · 特殊标记条目：'\x03/0' 右侧显示字符串、'\x03/1' 右侧显示数值
 *   · 运行时新增 / 删除条目（静态条目池 + ESGUI_MenuPageAddItem/RemoveItem）
 *   · 动态菜单（框架 malloc 条目数组，可增删、自动扩容缩容、销毁自动 free）
 *
 * ★ 条目池为什么必须常驻（static）：
 *   框架只保存 items[i].label 的**指针**，栈上变量在回调返回后即失效。
 */
#include "test_text_menu_page.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "ESGUI.h"
#include "ESGUI_PageDefaltVtbl.h"
#include "ESGUI_Widget.h"

/* ==================== 静态菜单：条目池与状态 ==================== */
#define TEXT_ITEM_BASE   9      /* 固定条目数 */
#define TEXT_ADD_MAX     4      /* 最多可新增 4 条 */
#define TEXT_ITEM_CAP    (TEXT_ITEM_BASE + TEXT_ADD_MAX)

static ESGUI_MenuPage_T  text_page;
static ESGUI_MenuItem_T  text_items[TEXT_ITEM_CAP];

static char       text_value[24] = "ESGUI";     /* 标记 '\x03/0'：字符串值 */
static eui_int8_t text_number    = 42;          /* 标记 '\x03/1'：数值 */
static uint8_t    text_added_num;               /* 已新增条目数 */
static char       text_added_label[TEXT_ADD_MAX][20];   /* 新增条目标签池（常驻） */

static ESGUI_PopWindow_T text_msg_popup;
static ESGUI_PopWindow_T text_kb_popup;
static ESGUI_PopWindow_T text_value_popup;

/* 统一弹消息的小工具（返回 ACT_SHOW_POPUP，调用处直接 return 它）
 * 注意：① 消息弹窗**只保存字符串指针**，所以文本必须是字面量或静态缓冲；
 *       ② 逻辑屏只有 112 宽（≈8 个汉字/行），消息按 2 行、每行 ≤8 个汉字来写；
 *       ③ 字库里没有全角标点（：（）等），一律用 ASCII 标点。 */
static ESGUI_MenuAction_T text_show_msg(const char *msg)
{
    ESGUI_DefaultMessagePopWindowCreate(&text_msg_popup, msg, 112, 56, 1);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &text_msg_popup};
}

/* ==================== 值弹窗的值描述符（0~100 的 int8） ==================== */
#define TEXT_NUM_MAX  100
#define TEXT_NUM_MIN  0

static eui_uint16_t text_num_get_permille(void *ctx)
{
    eui_int8_t v = *(eui_int8_t *)ctx;
    if (v <= TEXT_NUM_MIN) return 0;
    return (eui_uint16_t)(((eui_uint32_t)(v - TEXT_NUM_MIN) * 1000u)
                          / (TEXT_NUM_MAX - TEXT_NUM_MIN));
}

static eui_uint8_t text_num_to_string(void *ctx, char *buf, eui_uint16_t size)
{
    return (eui_uint8_t)snprintf(buf, size, "%d", (int)(*(eui_int8_t *)ctx));
}

static bool text_num_step(void *ctx, eui_int8_t direction)
{
    eui_int8_t v = *(eui_int8_t *)ctx;
    if (direction > 0) {
        if (v >= TEXT_NUM_MAX) return false;    /* 到边界：没有变化 */
        v++;
    } else {
        if (v <= TEXT_NUM_MIN) return false;
        v--;
    }
    *(eui_int8_t *)ctx = v;
    return true;
}

static const ESGUI_ValueDesc_T text_number_desc = {
    .ctx          = &text_number,
    .get_permille = text_num_get_permille,
    .to_string    = text_num_to_string,
    .step         = text_num_step,
};

/* ==================== 特殊条目绘制（右侧附加值） ====================
 * measure=true 只返回占宽（布局阶段），false 才真正画（渲染阶段）。
 * 两条铁律：① measure 时绝不能绘制；② 两次返回的宽度必须一致。
 * 逻辑屏很窄，所以右侧附加值统一**裁剪到半屏宽**，避免和左侧文字叠在一起。
 */
static eui_uint16_t text_page_special_draw(ESGUI_MenuPage_T *page, eui_uint16_t indx, bool measure)
{
    if (page == ESGUI_NULL || page->render_ctx == ESGUI_NULL) return 0;

    char marker = '\0';
    if (!ESGUI_WidgetCheckMarker(page->items[indx].label, ESGUI_WIDGET_DEFAULT_MARK,
                                 ESGUI_NULL, &marker)) {
        return 0;                                   /* 该条目没有特殊标记 */
    }

    CanvasStripIter *c_it = (CanvasStripIter *)page->render_ctx;
    eui_uint16_t x_right = (eui_uint16_t)(c_it->canvas->width - ESGUI_PROGRESS_BAR_W);
    eui_uint16_t max_w   = (eui_uint16_t)(c_it->canvas->width / 2);
    char buf[24];
    const char *text = buf;

    switch (marker) {
        case '0':                                   /* 字符串值 */
            if (page->items[indx].arg == ESGUI_NULL) return 0;
            text = (const char *)page->items[indx].arg;
            break;

        case '1':                                   /* 数值（int8） */
            if (page->items[indx].arg == ESGUI_NULL) return 0;
            snprintf(buf, sizeof(buf), "%d", (int)(*(eui_int8_t *)page->items[indx].arg));
            break;

        default:
            return 0;
    }

    eui_uint16_t w = (eui_uint16_t)eui_get_text_width(&ESGUI_DEFAULT_FONT, text);
    if (w > max_w) w = max_w;
    if (measure) {
        return w;
    }
    eui_draw_text_clip(c_it->canvas, (int)(x_right - w - 2), page->items[indx].y,
                       &ESGUI_DEFAULT_FONT, text, EUI_MODE_SET, (int)w);
    return w;
}

/* ==================== 条目回调（静态菜单） ==================== */

static ESGUI_MenuAction_T text_enter_plain(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    return text_show_msg("普通条目\n点击成功");
}

static ESGUI_MenuAction_T text_enter_edit_text(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    /* 键盘弹窗 112x100：键高 16，字母页 4 行 = 64px、数字页 5 行 = 80px，
     * 加输入框 19px 后都放得下（逻辑屏只有 112x128，这是能给的尺寸） */
    ESGUI_DefaultKeyBoardPopWindowCreate(&text_kb_popup, 112, 100,
                                         text_value, sizeof(text_value), text_value);
    (void)arg;
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &text_kb_popup};
}

static ESGUI_MenuAction_T text_enter_edit_number(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    ESGUI_DefaultValuePopWindowCreate(&text_value_popup, "值修改 0-100", 112, 56,
                                      &text_number_desc);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &text_value_popup};
}

static ESGUI_MenuAction_T text_enter_added_item(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    return text_show_msg("新增的条目\n标签在静态池");
}

static ESGUI_MenuAction_T text_enter_add(ESGUI_MenuPage_T *page, void *arg)
{
    (void)arg;
    if (text_added_num >= TEXT_ADD_MAX) {
        return text_show_msg("新增失败\n已达上限");
    }
    snprintf(text_added_label[text_added_num], sizeof(text_added_label[0]),
             "新增条目%d", text_added_num + 1);
    ESGUI_MenuItem_T it = {0, 0, text_added_label[text_added_num], ESGUI_NULL,
                           text_enter_added_item, ESGUI_NULL};
    if (!ESGUI_MenuPageAddItem(page, &it)) {
        return text_show_msg("新增失败\n容量不足");
    }
    text_added_num++;
    /* AddItem 内部已重排布局；这里再要一帧重绘（返回 ACT_REFRESH） */
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

static ESGUI_MenuAction_T text_enter_remove(ESGUI_MenuPage_T *page, void *arg)
{
    (void)arg;
    if (page->item_num <= TEXT_ITEM_BASE) {
        return text_show_msg("没有可删的\n动态条目");
    }
    if (!ESGUI_MenuPageRemoveItem(page, (eui_uint16_t)(page->item_num - 1))) {
        return text_show_msg("删除失败");
    }
    if (text_added_num > 0) text_added_num--;
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

static ESGUI_MenuAction_T text_enter_back(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    return (ESGUI_MenuAction_T){ACT_POP_PAGE, ESGUI_NULL};
}

static ESGUI_MenuAction_T text_enter_dynamic(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    return test_text_dynamic_page_create();
}

/* ==================== 静态菜单虚函数表 ==================== */

static const esgui_page_vtable_t text_page_vtable = {
    .on_create         = esgui_text_menu_defalt_on_create,
    .on_destroy        = esgui_text_menu_defalt_on_destroy,
    .on_draw           = esgui_text_menu_defalt_on_draw,
    .on_focus_change   = esgui_text_menu_defalt_on_focus_change,
    .on_input          = esgui_menu_defalt_on_input,
    .special_item_draw = text_page_special_draw,
    .on_page_chenge    = esgui_text_menu_default_on_page_change,
#if ESGUI_ENABLE_MENU_RUNTIME_ITEMS
    .on_relayout       = esgui_text_menu_relayout,
#endif
};

ESGUI_MenuAction_T test_text_menu_page_create(void)
{
    /* 固定条目（每次进入都重建，保证新增过的条目被清掉）
     * 标签按"逻辑屏 112 宽 ≈ 8 个汉字"来写；太长的条目会自动横向滚动（本身就是测试项） */
    text_items[0] = (ESGUI_MenuItem_T){0, 0, "普通条目", ESGUI_NULL,
                                       text_enter_plain, ESGUI_NULL};
    text_items[1] = (ESGUI_MenuItem_T){0, 0, "无回调条目", ESGUI_NULL,
                                       ESGUI_NULL, ESGUI_NULL};
    text_items[2] = (ESGUI_MenuItem_T){0, 0,
        "超长文本条目:焦点选中后会自动横向滚动,看首尾能否无缝循环衔接", ESGUI_NULL,
        ESGUI_NULL, ESGUI_NULL};
    text_items[3] = (ESGUI_MenuItem_T){0, 0, "文本值:\x03/0", ESGUI_NULL,
                                       text_enter_edit_text, text_value};
    text_items[4] = (ESGUI_MenuItem_T){0, 0, "数值:\x03/1", ESGUI_NULL,
                                       text_enter_edit_number, &text_number};
    text_items[5] = (ESGUI_MenuItem_T){0, 0, "新增条目", ESGUI_NULL,
                                       text_enter_add, ESGUI_NULL};
    text_items[6] = (ESGUI_MenuItem_T){0, 0, "删除条目", ESGUI_NULL,
                                       text_enter_remove, ESGUI_NULL};
    text_items[7] = (ESGUI_MenuItem_T){0, 0, "动态菜单", ESGUI_NULL,
                                       text_enter_dynamic, ESGUI_NULL};
    text_items[8] = (ESGUI_MenuItem_T){0, 0, "返回", ESGUI_NULL,
                                       text_enter_back, ESGUI_NULL};

    text_added_num = 0;

    ESGUI_DefaltTextMenuCreate(&text_page, text_items, "文本菜单", TEXT_ITEM_BASE);
    text_page.item_cap = TEXT_ITEM_CAP;         /* 运行时增删必须有容量声明 */
    text_page.vtbl     = &text_page_vtable;
    text_page.focus_idx = 0;

    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &text_page};
}

/* ==================================================================
 * 动态文本菜单（条目数组由框架 malloc / free）
 *   · 每条条目的 label 仍必须常驻（框架只存指针）→ 用静态标签池
 *   · 条目结构体本身由框架拷贝进自己的数组，可用局部变量
 * ================================================================== */
#define DYN_DATA_MAX    8       /* 最多 8 个"数据项" */
#define DYN_BASE_ITEMS  3       /* 顶部固定功能条目：新增/删除/重填 */

static ESGUI_MenuPage_T dyn_page;
static char             dyn_label[DYN_DATA_MAX][20];
static uint8_t          dyn_data_num;
static ESGUI_PopWindow_T dyn_msg_popup;

static ESGUI_MenuAction_T dyn_show_msg(const char *msg)
{
    ESGUI_DefaultMessagePopWindowCreate(&dyn_msg_popup, msg, 112, 56, 1);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &dyn_msg_popup};
}

static ESGUI_MenuAction_T dyn_data_enter(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    (void)arg;
    return dyn_show_msg("动态数据项\n增删自动扩容");
}

/* 追加一个数据项：label 从静态池取，arg 转成可读的"数据项编号" */
static bool dyn_add_data(ESGUI_MenuPage_T *page)
{
    if (dyn_data_num >= DYN_DATA_MAX) {
        return false;
    }
    snprintf(dyn_label[dyn_data_num], sizeof(dyn_label[0]), "数据项%d", dyn_data_num + 1);
    ESGUI_MenuItem_T it = {0, 0, dyn_label[dyn_data_num], ESGUI_NULL,
                           dyn_data_enter, (void *)(uintptr_t)(dyn_data_num + 1)};
    if (!ESGUI_MenuPageAddItem(page, &it)) {
        return false;
    }
    dyn_data_num++;
    return true;
}

static ESGUI_MenuAction_T dyn_enter_add(ESGUI_MenuPage_T *page, void *arg)
{
    (void)arg;
    if (!dyn_add_data(page)) {
        return dyn_show_msg("数据项已满\n上限 8");
    }
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

static ESGUI_MenuAction_T dyn_enter_del(ESGUI_MenuPage_T *page, void *arg)
{
    (void)arg;
    if (page->item_num <= DYN_BASE_ITEMS) {
        return dyn_show_msg("没有可删的\n数据项");
    }
    if (!ESGUI_MenuPageRemoveItem(page, (eui_uint16_t)(page->item_num - 1))) {
        return dyn_show_msg("删除失败");
    }
    if (dyn_data_num > 0) dyn_data_num--;
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

static ESGUI_MenuAction_T dyn_enter_reset(ESGUI_MenuPage_T *page, void *arg)
{
    (void)arg;
    /* 先删到只剩功能条目，再重新填 3 个数据项（顺带验证缩容/扩容路径） */
    while (page->item_num > DYN_BASE_ITEMS) {
        if (!ESGUI_MenuPageRemoveItem(page, (eui_uint16_t)(page->item_num - 1))) break;
    }
    dyn_data_num = 0;
    for (uint8_t i = 0; i < 3; i++) {
        (void)dyn_add_data(page);
    }
    return (ESGUI_MenuAction_T){ACT_REFRESH, ESGUI_NULL};
}

ESGUI_MenuAction_T test_text_dynamic_page_create(void)
{
    /* 每次进入都重新 Create：动态菜单 Pop 时条目数组已被 free，不能复用 */
    if (!ESGUI_DynamicTextMenuCreate(&dyn_page, "动态菜单", 2)) {
        return dyn_show_msg("动态菜单\n创建失败");
    }
    dyn_data_num = 0;

    /* 顶部 3 条功能条目：第一次 AddItem 会覆盖创建时的空占位条目 */
    ESGUI_MenuItem_T it_add = {0, 0, "新增数据项", ESGUI_NULL, dyn_enter_add, ESGUI_NULL};
    ESGUI_MenuItem_T it_del = {0, 0, "删除数据项", ESGUI_NULL, dyn_enter_del, ESGUI_NULL};
    ESGUI_MenuItem_T it_rst = {0, 0, "重填 3 项", ESGUI_NULL, dyn_enter_reset, ESGUI_NULL};
    ESGUI_MenuPageAddItem(&dyn_page, &it_add);      /* 覆盖占位条目 */
    ESGUI_MenuPageAddItem(&dyn_page, &it_del);
    ESGUI_MenuPageAddItem(&dyn_page, &it_rst);

    for (uint8_t i = 0; i < 3; i++) {               /* 初始 3 个数据项 */
        (void)dyn_add_data(&dyn_page);
    }
    dyn_page.focus_idx = 0;

    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &dyn_page};
}
