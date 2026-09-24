//
// Created by E_LJF on 2026/6/4.
//

#include "ESGUI_Menu.h"
#include "string.h"
#if ESGUI_ENABLE_MENU_RUNTIME_ITEMS
#include <stdlib.h>   /* 运行时增删（动态菜单）：realloc */
#endif

/**
 * @brief 初始化菜单控制器
 * @param emc 菜单控制器实例指针
 * @return 无
 *
 * 作用：将控制器结构体清零，设置默认按键重复延迟为 300ms，
 * 并使能菜单运行标志。
 */
void ESGUI_MenuCtrlInit(ESGUI_MenuCtrl_T *emc) {
    if (emc == ESGUI_NULL) return;
    memset(emc,0,sizeof(*emc));
    emc->repeat_delay_ms = 300;
    emc->running_en = 1;
    emc->anim_running = 0;
}

/**
 * @brief 将新页面压入菜单栈
 * @param emc  菜单控制器实例指针
 * @param page 待压入的页面指针
 * @return 无
 *
 * 说明：仅当当前栈深度小于 ESGUI_MAX_MENU_DEPTH 且菜单处于运行状态时
 * 才执行压栈操作。压栈后自动标记需要刷新。
 */
void ESGUI_MenuCtrlPushPage(ESGUI_MenuCtrl_T *emc, ESGUI_MenuPage_T *page) {
    if (emc == ESGUI_NULL || page == ESGUI_NULL) return;

    if ((emc->menu_depth < ESGUI_MAX_MENU_DEPTH) && (emc->running_en == 1)) {
        emc->page_stack[emc->menu_depth] = page;
        emc->menu_depth++;
        emc->need_refresh = 1;
    }
}

/**
 * @brief 弹出栈顶页面（返回上一级）
 * @param emc 菜单控制器实例指针
 * @return 无
 *
 * 说明：仅当栈深度大于 1 时执行（保留至少一个页面）。
 * 出栈前调用当前页面的 on_destroy 虚函数进行资源释放。
 */
void ESGUI_MenuCtrlPopPage(ESGUI_MenuCtrl_T *emc) {
    if (emc == ESGUI_NULL || emc->running_en == 0) return;

    if (emc->menu_depth > 1) {
        ESGUI_MenuPage_T *page = emc->page_stack[emc->menu_depth - 1];  //暂存当前页面指针

        if (page->vtbl->on_destroy){
            page->vtbl->on_destroy(page);
        }  //调用析构

        emc->menu_depth --;
        emc->need_refresh = 1;
    }
}

/**
 * @brief 显示模态弹窗（压入弹窗栈）
 * @param emc   菜单控制器实例指针
 * @param popup 弹窗页面指针
 * @return 无
 *
 * 说明：支持多个弹窗叠放：新弹窗压入栈顶后，输入事件优先路由到栈顶弹窗，
 * 关闭栈顶弹窗后自动回到下一层弹窗。同一弹窗指针不重复压栈。
 */
void ESGUI_MenuCtrlShowPopWindow(ESGUI_MenuCtrl_T *emc, ESGUI_PopWindow_T *popup) {
    if (emc == ESGUI_NULL || popup == ESGUI_NULL || emc->running_en == 0) return;

    /* 防重复：同一弹窗已在栈中则不重复压入 */
    for (eui_uint8_t i = 0; i < emc->pop_depth; i++) {
        if (emc->pop_stack[i] == popup) return;
    }
    if (emc->pop_depth >= ESGUI_MAX_POPUP_DEPTH) return;

    emc->pop_stack[emc->pop_depth] = popup;
    emc->pop_depth++;
    emc->pop_window_en = 1;
    emc->need_refresh = 1;
}

/**
 * @brief 关闭栈顶模态弹窗
 * @param emc 菜单控制器实例指针
 * @return 无
 *
 * 说明：销毁栈顶弹窗并出栈；若栈中仍有下层弹窗，则其成为新的输入焦点。
 */
