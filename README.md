# ESGUI + 触摸 测试工程（ESP32-S3 / TFT_eSPI / CST816D）

把 ESGUI 里**每一个 UI 组件**、触摸、显示都做成可点开的测试页面。
硬件：Waveshare ESP32-S3-Touch-LCD-1.83（面板 240x284 圆角屏 + CST816D 触摸）。

## 字号 / 安全区（为什么内容是"缩小居中"的）

这块面板**四角是圆角**，贴边的字会被圆角切掉；同时 16px 字在小屏上偏小。工程的做法是
**逻辑分辨率缩小 + 驱动整数倍放大**（见 `src/tft_drv.h`）：

```
面板 240x284
   └─ 逻辑区 112x128，每个逻辑像素放大 2x2 送出屏 → 实际显示区 224x256，居中
        （左右各留 8 列、上下各留 14 行安全边距 → 圆角永远切不到内容）
```

好处：**字、图形、焦点框、进度条一起变大 2 倍**（16px 字变 32px），触控目标变成 36px 行高，
而且不用换字库；框架的"像素级样式常量"已在 `platformio.ini` 里按 1/2 设置，
所以放大后观感与原来 240x280@1x 一致。

想再调大小，只改这两个地方（改完重新编译）：

| 想要 | `src/tft_drv.h` | 提示 |
| --- | --- | --- |
| 字更小 / 内容更多 | `TFT_ZOOM 1`、`ESGUI_LOGIC_W 224`、`ESGUI_LOGIC_H 264` | 一屏约 13 行 |
| 当前（推荐） | `TFT_ZOOM 2`、`ESGUI_LOGIC_W 112`、`ESGUI_LOGIC_H 128` | 一屏约 6 行，字 32px |
| 字更大 | `TFT_ZOOM 3`、`ESGUI_LOGIC_W 72`、`ESGUI_LOGIC_H 88` | 一屏只剩 4 行 |

> 逻辑尺寸必须是 8 的倍数；`ESGUI_LOGIC_* × TFT_ZOOM` 不能超过面板（头文件里有 `#error` 兜底）。
> 改 `TFT_ZOOM` 后记得同步 `src/touch_input.c` 的滑动阈值（一格 ≈ 一行的高度）。

## 注意事项：内置字库里**没有全角标点**

`lib/ESGUI/Font/eui_test_font.c` 只有 ASCII（0x20~0x7E）+ 3701 个汉字，
**`：（）；，。—…` 这类全角标点全部缺失**（缺字在屏上只会留一个空档）。
所以工程里所有界面文字都用 ASCII 标点（`:` `(` `)` `,` `;` `.`）。
写完新界面文字后用工具自查一遍：

```bash
python3 tools/check_font_chars.py --scan     # 列出"字符串里用到但字库没有"的字符
python3 tools/font_metrics.py '覆盖层:已添加' # 看某串字的实际像素宽度（估算一行能放几个字）
```

## 编译 / 烧录

```bash
# 用 PlatformIO Core（若系统里 pio 是 6.1.x 的旧版，请直接用官方安装的这一个）
~/.platformio/penv/bin/pio run -t upload && ~/.platformio/penv/bin/pio device monitor
```

VS Code 里直接用 PlatformIO 插件的 Build / Upload / Monitor 也可以。

## 操作方式（触摸手势 → ESGUI 事件）

| 手势 | 事件 | 效果 |
| --- | --- | --- |
| 轻点 | `EVT_CLICKED` | 确定 / 进入条目 |
| 上下滑动（每 32px 一格） | `EVT_KEY_UP / EVT_KEY_DOWN` | 移动焦点、切换图案 |
| 左右滑动 | `EVT_KEY_LEFT / EVT_KEY_RIGHT` | 与上下同义（列表页）/ 左右选值 |
| 长按 600ms | `EVT_KEY_BACK` | 返回上一页 / 关闭弹窗 |

阈值都在 `src/touch_input.c` 头部（`TOUCH_STEP_PX` / `TOUCH_SWIPE_MIN_PX` / `TOUCH_LONG_PRESS_MS`）；
**改手感、改帧率的方法见下面的《调参指南》。**

## 调参指南：显示帧率 / 触摸灵敏度

### 一、显示帧率

ESGUI 不是"固定帧率刷屏"，而是 **节拍（Tick）+ 按需重绘**：只有动画页（有 `anim` 在跑）或
请求过 `ACT_REFRESH` 的页面才整帧重绘，静态页面几乎不刷屏。所以分三层看：

#### 1) 节拍频率（决定帧率上限 & 输入延迟）

