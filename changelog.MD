# 更新日志（Changelog）

ESP32-S3 + ESGUI + 触摸 测试工程的改动记录（写法参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)）。

记录约定：

- 按「修复 / 变更 / 新增 / 文档」分类，条目里带上**宏名 + 文件 + 行号**，方便直接定位；
- 「未发布」= 已写进工作区但尚未提交的改动（可用 `git status` 对照）；
- 每轮改动末尾给出**实测验证结果**（编译产物占用、脚本输出），避免"改完不知道对不对"。

当前状态：工作区改动基于提交 `4542e97 添加了大文字显示`。

---

## [未发布] 2026-09-24 —— 焦点框居中修复 + 思源等宽 26px 真字库

### 修复 ★本轮重点

#### 1. 选中焦点框没框正：白框偏左上，文字在框里偏右下（下边还会切掉一条字）

**现象**：菜单 / 列表里被选中的那一项，白框看着比文字偏左上，文字没在框里居中；行高收紧后框下边
还会把汉字的最后一两行像素切掉。

**根因**（框架 `lib/ESGUI/ESGUI_PageDefaltVtbl.c` 的绘制约定）：

| # | 事实 | 后果 |
| --- | --- | --- |
| 1 | 框画在 **容器左缘**（菜单里 `x=0`、列表弹窗里 `x=window_x`） | 文字却从 `ESGUI_TEXT_MARGIN_X`(8) 处开始画 → 框**偏左**，左右留白 8 : 1 |
| 2 | 框顶对齐 **行顶**（`focus_box_y` / `items[0].y`） | 字墨要比行顶低 `(ascent − base_line)` 才开始 → 框**偏上** |
| 3 | 框高 = `pd->font_height` = 行高，而字墨高 ≈ 行高 | 上边空 9px、下边 −2px，**字被下边切掉** |

原行为（偏移全 0）实测留白（`tools/check_focus_box.py`）：

| 样本 | 左 | 右 | 上 | 下 |
| --- | --- | --- | --- | --- |
| 触摸测试 | 8 | 1 | 9 | **−2**（越界） |

**改法**：不硬编码，而是给框架加两个 `#ifndef` 保护的偏移宏（默认 `0` = 保持框架原行为，
向后兼容），再由 `platformio.ini` 覆盖取值。

```c
/* lib/ESGUI/ESGUI_DefaultConfig.h:160 / :173 */
#ifndef ESGUI_FOCUS_BOX_OFF_X
#define ESGUI_FOCUS_BOX_OFF_X    0   /* 正 = 右移；文本焦点框水平微调 */
#endif
#ifndef ESGUI_FOCUS_BOX_OFF_Y
#define ESGUI_FOCUS_BOX_OFF_Y    0   /* 正 = 下移；文本焦点框垂直微调 */
#endif
```

```ini
; platformio.ini:84-93
-DESGUI_FOCUS_BOX_OFF_X=4
-DESGUI_FOCUS_BOX_OFF_Y=5
```

推导依据（当前字库思源等宽 26px：`ascent 31` / `base_line 23` / 字面高约 24 / 行高 31）：

```
水平 = TEXT_MARGIN_X(8) + 字形左留白(≈1) − FOCUS_BOX_PAD_X(8)/2 = 4~5
垂直 = (ascent 31 − base_line 23) + 字面高 24/2 − 行高 31/2 ≈ 5.5 → 取 5
```

**落地点：`ESGUI_WidgetTextFocusBox` 全部 7 个调用点都按需补偏移**
（`grep -n 'WidgetTextFocusBox' lib/ESGUI/ESGUI_PageDefaltVtbl.c`）：