void ESGUI_MenuCtrlClosePopWindow(ESGUI_MenuCtrl_T *emc) {
    if (emc == ESGUI_NULL || emc->running_en == 0) return;

    if (emc->pop_depth > 0) {
        ESGUI_PopWindow_T *top = emc->pop_stack[emc->pop_depth - 1];
        if (top && top->vtbl->on_destroy) {
            top->vtbl->on_destroy((ESGUI_MenuPage_T*)top);
        }
        emc->pop_stack[emc->pop_depth - 1] = ESGUI_NULL;
        emc->pop_depth--;
    }
    emc->pop_window_en = (emc->pop_depth > 0) ? 1 : 0;
    emc->need_refresh = 1;

}

/**
 * @brief 执行页面返回的动作指令
 * @param emc 菜单控制器实例指针
 * @param act 动作指令结构体指针
 * @return 无
 *
 * 说明：根据 act->act 枚举值分发到对应的栈操作（压栈/出栈）、
 * 弹窗控制、刷新标记或退出应用等动作。
 */
void ESGUI_MenuCtrlHandleAction(ESGUI_MenuCtrl_T *emc, ESGUI_MenuAction_T *act)
{
    if (emc == ESGUI_NULL || act == ESGUI_NULL || emc->running_en == 0) return;

    switch(act->act) {
        case ACT_NONE:
            break;

        case ACT_PUSH_PAGE:
            if(act->param) ESGUI_MenuCtrlPushPage(emc, (ESGUI_MenuPage_T*)act->param);
            break;

        case ACT_POP_PAGE:
            /* 新增：栈底保护，防止单页面时误触发延迟销毁重建 */
            if (emc->menu_depth <= 1) {
                break;
            }
            if (emc->page_stack[emc->menu_depth-1]->vtbl->on_page_chenge) {
                emc->page_stack[emc->menu_depth-1]->vtbl->on_page_chenge(emc->page_stack[emc->menu_depth-1], act);
                ESGUI_MenuCtrlPreparePopPage(emc);
            }
            else{
                ESGUI_MenuCtrlPopPage(emc);
            }
            break;

        case ACT_SHOW_POPUP:
            if(act->param) {
                ESGUI_MenuCtrlShowPopWindow(emc, (ESGUI_PopWindow_T*)act->param);
            }
            break;

        case ACT_CLOSE_POPUP:
            if (emc->pop_depth == 0) break;
            {
                ESGUI_PopWindow_T *top_popup = emc->pop_stack[emc->pop_depth - 1];
                /* 弹窗关闭动作一开始即重排底层页面：弹窗操作可能改动了条目特殊
                 * 宽度/arg 等，而布局缓存只在 recenter 中刷新。提前重排可保证
                 * 关闭过渡动画期间的渲染帧即采用新布局，避免焦点框从旧宽度跳变。
                 * on_relayout 为 NULL（未启用运行时条目增删/页面无实现）时跳过。 */
                if (top_popup->vtbl->on_page_chenge) {
                    top_popup->vtbl->on_page_chenge((ESGUI_MenuPage_T*)top_popup, act);
                    ESGUI_MenuCtrlPreparePopPage(emc);
                }
                else {
                    ESGUI_MenuCtrlClosePopWindow(emc);
                }
            }
            break;

        case ACT_REFRESH:
            emc->need_refresh = 1;
            break;

        case ACT_EXIT_APP:
            emc->running_en = 0;  // 主循环根据这个退出
            break;

        default: break;
    }
}


/**
 * @brief 准备弹出页面（延迟销毁，页面保持存活直到动画完成）
 * @param emc 菜单控制器实例指针
 * @return true=成功进入延迟状态；false=条件不满足
 */