| 位置 | 当前值 | 说明 |
| --- | --- | --- |
| `src/esgui_port.c:129` | `>= 10`（ms） | **单线程模式**下每 10ms 才 `ESGUI_Tick()` 一次 → 100Hz 上限 |
| `src/esgui_port.c:34` | `PORT_USE_UI_TASK 0` | 改成 `1`：UI 跑在 core0 独立任务，`loop()` 只做触摸（推荐，见下文"跟手度"） |
| `src/esgui_port.c:83` | `vTaskDelay(pdMS_TO_TICKS(10))` | **独立任务模式**下 UI 任务自己的节拍 |
| `src/main.cpp:29` | `delay(2)` | `loop()` 周期：单线程模式下决定触摸采样率，独立任务模式下只影响触摸采样 |

* 更顺 / 更跟手：`10` → `5`（200Hz 上限）
* 更省电 / 降 CPU：`10` → `16`（≈60Hz）或 `20`

#### 2) 单帧耗时（真正的瓶颈）

每帧要送 `224×256×2 = 114,688` 字节 RGB565（逻辑区放大后的实际显示区）：

| 手段 | 位置 | 效果 |
| --- | --- | --- |
| **提高 SPI 时钟（收益最大）** | `lib/TFT_eSPI-2.5.43/User_Setup.h:49` `SPI_FREQUENCY 40000000` → `80000000` | 40MHz ≈ 23ms/帧（≈43fps）→ 80MHz ≈ 11.5ms/帧（≈87fps）；出现花屏/噪点就退回 40M |
| 减少 SPI 事务次数 | `src/tft_drv.h:73` `ESGUI_STRIP_H 32` → `64`，**同时**把 `src/tft_drv.cpp:30` 的 `> 32 ? 32 :` 上限一起改大（否则缓冲仍是 32 行，白改） | 条带 32→64 行：每帧 `pushImage` 次数减半；缓冲 28KB → 57KB（内部 RAM 够用） |
| 整帧一次推送（最顺、无撕裂） | `src/tft_drv.h:82` `TFT_USE_FRAME_BUF 1`，并把 `ESGUI_STRIP_H` 设为 `ESGUI_LOGIC_H` | 整帧放 PSRAM（114KB）一次 `pushImage`；需要板子有 PSRAM |
| 减少像素总量 | `src/tft_drv.h:46` `TFT_ZOOM 2` → `1`（逻辑改 `224×264`） | 画面更大、字变小；每帧字节数与现在相当 |
| 缩短动画时长（主观"更快"，不是帧率） | `platformio.ini` 追加 `-DESGUI_PAGE_TRANSITION_ANIM_TIME=200`（默认 350ms，见 `ESGUI_DefaultConfig.h:194`）；弹窗滑入的 400ms 写死在 `ESGUI_PageDefaltVtbl.c` | 页面切换 / 弹窗进出更快 |

> 推荐组合：`SPI_FREQUENCY 80M` + `ESGUI_STRIP_H 64`（含 `tft_drv.cpp:30` 上限）+ `PORT_USE_UI_TASK 1`。

#### 3) 实测当前帧率

进「覆盖层」页面看底部的 **`帧%lu`** 计数器（每绘制一帧 +1），秒表数 1 秒涨多少 ≈ 实际 fps；
也可以临时在 `test_draw_page.c` 的 `draw_frame()` 里加一行 `Serial.println(millis())` 打时间戳。

### 二、触摸灵敏度

手感参数全在 **`src/touch_input.c` 第 19~26 行**（4 个宏在 23~26 行；文件头就写着"想调手感就改这里"）：

| 参数 | 当前值 | 调**小**的效果 | 调**大**的效果 |
| --- | --- | --- | --- |
| `TOUCH_STEP_PX` | `32` | 更灵敏（划一点就翻行，一行菜单 36px） | 更迟钝（要划更长才翻一行） |
| `TOUCH_SWIPE_MIN_PX` | `30` | 更容易判定成"滑动" | 更难判定成滑动（轻点更"安全"） |
| `TOUCH_LONG_PRESS_MS` | `600` | 长按更快触发（返回） | 长按更难误触 |
| `TOUCH_EVT_QUEUE` | `8` | 一次快速甩动翻的格数变少 | 一次甩动可翻更多格 |

常见诉求对应改法：

* **太钝 / 划好几下才动一行** → `TOUCH_STEP_PX 32 → 22`、`TOUCH_SWIPE_MIN_PX 30 → 20`
* **太灵 / 手指一放就乱跳** → `TOUCH_STEP_PX 32 → 40`、`TOUCH_SWIPE_MIN_PX 30 → 36`
* **返回老被误触** → `TOUCH_LONG_PRESS_MS 600 → 800`

> 这些阈值是**屏幕像素**，与 UI 放大倍数绑定：改过 `tft_drv.h` 的 `TFT_ZOOM` 后，要同步把
> `TOUCH_STEP_PX` 设成"≈ 一行菜单的像素高"。

