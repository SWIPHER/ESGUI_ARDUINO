# ESGUI + 触摸 测试工程（ESP32-S3 / TFT_eSPI / CST816D）

把 ESGUI 里**每一个 UI 组件**、触摸、显示都做成可点开的测试页面。
硬件：Waveshare ESP32-S3-Touch-LCD-1.83（面板 240x284 圆角屏 + CST816D 触摸）。

## 字号 / 清晰度 / 安全区（怎么又大又清楚）

这块面板**四角是圆角**，贴边的字会被圆角切掉；同时 16px 字在小屏上偏小。
一开始试过"把 16px 点阵放大 2 倍"（TFT_ZOOM=2）——**结果是又大又糊**：像素放大不会产生新细节，
复杂汉字原本只有 1px 笔画，放大后所有笔画都变 2px、笔画间空隙被挤掉，字就"团"在一起了。

所以现在的做法是**按最终尺寸渲染的真字库 + 1:1 映射**：

```
面板 240x284
  └─ 逻辑区 216x272（1 逻辑像素 = 1 面板像素，零重采样）
       居中放置 → 左右各留 12 列、上下各留 6 行安全边距（圆角切不到内容）
  └─ 字库 src/font_big.c：思源等宽 Source Han Mono SC 26px（line_height=31，汉字步进 26px）
```

效果：26px 汉字（比框架自带 16px 明显大、比上一版 30px 略小）、一屏 6 行、每行 7 个汉字；
笔画细节保留（细笔画仍是 1px），1:1 输出锐利不打折。

> 换字体/调字号的完整方法见 **[change_font.md](change_font.md)**（含各字号实测对照表）。

### 想换字号

字库由脚本从系统字体生成，改字号只改一个参数：

```bash
python3 tools/gen_font_big.py --size 24                        # 更小（每行更多字）
python3 tools/gen_font_big.py --size 30                        # 更大
python3 tools/gen_font_big.py --size 26 --preview              # 只预览字体度量与字形，不写文件
python3 tools/gen_font_big.py --list-faces                     # 列出 .ttc 里的字体面（详表见 change_font.md）
```

脚本会打印"汉字步进 / 每行可放几个汉字 / 行距"，据此再决定要不要调逻辑分辨率和
`ESGUI_ITEM_SPACING`。**改完字号后记得同步两个地方**：

| 联动项 | 位置 | 说明 |
| --- | --- | --- |
| 逻辑分辨率（=安全边距） | `src/tft_drv.h` 的 `ESGUI_LOGIC_W/H`（保持 8 的倍数） | 字大了就适当减小，否则一屏行数太少 |
| 滑动一格阈值 | `src/touch_input.c` 的 `TOUCH_STEP_PX` | ≈ 一行的高度 = 行高 + 条目间距 |

### 想换字体/字重

默认字体是仓库里的 **思源等宽**（`myfont/SourceHanMono-Regular.ttc`，开源可再分发），
换字体只需 `--font <路径> --index <面>`；系统字体（如 PingFang）、`.ttf/.otf` 单字体都能用：

```bash
python3 tools/gen_font_big.py --list-faces --font myfont/SourceHanMono-Regular.ttc
#  index 0: ('Source Han Mono SC', 'Regular')   ← 简体面
python3 tools/gen_font_big.py --font myfont/SourceHanMono-Regular.ttc --index 0 --size 26
```

> 为什么不用"框架自带字库 + 放大"：那套 16px 字库只有 1px 笔画，放大只能得到 2px 方块。
> 真要大字号且清晰，必须重新渲染——这也是框架自带 `ESGUI Font Generator` 的思路，
> 本工程的 `tools/gen_font_big.py` 就是按同样的格式（页式位图 + FontGlyph + FontCmap）生成的，
> 可与官方工具生成的字库互换（见下文"换成你自己生成的字库"）。

## 注意事项：字库的字符覆盖 / 全角标点

本工程字库 `src/font_big.c` 由 `tools/gen_font_big.py` 生成，
字符集 = **ASCII（0x20~0x7E）+ 框架自带老字库的全部 3701 个汉字 + 21 个常用全角标点**
（`：，。、；？！（）《》【】“”‘’—…·％`）。

也就是说：**全角标点现在有了**（老字库没有，这也是之前界面文字统一改用 ASCII 标点的原因）。
换字号/换字体重新生成后，建议再自查一遍界面文字：

```bash
python3 tools/check_font_chars.py --scan          # 扫描 src/ 所有字符串字面量（用当前字库校验）
python3 tools/check_font_chars.py '测试（全角）：、；'   # 单条字符串检查
python3 tools/font_metrics.py '触摸测试 面板123,456'  # 查字符串实际像素宽度（排版估算）
```