bool ESGUI_MenuCtrlPreparePopPage(ESGUI_MenuCtrl_T *emc) {
    if (emc == ESGUI_NULL || emc->running_en == 0) return false;
    if (emc->menu_depth == 0) return false;
    if (emc->pending_pop) return false;  // 已有待处理动作，防止重复

    emc->pending_pop = 1;
    emc->need_refresh = 1;

    if (emc->pop_window_en && emc->pop_depth > 0) {
        emc->pending_destroy_page = (ESGUI_MenuPage_T*)emc->pop_stack[emc->pop_depth - 1];
        return true;
    }

    emc->pending_destroy_page = emc->page_stack[emc->menu_depth - 1];
    return true;
}

/**
 * @brief 执行待处理的出栈操作（动画完成后调用）
 * @param emc 菜单控制器实例指针
 */
void ESGUI_MenuCtrlExecPendingPop(ESGUI_MenuCtrl_T *emc) {
    if (emc == ESGUI_NULL || !emc->pending_pop) return;

    ESGUI_MenuPage_T *page = emc->pending_destroy_page;
    bool was_popup = (emc->pop_window_en && emc->pop_depth > 0 &&
                      (ESGUI_MenuPage_T*)emc->pop_stack[emc->pop_depth - 1] == page);

    if (page && page->vtbl && page->vtbl->on_destroy) {
        page->vtbl->on_destroy(page);
    }

    if (emc->pop_window_en && emc->pop_depth > 0) {
        ESGUI_MenuCtrlClosePopWindow(emc);
    }
    else {
        ESGUI_MenuCtrlPopPage(emc);
    }

    if (!was_popup && emc->menu_depth > 0) {
        ESGUI_MenuPage_T *new_top = emc->page_stack[emc->menu_depth - 1];
        if (new_top && new_top->vtbl && new_top->vtbl->on_focus_change) {
            new_top->vtbl->on_focus_change(new_top, new_top->focus_idx, new_top->focus_idx);
        }
    }

    emc->pending_pop = 0;
    emc->pending_done = 0;
}

/**
 * @brief 入队延迟动作
 * @param emc 菜单控制器实例指针
 * @param act 待排队的动作
 * @return 无
 * @note  排队动作将在"当前动作（如弹窗滑出动画）完成"后由 ESGUI_Tick
 *        依次取出执行。典型用法：弹窗 on_enter 中先把"关闭下层弹窗"入队，
 *        再返回"关闭当前弹窗"动作，实现一次按键连续关闭多层弹窗。
 *        队列满或动作为 ACT_NONE 时忽略。
 */
void ESGUI_MenuCtrlQueueAction(ESGUI_MenuCtrl_T *emc, ESGUI_MenuAction_T act) {
    if (emc == ESGUI_NULL || act.act == ACT_NONE) return;
    if (emc->pending_act_count >= ESGUI_MENU_PENDING_ACT_QUEUE_SIZE) return;
    emc->pending_act_queue[emc->pending_act_tail] = act;
    emc->pending_act_tail = (eui_uint8_t)((emc->pending_act_tail + 1) % ESGUI_MENU_PENDING_ACT_QUEUE_SIZE);
    emc->pending_act_count++;
}

/**
 * @brief 出队执行延迟动作队列
 * @param emc 菜单控制器实例指针
 * @return 无
 * @note  由 ESGUI_Tick 在"当前无待处理动作"时调用：循环取出队头动作执行；
 *        若某动作触发了 must_complete 动画（进入 pending_push/pending_pop
 *        延迟流程）则停止，待动画完成后的下一个 Tick 再次调用续取。
 */
void ESGUI_MenuCtrlExecQueuedAction(ESGUI_MenuCtrl_T *emc) {
    if (emc == ESGUI_NULL || emc->running_en == 0) return;
    while (emc->pending_act_count > 0) {
        if (emc->pending_push || emc->pending_pop) break;   /* 前一动作进入延迟流程，等动画完成 */
        ESGUI_MenuAction_T act = emc->pending_act_queue[emc->pending_act_head];
        emc->pending_act_head = (eui_uint8_t)((emc->pending_act_head + 1) % ESGUI_MENU_PENDING_ACT_QUEUE_SIZE);
        emc->pending_act_count--;
        ESGUI_MenuCtrlHandleAction(emc, &act);
    }
}


