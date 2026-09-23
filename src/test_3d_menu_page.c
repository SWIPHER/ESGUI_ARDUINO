/*
 * test_3d_menu_page.c —— 3D 线框菜单测试页
 *
 * 覆盖：ESGUI_3D 线框渲染（模型 / 变换 / 定点透视投影，零浮点）
 *       + 3D 菜单布局（模型面积归一化自动缩放、焦点模型绕 Z 轴持续旋转、
 *         上下分界线与标签、顶部进度条、焦点框生长动画）。
 *
 * ★ 模型坐标是"用户直觉坐标"：x 向右、y 朝屏幕里（深度）、z 向上，单位任意，
 *   菜单会按包围盒面积自动缩放，所以这里用 ±100 的整数尺度即可。
 */
#include "test_3d_menu_page.h"

#include <stdio.h>

#include "ESGUI.h"
#include "ESGUI_PageDefaltVtbl.h"
#include "ESGUI_3D.h"

/* ==================== 线框模型 ==================== */

/* 立方体：8 顶点 12 边 */
static const ESGUI_3DPoint_T cube_pts[] = {
    {-100, -100, -100}, { 100, -100, -100}, { 100, -100,  100}, {-100, -100,  100},
    {-100,  100, -100}, { 100,  100, -100}, { 100,  100,  100}, {-100,  100,  100},
};
static const ESGUI_3DEdge_T cube_edges[] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},     /* 后面 */
    {4, 5}, {5, 6}, {6, 7}, {7, 4},     /* 前面 */
    {0, 4}, {1, 5}, {2, 6}, {3, 7},     /* 侧棱 */
};
static const ESGUI_3D_T model_cube = {
    .point_list = cube_pts,
    .num_points = ESGUI_ITEM_NUM_COUNT(cube_pts),
    .edge_list  = cube_edges,
    .num_edges  = ESGUI_ITEM_NUM_COUNT(cube_edges),
};

/* 四棱锥（金字塔）：5 顶点 8 边 */
static const ESGUI_3DPoint_T pyr_pts[] = {
    {-100, -100, -100}, { 100, -100, -100}, { 100,  100, -100}, {-100,  100, -100},
    {0, 0, 150},
};
static const ESGUI_3DEdge_T pyr_edges[] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {0, 4}, {1, 4}, {2, 4}, {3, 4},
};
static const ESGUI_3D_T model_pyramid = {
    .point_list = pyr_pts,
    .num_points = ESGUI_ITEM_NUM_COUNT(pyr_pts),
    .edge_list  = pyr_edges,
    .num_edges  = ESGUI_ITEM_NUM_COUNT(pyr_edges),
};

/* 三棱柱：6 顶点 9 边 */
static const ESGUI_3DPoint_T prism_pts[] = {
    {-90, -80, -100}, { 90, -80, -100}, {0, -80, 90},
    {-90,  80, -100}, { 90,  80, -100}, {0,  80, 90},
};
static const ESGUI_3DEdge_T prism_edges[] = {
    {0, 1}, {1, 2}, {2, 0},
    {3, 4}, {4, 5}, {5, 3},
    {0, 3}, {1, 4}, {2, 5},
};
static const ESGUI_3D_T model_prism = {
    .point_list = prism_pts,
    .num_points = ESGUI_ITEM_NUM_COUNT(prism_pts),
    .edge_list  = prism_edges,
    .num_edges  = ESGUI_ITEM_NUM_COUNT(prism_edges),
};

/* 八面体：6 顶点 12 边 */
static const ESGUI_3DPoint_T octa_pts[] = {
    { 140, 0, 0}, {-140, 0, 0}, {0,  140, 0}, {0, -140, 0}, {0, 0,  140}, {0, 0, -140},
};
static const ESGUI_3DEdge_T octa_edges[] = {
    {0, 2}, {0, 3}, {0, 4}, {0, 5},
    {1, 2}, {1, 3}, {1, 4}, {1, 5},
    {2, 4}, {4, 3}, {3, 5}, {5, 2},
};
static const ESGUI_3D_T model_octahedron = {
    .point_list = octa_pts,
    .num_points = ESGUI_ITEM_NUM_COUNT(octa_pts),
    .edge_list  = octa_edges,
    .num_edges  = ESGUI_ITEM_NUM_COUNT(octa_edges),
};

/* 三棱锥（四面体）：4 顶点 6 边 */
static const ESGUI_3DPoint_T tetra_pts[] = {
    {-120, -120, -100}, { 120, -120, -100}, {0, 120, -100}, {0, 0, 150},
};
static const ESGUI_3DEdge_T tetra_edges[] = {
    {0, 1}, {1, 2}, {2, 0}, {0, 3}, {1, 3}, {2, 3},
};
static const ESGUI_3D_T model_tetrahedron = {
    .point_list = tetra_pts,
    .num_points = ESGUI_ITEM_NUM_COUNT(tetra_pts),
    .edge_list  = tetra_edges,
    .num_edges  = ESGUI_ITEM_NUM_COUNT(tetra_edges),
};

/* ==================== 3D 菜单页面 ==================== */

static ESGUI_MenuPage_T   d3_page;
static ESGUI_PopWindow_T  d3_msg_popup;

/* ★ 弹窗不拷贝消息文本，只存指针 → 拼好的文本必须放静态缓冲 */
static char d3_msg_buf[40];

static ESGUI_MenuAction_T d3_item_enter(ESGUI_MenuPage_T *page, void *arg)
{
    (void)page;
    snprintf(d3_msg_buf, sizeof(d3_msg_buf), "选中模型\n%s", (const char *)arg);
    ESGUI_DefaultMessagePopWindowCreate(&d3_msg_popup, d3_msg_buf, 216, 110, 1);
    return (ESGUI_MenuAction_T){ACT_SHOW_POPUP, &d3_msg_popup};
}

static ESGUI_MenuItem_T d3_items[] = {
    {0, 0, "立方体", &model_cube,        d3_item_enter, (void *)"立方体"},
    {0, 0, "四棱锥", &model_pyramid,     d3_item_enter, (void *)"四棱锥"},
    {0, 0, "三棱柱", &model_prism,       d3_item_enter, (void *)"三棱柱"},
    {0, 0, "八面体", &model_octahedron,  d3_item_enter, (void *)"八面体"},
    {0, 0, "三棱锥", &model_tetrahedron, d3_item_enter, (void *)"三棱锥"},
};

ESGUI_MenuAction_T test_3d_menu_page_create(void)
{
    ESGUI_Default3DMenuCreate(&d3_page, "3D菜单", d3_items,
                              ESGUI_ITEM_NUM_COUNT(d3_items));
    d3_page.focus_idx = 0;
    return (ESGUI_MenuAction_T){ACT_PUSH_PAGE, &d3_page};
}