> 缺字在屏幕上只会留一个空档，不会报错，所以新增界面文字后跑一下第一条命令最省事。
> 两个工具都会自动使用 `src/font_big.c`；要校验别的字库用 `--font <文件>`。

### 换成你自己生成的字库（官方 ESGUI Font Generator 等）

格式完全一致，所以可以互换。三步：

1. 把生成的字库 `.c/.h` 放进 `src/`（例如 `src/my_font.c/.h`，导出符号名假定为 `my_font`）；
2. 把生成脚本里用到的字段对齐成同样的写法（`Font` 里 `line_height` / `base_line` 必须与你字库的
   实际度量一致——框架的排版全部依赖这两个值）；
3. 改 `platformio.ini` 的三行（`-I src` 已有，不用动）：

```ini
    -DESGUI_DEFAULT_FONT=my_font
    -DESGUI_KEY_BOARD_FONT=my_font
    -include my_font.h
```

对照检查用：

```bash
python3 tools/font_metrics.py --font src/my_font.c 'A中'
python3 tools/check_font_chars.py --font src/my_font.c --scan
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
| 上下滑动（每 38px 一格，一行是 39px） | `EVT_KEY_UP / EVT_KEY_DOWN` | 移动焦点、切换图案 |
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

每帧要送 `216×272×2 = 117,504` 字节 RGB565（1:1 映射，没有放大开销）：

| 手段 | 位置 | 效果 |
| --- | --- | --- |
| **提高 SPI 时钟（收益最大）** | `lib/TFT_eSPI-2.5.43/User_Setup.h:49` `SPI_FREQUENCY`（工程默认 40000000，**现已提到 `80000000`**） | 80MHz ≈ 11.5ms/帧（≈87fps）；退回 40MHz ≈ 23ms/帧（≈43fps）。出现花屏/噪点就退回 `40000000` |
| 减少 SPI 事务次数 | `src/tft_drv.h:73` `ESGUI_STRIP_H 32` → `64`，**同时**把 `src/tft_drv.cpp:30` 的 `> 32 ? 32 :` 上限一起改大（否则缓冲仍是 32 行，白改） | 条带 32→64 行：每帧 `pushImage` 次数减半；缓冲 28KB → 57KB（内部 RAM 够用） |
| 整帧一次推送（最顺、无撕裂） | `src/tft_drv.h:82` `TFT_USE_FRAME_BUF 1`，并把 `ESGUI_STRIP_H` 设为 `ESGUI_LOGIC_H` | 整帧放 PSRAM（114KB）一次 `pushImage`；需要板子有 PSRAM |
| 减少像素总量（进一步提速） | `src/tft_drv.h` 的 `ESGUI_LOGIC_W/H` 调小（如 `200×256`） | 每帧字节数按比例下降，安全边距同时变大 |
| 缩短动画时长（主观"更快"，不是帧率） | `platformio.ini` 追加 `-DESGUI_PAGE_TRANSITION_ANIM_TIME=200`（默认 350ms，见 `ESGUI_DefaultConfig.h:194`）；弹窗滑入的 400ms 写死在 `ESGUI_PageDefaltVtbl.c` | 页面切换 / 弹窗进出更快 |

> 推荐组合：`SPI_FREQUENCY 80M` + `ESGUI_STRIP_H 64`（含 `tft_drv.cpp:30` 上限）+ `PORT_USE_UI_TASK 1`。

#### 3) 实测当前帧率

进「覆盖层」页面看底部的 **`帧%lu`** 计数器（每绘制一帧 +1），秒表数 1 秒涨多少 ≈ 实际 fps；
也可以临时在 `test_draw_page.c` 的 `draw_frame()` 里加一行 `Serial.println(millis())` 打时间戳。

### 二、触摸灵敏度

手感参数全在 **`src/touch_input.c` 第 19~26 行**（4 个宏在 23~26 行；文件头就写着"想调手感就改这里"）：

| 参数 | 当前值 | 调**小**的效果 | 调**大**的效果 |
| --- | --- | --- | --- |
| `TOUCH_STEP_PX` | `36` | 更灵敏（划一点就翻行；一行菜单 = 行高 31 + 间距 6 = 37px） | 更迟钝（要划更长才翻一行） |
| `TOUCH_SWIPE_MIN_PX` | `34` | 更容易判定成"滑动" | 更难判定成滑动（轻点更"安全"） |
| `TOUCH_LONG_PRESS_MS` | `600` | 长按更快触发（返回） | 长按更难误触 |
| `TOUCH_EVT_QUEUE` | `8` | 一次快速甩动翻的格数变少 | 一次甩动可翻更多格 |

常见诉求对应改法：

* **太钝 / 划好几下才动一行** → `TOUCH_STEP_PX 36 → 24`、`TOUCH_SWIPE_MIN_PX 34 → 22`
* **太灵 / 手指一放就乱跳** → `TOUCH_STEP_PX 36 → 46`、`TOUCH_SWIPE_MIN_PX 34 → 40`
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

### 三、其它视觉参数（例：3D 菜单的模型大小）

**3D 菜单模型大小**由 `ESGUI_3D_MENU_MODEL_SCALE` 控制 —— 它是"模型尺寸占焦点框尺寸的百分比"
（框架默认 65），所以**减半就是 33**。本工程已在 `platformio.ini` 里设成 33（= 默认的 0.5 倍）：

```
platformio.ini:  -DESGUI_3D_MENU_MODEL_SCALE=33
```

本屏（逻辑 216x272、字高 31）实际算出来：焦点框 189px × 33% ≈ **模型 62px**，
原来 65% 时是 123px——正好一半；`slot_w` 也一起变小，一屏可见的模型从 2.1 个变成约 3.4 个
（横向仍是"焦点居中"轮播）。

> 想核对不同取值下的尺寸，直接跑脚本（复刻了框架的公式）：
> `python3 tools/check_3d_model_size.py 33`

相关联动（想微调时看这里）：

| 想要 | 改哪个 |
| --- | --- |
| 模型再大/再小一点 | `ESGUI_3D_MENU_MODEL_SCALE`（百分比，越大越大） |
| 模型之间的横向间距 | `ESGUI_3D_MENU_ITEM_GAP`（当前 8；`slot_w = 模型尺寸 + 本值`，模型变小后间距不变，看起来会更"散"） |
| 焦点框（那个方框）大小 | `ESGUI_3D_MENU_FOCUS_MARGIN`（当前 10，**越小框越大**；它和模型大小是独立的两个量） |
| 透视强弱（立体感） | `ESGUI_3D_MENU_FOCAL` 与 `ESGUI_3D_MENU_DEPTH`（当前 40 / 20）——**只影响透视观感，不改显示尺寸**（框架会自动补偿缩放）；想改就按比例同时改这两个 |
| 焦点模型旋转速度 | `ESGUI_3D_DURATION`（毫秒/圈，框架默认 3000） |

> 上面这些宏都是 `#ifndef` 保护的，用 `platformio.ini` 的 `build_flags` 加 `-D...=值` 即可覆盖，
> 不用改框架源码；改完 `pio run -t upload` 生效。