| 行号 | 函数 | 水平 | 垂直 | 说明 |
| --- | --- | --- | --- | --- |
| 1028 | `esgui_text_menu_defalt_on_draw` | ✅ `OFF_X` | ✅ `OFF_Y` | 文本菜单（原来 `x` 写死 0，最主要的一处） |
| 2608 | `esgui_default_message_popwindow_on_draw` | — | ✅ | 消息弹窗 OK 按钮（框本来就按文字宽画，水平不动） |
| 2821 | `esgui_default_message_scroll_title_popwindow_on_draw` | — | ✅ | 滚动标题消息弹窗 OK 按钮 |
| 3065 | `esgui_default_bool_popwindow_on_draw` | — | ✅ | 布尔弹窗「是否」按钮 |
| 3189 | `esgui_default_bool_scroll_title_popwindow_on_draw` | — | ✅ | 滚动标题布尔弹窗「是否」按钮 |
| 3940 | `esgui_default_text_list_popwindow_on_draw` | ✅ | ✅ | 文本列表弹窗（列表项与菜单同构） |
| 4128 | `esgui_default_text_list_scroll_title_popwindow_on_draw` | ✅ | ✅ | 滚动标题文本列表弹窗 |

> 未受影响：位图菜单的**四角焦点框**（`ESGUI_WidgetBmpFocusBox`，框的是缩略图不是文字）、
> 3D 菜单的**线框焦点框**（由 `ESGUI_3D_MENU_FOCUS_MARGIN` 控制）、绘图页里那个**空框控件示例**
> （`src/test_draw_page.c:329`，框里没有文字）。

**改后实测**（`python3 tools/check_focus_box.py 首页 文本菜单 触摸测试 设置 3D菜单`）：

| 样本 | 左 | 右 | 上 | 下 |
| --- | --- | --- | --- | --- |
| 首页 / 设置 / 文本菜单 / 3D菜单 | **5** | **5** | 4 | 2~3 |
| 触摸测试 | 4 | 5 | 4 | 3 |

**已知限制**：带下伸部的字母（`g y p j q`）字墨比行高还高，而框架框高 = 行高，下边**必然**装不下
（只能二选一）。当前界面全是汉字 / 数字 / 大写，按汉字居中取 5 最合适；若日后界面以英文小写为主，
把 `OFF_Y` 调到 ~8 可让下伸部也不越界（代价是汉字略偏上）。

### 变更

#### 1. 字库换成「思源等宽 SC 26px」真字库（开源可再分发）

上一条提交用的是系统 `PingFang.ttc` 30px，本轮改用随工程分发的 `myfont/SourceHanMono-Regular.ttc`
（`--index 0` = Source Han Mono SC）26px：

| 项 | 旧（PingFang 30px） | 现（思源等宽 26px） | 影响 |
| --- | --- | --- | --- |
| 字库来源 | `/System/Library/Fonts/PingFang.ttc` index 2（系统字体，不便再分发） | `myfont/SourceHanMono-Regular.ttc` index 0 | 可随固件开源分发 |
| 渲染字号 | 30px | **26px** | 字略小、笔画更清，同屏仍是 6 行 |
| `line_height` | 33 | **31** | 菜单行距 39 → **37**（行间更紧凑） |
| `base_line` | 26 | **23** | 决定字墨落点，是本轮焦点框偏移的计算依据 |
| 位图数据 | 413,379 B（≈404 KB） | **292,151 B（≈285 KB）** | Flash 占用降约 118 KB |
| 字宽 | 比例字宽 | **全等宽**（26px 步进） | 数字 / 表格对齐整齐，每行仍是 7 个汉字 |

连带同步（漏一项就会出现"滑动跳两行 / 字被裁 / 框不居中"）：

| 联动项 | 旧 → 新 | 位置 |
| --- | --- | --- |
| `TOUCH_STEP_PX` | 38 → **36** | `src/touch_input.c`（= 行高 31 + 条目间距 6 ≈ 37） |
| `TOUCH_SWIPE_MIN_PX` | 36 → **34** | 同上 |
| 焦点框偏移 | 0 → **4 / 5** | `platformio.ini:92-93`（本轮修复，见上） |
| 绘图页排版 | 竖线区 / 棋盘格 / 控件段起点按新行高微调 | `src/test_draw_page.c`（`fy`、`pat_align`、`pat_dither`） |
| 字库数据 | 重新生成 | `src/font_big.c`(285 KB 数据) / `src/font_big.h`（头注释更新） |