#### 跟手度（最容易被忽略的点）

单线程模式（`PORT_USE_UI_TASK 0`）下整帧绘制就在 `loop()` 里做：一帧 23ms 意味着这一帧期间
只采到一次触摸，于是"滑动轨迹很粗、容易丢格"。三种解法（任选其一）：

1. **`src/esgui_port.c:34` 改成 `PORT_USE_UI_TASK 1`（推荐）**：触摸仍留在 `loop()`（≈2ms 一次），
   UI 在 core0 独立任务里绘制 → 滑动立刻跟手；
2. 保持单线程但缩短单帧：提高 `SPI_FREQUENCY`、加大 `ESGUI_STRIP_H`（见上面帧率表）；
3. 只把 `src/main.cpp:29` 的 `delay(2)` 改成 `delay(1)`——提升有限，瓶颈是整帧绘制。

> 平台层为了保证"快速连滑每一格都生效"，在两次真实事件之间插了一个 `EVT_NONE`
> （见 `esgui_port.c` 的注释），代价是**事件输出速率 = 轮询率 ÷ 2**。想更快请用方案 1，
> 不要直接把这个间隔删掉（会被下面的框架节流丢掉）。

#### 框架层的"同向按键节流"（滑动/连按被吃掉时看这里）

* `lib/ESGUI/ESGUI_Menu.c:22`：`repeat_delay_ms = 300`（初始值）
* `lib/ESGUI/ESGUI.c:174`：同方向事件在该时间内**直接丢弃**；`:178` 每成功处理一次 −40ms（最低 50ms）；
  `:199` 未产生动作时重置回 300

本工程在平台层插了 `EVT_NONE` 复位 `last_event`，正常情况下不会误丢；若你改了投递逻辑或改用
编码器输入，可以把 300 调小、把 −40 调大。

#### 硬件层（一般不用动）

* **自动休眠**：`src/touch_cst816d.cpp` 的 `TOUCH_DISABLE_AUTO_SLEEP`（写 `0xFE=1`）——
  "过一会儿点不动"的根治点；
* **I2C 速率**：`src/touch_cst816d.h` 的 `TOUCH_I2C_FREQ 400000`；
  若触摸页的"失败"计数一直涨，可降到 `100000` 增强抗干扰。

## 首页 → 测试项对照

| 首页条目 | 覆盖的 ESGUI 能力 |
| --- | --- |
| 文本菜单 | 默认文本菜单、长文本环形滚动、特殊标记 `\x03/0`、`\x03/1`、运行时增删条目、动态菜单（malloc） |
| 图形菜单 | BMP 菜单布局/焦点框动画、GIF 动图条目、图片列表弹窗（含滚动标题版、含动图版） |
| 3D菜单 | ESGUI_3D 线框渲染、3D 菜单自动缩放、焦点模型持续旋转 |
| 弹窗集合 | 12 种默认弹窗（消息/布尔/值/文本列表/图片列表 各含滚动标题版）+ 长文本弹窗 + 三层弹窗叠放 |
| 键盘编辑 | 键盘输入弹窗（字母/数字符号页、大小写、光标、退格）、多行编辑页、EditBox |
| 绘图显示 | 13 个图案：全部 BSP 图元（SET/CLEAR/XOR）、位图（普通/反色/透明）、GIF、Widget 组件、ASCII 字表、调色板换色（`g_pal_cb`）、像素对齐自检、灰阶抖动、直写屏自检、过渡遮罩 |
| 触摸测试 | CST816D 原始坐标/手势码/触点数、采样成功率、触摸→事件计数、网格+反光十字准星+轨迹 |
| 覆盖层 | 覆盖层 Add/Remove/SetVisible、`always_dirty` 常驻刷新、7 条内置缓动曲线 + 往返无限循环 |
| 系统信息 | 两页信息（轻点翻页）：芯片/主频/内核/RAM/PSRAM/Flash、面板/逻辑分辨率/放大倍数/条带、触摸状态/采样统计 |

`绘图显示` 里的「直写屏」图案会**阻塞 4~5 秒**：它绕过 ESGUI 直接用 TFT_eSPI 画
单色轮播 / RGB 竖条 / 三色渐变 / 棋盘格+红边框+绿安全框，用来区分"屏驱动问题"还是"框架绘制问题"。

## 文件结构