#### "选中白框"没框正（文字在框里偏右下）

框架画白框的基准是「行顶 + 容器左缘」（菜单里 `x=0`、列表弹窗里 `x=窗口左缘`），而文字是从
`ESGUI_TEXT_MARGIN_X` 处开始画、字墨还要比行顶低 `(ascent − base_line)` 才开始 →
**框偏左上、文字显得偏右下**。原值（偏移全 0）实测：左留白 8 / 右留白 1、上留白 9 / 下留白 **−2**
（下边把字切掉 2px），正是肉眼看"没居中"的原因。

本工程用两个宏把框摆正（宏定义在 `lib/ESGUI/ESGUI_DefaultConfig.h`，默认 `0` = 框架原样，只在
`platformio.ini` 里覆盖取值）：

| 宏 | 当前值 | 作用范围 / 含义 |
| --- | --- | --- |
| `ESGUI_FOCUS_BOX_OFF_X` | `4` | 焦点框**右移**像素；用于菜单 + 文本列表弹窗（弹窗里的 OK / 是否 按钮本来就是按文字宽画的，不参与水平位移） |
| `ESGUI_FOCUS_BOX_OFF_Y` | `5` | 焦点框**下移**像素；用于**所有**文本焦点框（含弹窗 OK 按钮、是否按钮） |

摆正后的实测留白（`python3 tools/check_focus_box.py 测试 触摸测试 设置`）：**左 5 / 右 5、上 4 / 下 3**
——基本正中心。想自己量当前效果：

```
python3 tools/check_focus_box.py                 # 默认样本字
python3 tools/check_focus_box.py 设置 玩家1 3D菜单  # 换成你界面里的实际文字
```

> 取值与**字体/字号**强相关（字库的 `ascent`/`base_line` 一改就得重算）：
> 右移量 ≈ `TEXT_MARGIN_X + 字形左留白 − FOCUS_BOX_PAD_X/2`，
> 下移量 ≈ `(ascent − base_line) + 字面高/2 − 行高/2`。换字库后跑一遍脚本，
> 微调到"左右差 ≤ 1、上下差 ≤ 3"即可（详见 `change_font.md`）。