> 其余测试页坐标**不用改**：它们内部统一用 `#define LH (ESGUI_DEFAULT_FONT.line_height)` 排版，
> 换字号自动跟随。

#### 2. `tools/gen_font_big.py` 默认值与功能

- 默认字体 / 字号：`PingFang.ttc 30px` → **`myfont/SourceHanMono-Regular.ttc` `--index 0` 26px**
  （不带参数执行即为"复现当前字库"）；
- 新增 `--chars '※℃✓'`：临时把额外字符加进字库（长期加字改脚本里的 `EXTRA_CHARS`）；
- 用法注释与 `--size/--font/--index/--list-faces` 说明更新，并指向 `change_font.md`。

### 新增

| 内容 | 位置 | 说明 |
| --- | --- | --- |
| 焦点框偏移宏 | `lib/ESGUI/ESGUI_DefaultConfig.h:160`(`OFF_X`) / `:173`(`OFF_Y`) | `#ifndef` 保护，默认 0 = 框架原行为；本工程在 `platformio.ini` 覆盖为 4 / 5 |
| 焦点框居中考校脚本 | `tools/check_focus_box.py` | 读 `platformio.ini` 的 `OFF_X/OFF_Y` + `font_big.c` 度量，算出框内四边留白并判定是否居中；换字体 / 字号后必跑 |
| 换字体 / 调字号指南 | `change_font.md` | 字号实测对照表、换字体流程、字库格式规范、加字符、排查表、当前值速查 |
| Python 缓存忽略 | `.gitignore` | 追加 `__pycache__/`（工具脚本 import 时产生） |

`check_focus_box.py` 用法：

```bash
python3 tools/check_focus_box.py                  # 默认样本字（汉字/数字/字母）
python3 tools/check_focus_box.py 设置 玩家1 3D菜单  # 换成界面里的实际文字
```

判定规则：左右留白差 ≤ 1、上下留白差 ≤ 3 视作居中；任一边为负 = 字被框切到；带下伸部字母导致的
下方越界会单独说明、不算失败。

### 文档

| 文件 | 改动 |
| --- | --- |
| `README.md` | 《调参指南 三、其它视觉参数》新增 **「选中白框」没框正** 小节（根因 + 取值 + 自测命令 + 下伸部说明） |
| `README.md` | 工具清单新增 `check_focus_box.py`；FAQ 新增「滑动一格跳两行」条目（指向 `TOUCH_STEP_PX`）；开头字号 / 清晰度段落、`gen_font_big.py --list-faces` 示例改为思源等宽 SC |
| `change_font.md` | 新增《改完字号必须同步的地方》里补「焦点框相对文字的位置」一行；排查表补「白框没框正」症状；当前值速查补焦点框偏移 |
| `changelog.MD` | 本文件（新建） |

### 验证（本轮实测）

```bash
~/.platformio/penv/bin/pio run          # exit=0
```

| 检查项 | 结果 |
| --- | --- |
| 编译 | **exit=0**；仅 2 条 TFT_eSPI 自带 `TOUCH_CS pin not defined` 警告（与本改动无关，触摸走 CST816D/I2C） |
| RAM / Flash | 17.0%（55,632 B）/ 12.2%（797,360 B，含 285 KB 字库） |
| 宏是否生效 | 编译产物 `.pio/build/esp32-s3-devkitc-1/idedata.json` 中确认 `ESGUI_FOCUS_BOX_OFF_X=4`、`ESGUI_FOCUS_BOX_OFF_Y=5` |
| 焦点框居中 | `python3 tools/check_focus_box.py 首页 文本菜单 触摸测试 设置 3D菜单` → **全 OK**（左5/右5、上4/下2~3） |
| 界面字符覆盖 | `python3 tools/check_font_chars.py --scan` → 全绿（无缺字） |