/* ==================== 运行时条目增删 ====================
 * 由 ESGUI_ENABLE_MENU_RUNTIME_ITEMS 控制（0=整套剔除，静态条目系统不受影响）。
 * 动态菜单（item_auto_expand=1）支持容量自适应：
 *   增加/插入容量不足 → realloc 翻倍扩容；删除后容量 > 2×条目数 → 缩容。
 * 静态数组（item_auto_expand=0）绝不 realloc。
 */
#if ESGUI_ENABLE_MENU_RUNTIME_ITEMS

/**
 * @brief 条目结构变化后的统一收尾：通知页面重排布局
 * @param page      页面指针
 * @param old_focus 变化前的焦点索引
 * @param new_focus 变化后的焦点索引
 */
static void menu_page_relayout(ESGUI_MenuPage_T *page, eui_uint16_t old_focus, eui_uint16_t new_focus)
{
    if (page == ESGUI_NULL || page->vtbl == ESGUI_NULL) return;
    if (page->vtbl->on_relayout) {
        page->vtbl->on_relayout(page, old_focus, new_focus);
    }
}

/**
 * @brief 扩容条目数组（容量翻倍）
 * @param page 页面指针
 * @return true=扩容成功；false=未启用自动扩容 / realloc 失败 / 已达上限
 * @note  仅动态菜单（item_auto_expand=1）生效；新扩容区域清零。
 */
static bool menu_page_grow(ESGUI_MenuPage_T *page)
{
    if (page == ESGUI_NULL || page->items == ESGUI_NULL) return false;
    if (page->item_auto_expand == 0 || page->item_cap == 0) return false;   /* 静态数组/未启用 */
    eui_uint32_t new_cap = (eui_uint32_t)page->item_cap * 2u;
    if (new_cap > 0xFFFFu) new_cap = 0xFFFFu;                               /* item_cap 为 u16，封顶 */
    if (new_cap <= page->item_cap) return false;                            /* 已达上限 */
    ESGUI_MenuItem_T *p = (ESGUI_MenuItem_T *)ESGUI_REALLOC(page->items,
                            (size_t)new_cap * sizeof(ESGUI_MenuItem_T));
    if (p == ESGUI_NULL) return false;                                      /* 失败保持原状态 */
    memset(&p[page->item_cap], 0, ((size_t)new_cap - page->item_cap) * sizeof(ESGUI_MenuItem_T));
    page->items = p;
    page->item_cap = (eui_uint16_t)new_cap;
    return true;
}

/**
 * @brief 缩容条目数组（容量 > 2×条目数 时缩到当前条目数，保底 4 条）
 * @param page 页面指针
 * @note  仅动态菜单（item_auto_expand=1）生效；realloc 失败时保持原容量，不影响功能。
 */
static void menu_page_shrink(ESGUI_MenuPage_T *page)
{
    if (page == ESGUI_NULL || page->items == ESGUI_NULL) return;
    if (page->item_auto_expand == 0) return;
    if (page->item_cap <= 4) return;                                        /* 保底容量，避免缩了又扩的抖动 */
    if (page->item_cap <= (eui_uint16_t)(page->item_num * 2u)) return;      /* 未超 2 倍，不缩 */
    eui_uint16_t new_cap = (page->item_num < 4) ? 4 : page->item_num;
    ESGUI_MenuItem_T *p = (ESGUI_MenuItem_T *)ESGUI_REALLOC(page->items,
                            (size_t)new_cap * sizeof(ESGUI_MenuItem_T));
    if (p == ESGUI_NULL) return;                                            /* 失败保持原容量 */
    page->items = p;
    page->item_cap = new_cap;
}

/**
 * @brief 判断动态菜单是否处于"仅含空占位条目"状态
 * @param page 页面指针
 * @return true=刚由 ESGUI_DynamicTextMenuCreate 创建（item_auto_expand=1 且仅剩 1 条空 label）
 * @note  占位条目用于维持 item_num>=1 框架不变量；首次增删时直接覆盖占位，
 *        避免出现"空条目 + 真实条目"两个条目。
 */