> 注意：带下伸部的字母（`g` `y` `p` `j` `q`）字墨比行高还高，而框架的框高 = 行高，
> 所以下边**必然**装不下（只能二选一）。当前界面全是汉字/数字/大写，按汉字居中取 5 最合适；
> 若你的界面以英文小写为主，把 `ESGUI_FOCUS_BOX_OFF_Y` 调到 `8` 左右可让下伸部也不越界，
> 代价是汉字看起来略偏上。

其它 UI 比例参数（条目间距、进度条宽、键盘键高、圆角等）都在 `platformio.ini` 的
"UI 缩放"那一组 build_flags 里（见本文开头《字号 / 安全区》）。

## 首页 → 测试项对照

| 首页条目 | 覆盖的 ESGUI 能力 |
| --- | --- |
| 文本菜单 | 默认文本菜单、长文本环形滚动、特殊标记 `\x03/0`、`\x03/1`、运行时增删条目、动态菜单（malloc） |
| 图形菜单 | BMP 菜单布局/焦点框动画、GIF 动图条目、图片列表弹窗（含滚动标题版、含动图版） |
| 3D菜单 | ESGUI_3D 线框渲染、3D 菜单自动缩放、焦点模型持续旋转（模型尺寸 = 默认的 0.5 倍，见《调参指南》三） |
| 弹窗集合 | 12 种默认弹窗（消息/布尔/值/文本列表/图片列表 各含滚动标题版）+ 长文本弹窗 + 三层弹窗叠放 |
| 键盘编辑 | 键盘输入弹窗（字母/数字符号页、大小写、光标、退格）、多行编辑页、EditBox |
| 绘图显示 | 13 个图案：全部 BSP 图元（SET/CLEAR/XOR）、位图（普通/反色/透明）、GIF、Widget 组件、ASCII 字表（0x20~0x67）、调色板换色（`g_pal_cb`）、像素对齐自检、灰阶抖动、直写屏自检、过渡遮罩 |
| 触摸测试 | CST816D 原始坐标/手势码/触点数（同时显示 **面板** 与 **逻辑** 两组坐标）、采样成功率、触摸→事件计数、网格+反色十字准星+轨迹 |
| 覆盖层 | 覆盖层 Add/Remove/SetVisible、`always_dirty` 常驻刷新、7 条内置缓动曲线 + 往返无限循环 |
| 系统信息 | 两页信息（轻点翻页）：芯片/主频/内核/RAM/PSRAM/Flash、面板/逻辑分辨率/显存、触摸状态/采样统计 |

`绘图显示` 里的「直写屏」图案会**阻塞 4~5 秒**：它绕过 ESGUI 直接用 TFT_eSPI 画
单色轮播 / RGB 竖条 / 三色渐变 / 棋盘格+红边框+绿安全框，用来区分"屏驱动问题"还是"框架绘制问题"。

## 文件结构