```
src/
  esgui_port.c/.h      平台装配层：屏幕初始化 → ESGUI_Init → 触摸轮询 → ESGUI_Tick（唯一消费者）
  tft_drv.cpp/.h       ESGUI 1bpp 条带画布 → RGB565 →(整数倍放大+安全区居中)→ TFT_eSPI 送屏
                       附：显示自检图案、g_pal_cb 调色板回调、安全区护栏 tft_drv_cover_margins()
  touch_cst816d.cpp/.h CST816D I2C 采集层
  touch_input.c/.h     触点序列 → ESGUI 事件；并提供触点快照/采样统计给测试页
  main.cpp             setup()/loop() 两行装配
  test_home_page.c/.h  首页菜单 + 系统信息页（2 页）
  test_text_menu_page.* / test_bmp_menu_page.* / test_3d_menu_page.*
  test_popup_page.*    / test_edit_page.*
  test_draw_page.*     / test_touch_page.* / test_overlay_page.*
  test_assets.c/.h     位图/动图资源（自动生成：32x32 图标 + 24x24 缩略图 + 12 帧动图）

tools/
  gen_test_assets.py   生成 src/test_assets.c/.h
                       python3 tools/gen_test_assets.py            # 重新生成
                       python3 tools/gen_test_assets.py --preview  # 终端 ASCII 预览图标
  check_font_chars.py  检查界面字符串里的字符（汉字+标点）是否都在字库里
                       python3 tools/check_font_chars.py --scan
  font_metrics.py      查看字符度量/字符串像素宽度（排版估算用）
                       python3 tools/font_metrics.py 'ESGUI 测试'
```

## 常见问题（先看这里）

**1. 屏幕最上面 / 最下面出现红色实线 + 白色虚线（点点相连）**

这是「直写屏」自检第 4 张图案的正常画面：`1px 红边框 + 8x8 黑白棋盘格 + 绿色安全框`
—— 红实线是边框的上/下两条，白虚线是棋盘格最外圈的白格。

它**曾经会留在屏上**，因为面板 284 行、而 ESGUI 只画中间一段，逻辑区外的留边框架永不覆盖。
现在已修：自检结束时清全屏 + `esgui_flush_area()` 每帧都会补刷安全区外的留边
（`tft_drv_cover_margins()`），所以任何"绕过框架直写屏幕"的代码都不会再留残影。

**2. 画面整体偏移 / 上下不对称** → 面板可见区与 GRAM 起点不一致时，微调 `tft_drv.h` 里的
`TFT_OFFSET_X/TFT_OFFSET_Y`（现在是按"居中"自动算的；若要整体位移，改成固定值即可）。

**3. 颜色不对（红蓝互换）** → `tft_drv.cpp` 里 `tft.setSwapBytes(true)` 与 TFT_eSPI 的 RGB/BGR 设置。

**4. 界面上有字符变成空档** → 字库缺字（多半是全角标点）：
用 `python3 tools/check_font_chars.py --scan` 查出来，换成 ASCII 写法即可。

**5. 内容还是贴到圆角上** → 把安全边距放大：减小 `ESGUI_LOGIC_W/H`（例如 104x120），
`TFT_OFFSET_X/Y` 会自动重新居中；或直接在 `tft_drv.h` 里手填偏移。

**6. 滑动一格跳两行** → 改 `src/touch_input.c` 的 `TOUCH_STEP_PX`（现在 32 ≈ 一行 36px 高），
或见上文《调参指南》二、触摸灵敏度（含"跟手度"为什么不佳的说明）。

## 写测试页时的三个框架要点（实测踩到）

1. **弹窗只保存字符串指针，不拷贝文本**：给 `ESGUI_Default*PopWindowCreate` 传的消息必须是
   字面量或**静态**缓冲，栈上局部数组会在回调返回后失效。
2. **多行编辑页会先把工作缓冲清空**（内部 `ESGUI_MultiLineEditBoxInit` 会写 `buffer[0]='\0'`），
   再用 `init_text` 回填；因此 `init_text` 不能直接传工作缓冲本身，要传一份快照
   （见 `src/test_edit_page.c`）。

另外，条目/页面回调返回动作的几种用法在本工程里都有示例：`ACT_PUSH_PAGE / ACT_SHOW_POPUP /
ACT_REFRESH / ACT_POP_PAGE`；自定义虚函数表页面（`item_num = 0`，自己实现 `on_draw`/`on_input`）
见 `test_draw_page.c`、`test_touch_page.c`、`test_home_page.c`（系统信息页）。

3. **同一时刻只有第一个覆盖层能拿到画布**：`ESGUI.c` 的 `ESGUI_Tick` 里给覆盖层注入
   `render_ctx` 的循环带了个 `break`（只处理 `overlays[0]`），所以同时挂多个覆盖层时，
   第 2 个之后的 `ov->render_ctx` 一直是 NULL（画不出来）。本工程只用一个覆盖层，
   已验证可用；若你要用多个，需要先把框架里那个 `break` 去掉。