static bool menu_page_has_placeholder(ESGUI_MenuPage_T *page)
{
    if (page == ESGUI_NULL || page->items == ESGUI_NULL) return false;
    if (page->item_auto_expand == 0 || page->item_num != 1) return false;
    const char *lbl = page->items[0].label;
    return (lbl == ESGUI_NULL || lbl[0] == '\0');
}

bool ESGUI_MenuPageAddItem(ESGUI_MenuPage_T *page, const ESGUI_MenuItem_T *item)
{
    if (page == ESGUI_NULL || item == ESGUI_NULL || page->items == ESGUI_NULL) return false;
    if (menu_page_has_placeholder(page)) {
        page->items[0] = *item;                                             /* 覆盖占位，而不是追加 */
        menu_page_relayout(page, page->focus_idx, page->focus_idx);
        return true;
    }
    if (page->item_num >= page->item_cap) {
        if (!menu_page_grow(page)) return false;                            /* 容量不足：静态返回 false，动态自动扩容 */
    }
    page->items[page->item_num] = *item;
    page->item_num++;
    menu_page_relayout(page, page->focus_idx, page->focus_idx);
    return true;
}

bool ESGUI_MenuPageInsertItem(ESGUI_MenuPage_T *page, eui_uint16_t idx, const ESGUI_MenuItem_T *item)
{
    if (page == ESGUI_NULL || item == ESGUI_NULL || page->items == ESGUI_NULL) return false;
    if (menu_page_has_placeholder(page)) {
        page->items[0] = *item;                                             /* 覆盖占位（此时仅 1 条，idx 无意义） */
        menu_page_relayout(page, page->focus_idx, page->focus_idx);
        return true;
    }
    if (idx > page->item_num) idx = page->item_num;                         /* 越界视为尾部插入 */
    if (page->item_num >= page->item_cap) {
        if (!menu_page_grow(page)) return false;                            /* 容量不足：静态返回 false，动态自动扩容 */
    }
    eui_uint16_t old_focus = page->focus_idx;
    memmove(&page->items[idx + 1], &page->items[idx],
            (size_t)(page->item_num - idx) * sizeof(ESGUI_MenuItem_T));
    page->items[idx] = *item;
    page->item_num++;
    /* 插入位置在焦点前/焦点处：焦点后移一位，保持同一逻辑条目被选中 */
    if (idx <= old_focus) page->focus_idx = old_focus + 1;
    menu_page_relayout(page, old_focus, page->focus_idx);
    return true;
}

bool ESGUI_MenuPageRemoveItem(ESGUI_MenuPage_T *page, eui_uint16_t idx)
{
    if (page == ESGUI_NULL || page->items == ESGUI_NULL) return false;
    if (page->item_num <= 1 || idx >= page->item_num) return false;         /* 至少保留 1 条，避免空菜单 */
    eui_uint16_t old_focus = page->focus_idx;
    if (idx < old_focus) {
        page->focus_idx = old_focus - 1;                                    /* 删除项在焦点前：焦点顺移 */
    } else if (idx == old_focus) {
        /* 删除焦点项：焦点落在同下标的后继条目上；若删除的是最后一条则回退 */
        page->focus_idx = old_focus;
        if (page->focus_idx >= page->item_num - 1) {
            page->focus_idx = page->item_num - 2;
        }
    }
    /* else：删除项在焦点后，焦点不变 */
    memmove(&page->items[idx], &page->items[idx + 1],
            (size_t)(page->item_num - 1 - idx) * sizeof(ESGUI_MenuItem_T));
    page->item_num--;
    menu_page_shrink(page);                                                 /* 动态菜单：容量超 2 倍时缩容 */
    menu_page_relayout(page, old_focus, page->focus_idx);
    return true;
}

#endif /* ESGUI_ENABLE_MENU_RUNTIME_ITEMS */