```
src/
  esgui_port.c/.h      平台装配层：屏幕初始化 → ESGUI_Init → 触摸轮询 → ESGUI_Tick（唯一消费者）
  tft_drv.cpp/.h       ESGUI 1bpp 条带画布 → RGB565 → TFT_eSPI 送屏（1:1 + 安全区居中）
                       附：显示自检图案、g_pal_cb 调色板回调、安全区护栏、面板⇄逻辑坐标换算
  font_big.c/.h        真·26px 字库（思源等宽；由 tools/gen_font_big.py 生成，约 285KB 数据放 Flash）
  touch_cst816d.cpp/.h CST816D I2C 采集层（含可选方向镜像）
  touch_input.c/.h     触点序列 → ESGUI 事件；并提供触点快照/采样统计给测试页
  main.cpp             setup()/loop() 两行装配
  test_home_page.c/.h  首页菜单 + 系统信息页（2 页）
  test_text_menu_page.* / test_bmp_menu_page.* / test_3d_menu_page.*
  test_popup_page.*    / test_edit_page.*
  test_draw_page.*     / test_touch_page.* / test_overlay_page.*
  test_assets.c/.h     位图/动图资源（自动生成：48x48 图标 + 40x40 缩略图 + 12 帧 48x48 动图）

tools/
  gen_font_big.py      从 TTF/TTC 生成字库 src/font_big.c/.h（默认思源等宽 SC 26px）
                       python3 tools/gen_font_big.py --size 26        # 重新生成（详见 change_font.md）
                       python3 tools/gen_font_big.py --preview       # 终端预览字形与度量
                       python3 tools/gen_font_big.py --list-faces    # 列出 .ttc 的字体面
  gen_test_assets.py   生成 src/test_assets.c/.h（图标/缩略图/动图）
                       python3 tools/gen_test_assets.py --preview
  check_font_chars.py  校验界面字符串的字符是否都在**当前字库**里（自动指向 font_big.c）
                       python3 tools/check_font_chars.py --scan
  font_metrics.py      查字符度量 / 字符串像素宽度（排版估算）
                       python3 tools/font_metrics.py '触摸测试 面板123,456'
  check_3d_model_size.py  复刻框架 3D 菜单尺寸公式，核对模型在不同 MODEL_SCALE 下的显示尺寸
                       python3 tools/check_3d_model_size.py 33
  check_focus_box.py   核对"选中白框"相对文字是否居中（读 platformio.ini 的 OFF_X/OFF_Y + 字库度量）
                       python3 tools/check_focus_box.py 测试 触摸测试
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

**4. 界面上有字符变成空档** → 字库缺字：
`python3 tools/check_font_chars.py --scan` 会列出"字符串里用到但当前字库没有"的字符
（工具默认读 `src/font_big.c`）。新生成字库后如果加了新的界面文字，跑一下这条命令最省事。

**5. 内容贴到圆角上** → 把安全边距放大：减小 `ESGUI_LOGIC_W/H`（例如 200x256），
`TFT_OFFSET_X/Y` 会自动重新居中；或直接在 `tft_drv.h` 里手填偏移。

**6. 滑动一格跳两行** → 改 `src/touch_input.c` 的 `TOUCH_STEP_PX`（现在 36 ≈ 一行 37px 高），
或见上文《调参指南》二、触摸灵敏度（含"跟手度"为什么不佳的说明）。

**7. 触摸测试页的准星 / 轨迹和手指位置不一致**

这是"**面板坐标 vs 逻辑坐标**"的换算问题（不是触摸不准，也不影响菜单操作——ESGUI 是焦点式
交互，不按坐标命中，所以只有这页看得出来）：

* 触摸芯片报的是**面板像素**：`x:0..239  y:0..283`；
* 画布画的是**逻辑像素**：`216x272`；驱动送屏时做映射
  `panel = TFT_OFFSET_X/Y + logic × TFT_ZOOM`（当前 `12/6` + `×1`）。
* 把面板坐标**直接**画到画布上，位置就"多一个偏移"（若把 TFT_ZOOM 调成 2 还会再放大 2 倍）。

现在触摸页已改用 `tft_panel2logic_x()/tft_panel2logic_y()`（在 `src/tft_drv.h`）换算，
并同时显示两组读数：`面板 X,Y`（芯片原始值）与 `逻辑 x,y`（画布上十字所在的位置）。
自己写"按坐标命中某个区域"的页面时，也要走这两个函数；只判断"位移量/阈值"（滑动分格、
长按不动）时不用换算，用面板像素即可。

校验办法：屏幕上有每 40 面板像素一条的网格线，竖线下方标了刻度数字（`40 / 80 / … / 200`）——
手指压在那条线上时，读数应接近该数值，准星也应落在线上。当前 `TFT_ZOOM = 1`，
面板↔逻辑是 1:1 映射（只差固定偏移），所以**不存在量化误差**。

若发现是**整轴反向**（手指在左上、读数却在右下），那是面板贴合方向问题：
打开 `src/touch_cst816d.h` 里的 `TOUCH_MIRROR_X` / `TOUCH_MIRROR_Y`（置 1）即可，
滑动方向判定会一起跟着变正确。

**8. 字变大了但"不清晰"（笔画糊成一团）**

这是**像素放大**的固有限制：把 16px 点阵拉成 2 倍，笔画从 1px 变 2px、笔画之间的空隙被挤掉，
复杂汉字就糊了——放大不会产生新细节。本工程已经改成正确做法：
**用 `tools/gen_font_big.py` 按最终尺寸（当前 26px）渲染真字库 + `TFT_ZOOM=1` 做 1:1 映射**。
换字体/调字号的完整流程见 **[change_font.md](change_font.md)**。

如果你自己改了字号又觉得糊，检查两点：

1. 是不是又去调了 `TFT_ZOOM`（应该保持 1；它只用于"没有大字号字库时应急放大"）；
2. 新字库的 `line_height` / `base_line` 是否与渲染尺寸匹配（`gen_font_big.py` 会自动算；
   用官方生成器时请核对这两个字段，框架的排版全靠它们）。

想看渲染质量，直接用 `--preview` 在终端里看字形点阵：

```bash
python3 tools/gen_font_big.py --size 30 --preview
```

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