### 变更文件清单

| 文件 | 状态 | 说明 |
| --- | --- | --- |
| `lib/ESGUI/ESGUI_DefaultConfig.h` | 改（+25） | 新增 `ESGUI_FOCUS_BOX_OFF_X` / `_OFF_Y`（默认 0 = 框架原行为） |
| `lib/ESGUI/ESGUI_PageDefaltVtbl.c` | 改（+11 / −10） | 7 处焦点框绘制点按需应用偏移 |
| `platformio.ini` | 改（+11） | `OFF_X=4` / `OFF_Y=5` + 推导注释 |
| `src/font_big.c` / `src/font_big.h` | 改 | 重新生成（思源等宽 26px，位图 292,151 B） |
| `src/touch_input.c` | 改 | `TOUCH_STEP_PX` 38 → 36、`TOUCH_SWIPE_MIN_PX` 36 → 34 |
| `src/test_draw_page.c` | 改 | 按新行高微调排版（`fy`、`pat_align` 竖线区、`pat_dither` 棋盘格） |
| `tools/gen_font_big.py` | 改 | 默认字体 / 字号改为思源等宽 26px；新增 `--chars` |
| `tools/check_focus_box.py` | **新增** | 焦点框居中考校脚本 |
| `change_font.md` | **新增** | 换字体 / 调字号指南 |
| `changelog.MD` | **新增** | 本文件 |
| `README.md` | 改（+64 / −19） | 「选中白框」小节、工具清单、FAQ、字号段落同步 |
| `.gitignore` | 改（+3） | 追加 `__pycache__/` |
| `myfont/SourceHanMono-Regular.ttc` | 新增（未跟踪，22 MB） | 字库生成源；可选提交或改用 Git LFS |

### 升级 / 复现须知

1. **用对 pio**：系统 PATH 里的 `pio` 是旧版 6.1.18，请用 `~/.platformio/penv/bin/pio`。
2. **换字库必做三件事**（漏一项就会出现"滑动跳两行 / 框不居中"）：
   ① `python3 tools/gen_font_big.py --size N` 重新生成 →
   ② 同步 `src/touch_input.c` 的 `TOUCH_STEP_PX`（= `line_height` + 6）→
   ③ `python3 tools/check_focus_box.py` 复校并微调 `ESGUI_FOCUS_BOX_OFF_X/Y`。
3. **只想回退焦点框改动**：删掉 `platformio.ini` 里那两行 `-DESGUI_FOCUS_BOX_OFF_*`
   （宏默认 0），或调小取值即可，框架源码不必再改。
4. **只想回退整轮字体改动**：
   `git checkout HEAD -- src/font_big.c src/font_big.h src/touch_input.c src/test_draw_page.c tools/gen_font_big.py platformio.ini`
5. `myfont/`（22 MB）未提交也能编译（字库已生成进 `src/font_big.c`），只是日后无法复现生成过程；
   要保留就 `git add myfont/`（或配 Git LFS）。

### 待验证 / 下一步

- [ ] 烧录后肉眼确认：菜单 / 列表 / 弹窗里**白框与文字居中**（本轮修复的目标手感）
- [ ] 确认滑动一格正好翻一行（`TOUCH_STEP_PX 36` vs 行距 37）
- [ ] 确认二值化后的思源等宽 26px 汉字笔画清晰、无粘连（如偏糊可 `--threshold 120` 重生成）

---

## 已提交历史（对照）

| 提交 | 日期 | 说明 |
| --- | --- | --- |
| `4542e97` | 2026-09-23 | 添加了大文字显示（PingFang 30px 真字库 + 逻辑安全区 / 坐标换算） |
| `e2ffeee` | 2026-09-23 | 修改了触摸位置不正确的问题 |
| `78f5b0c` | 2026-09-23 | 增加了修改指南 |
| `f4da175` | 2026-09-23 | 每个 UI 模块的测试页面 |
| `c091674` | 2026-09-15 | Initial commit |


