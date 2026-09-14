/*
 * touch_input.c —— 把 CST816D 的"触点序列"翻译成 ESGUI 能懂的事件
 *
 * 判定规则（阈值都可调，见下面 4 个宏）：
 *   手指按下 → 记录起点；按着时每滑过 STEP 像素就排一个方向事件；
 *   抬手时：位移 < SWIPE_MIN 且没发过长按 → 轻点（EVT_CLICKED）；
 *           按住 ≥ LONG_PRESS 且没怎么动      → 已经提前发过 EVT_KEY_BACK；
 *           已经按格滑过                       → 不再补发（剩余零头忽略）。
 *
 * ★ 为什么两个真实事件之间要插一个 EVT_NONE：
 *   框架有"同向按键节流 + 连击加速"（ESGUI.c → FeedKey_impl）：
 *   连续两次相同事件间隔 < repeat_delay_ms 时，第二次会被丢弃。
 *   中间插一个 EVT_NONE 会把 last_event 复位 → 快速连滑的每一格都算数。
 */
#include "touch_input.h"
#include "touch_cst816d.h"
#include <string.h>                 /* memset */

/* ==================== 手感参数（想调手感就改这里） ==================== */
#define TOUCH_EVT_QUEUE     8       /* 一次滑动最多排多少个事件（= 一次最多翻几格） */
#define TOUCH_STEP_PX       20      /* 滑过多少像素算"一格"（≈ 一条菜单的高度） */
#define TOUCH_SWIPE_MIN_PX  24      /* 位移超过它才判定为"滑动"；小于它就是"轻点" */
#define TOUCH_LONG_PRESS_MS 600     /* 按住多久算长按（= 返回） */

/* ==================== 内部状态 ==================== */
typedef struct {
    uint8_t  down;        /* 1 = 手指当前按着 */
    uint8_t  axis;        /* 0=未定 1=竖滑 2=横滑（一次手势只认一个方向） */
    uint8_t  long_sent;   /* 长按事件是否已经发过 */
    uint16_t x0, y0;      /* 按下点 */
    uint16_t ex, ey;      /* 最近一次看到的触点（抬手时用来算总位移） */
    int      stick_x;     /* 横向步进吸附锚点 */
    int      stick_y;     /* 纵向步进吸附锚点 */
    uint32_t t_down;      /* 按下时刻 */
} gesture_t;

static gesture_t s_g;
static ESGUI_EventCode_t s_q[TOUCH_EVT_QUEUE];
static uint8_t s_q_head, s_q_tail;
static uint8_t s_gap;     /* 1 = 上一轮发过真实事件，本轮先回 EVT_NONE */

/* ==================== 小工具 ==================== */
static int abs_i(int v) { return (v < 0) ? -v : v; }

static void q_reset(void) { s_q_head = s_q_tail = 0; }

static bool q_push(ESGUI_EventCode_t e)        /* false = 队列满（本次先不排，下轮再试） */
{
    uint8_t n = (uint8_t)((s_q_head + 1) % TOUCH_EVT_QUEUE);
    if (n == s_q_tail) return false;
    s_q[s_q_head] = e;
    s_q_head = n;
    return true;
}

static ESGUI_EventCode_t q_pop(void)
{
    ESGUI_EventCode_t e;
    if (s_q_head == s_q_tail) return EVT_NONE;
    e = s_q[s_q_tail];
    s_q_tail = (uint8_t)((s_q_tail + 1) % TOUCH_EVT_QUEUE);
    return e;
}
/* ==================== 接口实现 ==================== */
void touch_input_init(void)
{
    memset(&s_g, 0, sizeof(s_g));
    q_reset();
    s_gap = 0;
    touch_drv_init();                       /* I2C + 复位 + 探测 0x15（见 §1.7） */
}

/* 采一帧触点并推进状态机（可能往队列里排 0~N 个事件）
 * ★ 每一轮轮询都必须调用它：漏采样会让"快速滑动"丢格
 *   （滑动位移是按采样点累计的，漏一帧就少算一段距离） */
