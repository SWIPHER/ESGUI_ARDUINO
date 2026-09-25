import sys

with open('lib/ESGUI/ESGUI_Menu.c', 'r') as f:
    content = f.read()

old = '''    /* 打开弹窗时，停止底层页面的所有动画，防止动画池满导致状态异常 */
    if (emc->menu_depth > 0) {
        ESGUI_MenuPage_T *page = emc->page_stack[emc->menu_depth - 1];
        if (page && page->items) {
            anim_stop_all(&page->items[0].x);
            anim_stop_all(&page->items[0].y);
        }
    }'''

new = '''    /* 打开弹窗时，停止底层页面的所有动画，防止动画池满导致状态异常 */
    if (emc->menu_depth > 0) {
        ESGUI_MenuPage_T *page = emc->page_stack[emc->menu_depth - 1];
        if (page && page->items) {
            anim_stop_all(&page->items[0].x);
            anim_stop_all(&page->items[0].y);
            /* 停止3D菜单的其他动画变量 */
            ESGUI_DEFAULT_3D_MENU_DAT *dat = (ESGUI_DEFAULT_3D_MENU_DAT *)page->draw_data;
            if (dat) {
                anim_stop_all(&dat->progress_bar_per);
                anim_stop_all(&dat->box_permille);
                anim_stop_all(&dat->label_anim_y);
            }
        }
    }'''

if old in content:
    content = content.replace(old, new)
    with open('lib/ESGUI/ESGUI_Menu.c', 'w') as f:
        f.write(content)
    print('OK')
else:
    print('NOT FOUND')