static void touch_sample_and_update(uint32_t now_ms)
{
    touch_state_t st;

    /* 读一次触点；I2C 失败就当"本次没有输入"，不改任何状态 */
    if (!touch_drv_read(&st)) {
        return;
    }

    if (st.fingers > 0) {
        /* ---------------- 手指按着 ---------------- */
        if (!s_g.down) {                                 /* 按下沿：记录起点 */
            s_g.down      = 1;
            s_g.axis      = 0;
            s_g.long_sent = 0;
            s_g.x0 = s_g.ex = st.x;
            s_g.y0 = s_g.ey = st.y;
            s_g.stick_x = (int)st.x;
            s_g.stick_y = (int)st.y;
            s_g.t_down  = now_ms;
        } else {                                         /* 按住中：累计位移 */
            s_g.ex = st.x;
            s_g.ey = st.y;

            /* 位移够大就锁定方向（一次手势只做一个方向，斜着划不会乱跳） */
            if (s_g.axis == 0 &&
                (abs_i((int)st.x - (int)s_g.x0) >= TOUCH_SWIPE_MIN_PX ||
                 abs_i((int)st.y - (int)s_g.y0) >= TOUCH_SWIPE_MIN_PX)) {
                s_g.axis = (abs_i((int)st.y - (int)s_g.y0) >=
                            abs_i((int)st.x - (int)s_g.x0)) ? 1 : 2;
                s_g.stick_x = (int)s_g.x0;               /* 锚点回到按下点：从按下点开始数格 */
                s_g.stick_y = (int)s_g.y0;
            }

            if (s_g.axis == 1) {                         /* 竖滑 → 上/下 */
                int dy = (int)st.y - s_g.stick_y;
                while (dy >= TOUCH_STEP_PX) {
                    if (!q_push(EVT_KEY_DOWN)) break;    /* 队列满就下轮继续 */
                    s_g.stick_y += TOUCH_STEP_PX;
                    dy -= TOUCH_STEP_PX;
                }
                while (dy <= -TOUCH_STEP_PX) {
                    if (!q_push(EVT_KEY_UP)) break;
                    s_g.stick_y -= TOUCH_STEP_PX;
                    dy += TOUCH_STEP_PX;
                }
            } else if (s_g.axis == 2) {                  /* 横滑 → 右/左 */
                int dx = (int)st.x - s_g.stick_x;
                while (dx >= TOUCH_STEP_PX) {
                    if (!q_push(EVT_KEY_RIGHT)) break;
                    s_g.stick_x += TOUCH_STEP_PX;
                    dx -= TOUCH_STEP_PX;
                }
                while (dx <= -TOUCH_STEP_PX) {
                    if (!q_push(EVT_KEY_LEFT)) break;
                    s_g.stick_x -= TOUCH_STEP_PX;
                    dx += TOUCH_STEP_PX;
                }
            } else if (!s_g.long_sent &&
                       (uint32_t)(now_ms - s_g.t_down) >= TOUCH_LONG_PRESS_MS) {
                /* 按住不动够久了：立刻发"返回"，不用等抬手 */
                s_g.long_sent = 1;
                q_push(EVT_KEY_BACK);
            }
        }
    } else if (s_g.down) {
        /* ---------------- 抬手沿 ---------------- */
        int dx, dy, adx, ady;

        /* CST816D 抬手后会锁存最后位置：用它兜住"两帧之间甩出去"的极快滑动。
         * 有的批次抬手后坐标会清零，所以加了 (x||y) 判断。 */
        if (st.x != 0 || st.y != 0) {
            s_g.ex = st.x;
            s_g.ey = st.y;
        }

        dx  = (int)s_g.ex - (int)s_g.x0;
        dy  = (int)s_g.ey - (int)s_g.y0;
        adx = abs_i(dx);
        ady = abs_i(dy);

        s_g.down = 0;

        if (s_g.long_sent) {
            /* 长按已经处理过：抬手不再产生事件 */
        } else if (s_g.axis != 0) {
            /* 滑动已经按格发完了：剩余不足一格的零头忽略 */
        } else if (adx < TOUCH_SWIPE_MIN_PX && ady < TOUCH_SWIPE_MIN_PX) {
            q_push(EVT_CLICKED);                     /* 轻点 = 确定 */
        } else {
            /* 位移够大但方向没锁定（按下→抬手只采到一帧的甩动）：按方向补一格 */
            if (ady >= adx) q_push((dy > 0) ? EVT_KEY_DOWN : EVT_KEY_UP);
            else            q_push((dx > 0) ? EVT_KEY_RIGHT : EVT_KEY_LEFT);
        }
    }

}

/* 对外接口：每轮先采样，再"最多每两个轮询吐一个事件"给框架
 * （中间那个空轮询是为了复位框架的同向按键节流，见文件头注释） */
ESGUI_EventCode_t touch_input_poll(uint32_t now_ms)
{
    ESGUI_EventCode_t e;

    touch_sample_and_update(now_ms);          /* ① 每轮都采样，绝不漏位移 */

    if (s_gap) {                              /* ② 上一轮刚发过 → 本轮回 EVT_NONE */
        s_gap = 0;
        return EVT_NONE;
    }

    e = q_pop();                              /* ③ 吐一个（没有就是 EVT_NONE） */
    if (e != EVT_NONE) {
        s_gap = 1;
    }
    return e;
}