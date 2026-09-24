# 更新日志（Changelog）

ESP32-S3 + ESGUI + 触摸 测试工程的改动记录（写法参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)）。

**记录约定**：
- 按「修复 / 变更 / 新增 / 文档」分类，条目里带上**文件 + 行号**，方便直接定位；
- 「未发布」= 已写进工作区但尚未提交的改动（可用 `git status` 对照）；
- 每轮改动末尾给出**实测验证结果**，避免"改完不知道对不对"。

**当前状态**：工作区改动基于提交 `4542e97 添加了大文字显示`。

---

## [未发布] 2026-09-24 —— 焦点框居中修复 + 思源等宽 26px 真字库

### 修复 ★本轮重点

#### 1. 选中焦点框没框正：白框偏左上，文字在框里偏右下（下边还会切掉一条字）

**现象**：菜单 / 列表里被选中的那一项，白框看着比文字偏左上，文字没在框里居中；行高收紧后框下边
还会把汉字的最后一两行像素切掉。

**根因**（框架 `lib/ESGUI/ESGUI_PageDefaltVtbl.c` 的绘制约定）：

| # | 事实 | 后果 |
| :--- | :--- | :--- |
| 1 | 框画在 **容器左缘**（菜单 `x=0`、弹窗 `x=window_x`） | 文字从 `ESGUI_TEXT_MARGIN_X`(8) 开始画 → 框**偏左**，左右留白 8:1 |
| 2 | 框顶对齐 **行顶** | 字墨比行顶低 `(ascent − base_line)` 才开始 → 框**偏上** |
| 3 | 框高 = 行高，字墨高 ≈ 行高 | 上边空 9px、下边 −2px，**字被下边切掉** |

**原行为实测**（偏移全 0）：

| 样本 | 左 | 右 | 上 | 下 |
| :--- | :---: | :---: | :---: | :---: |
| 触摸测试 | 8 | 1 | 9 | **−2**（越界） |

**改法**：给框架加两个偏移宏（默认 0 向后兼容），由 `platformio.ini` 覆盖取值。

```c
/* lib/ESGUI/ESGUI_DefaultConfig.h:160 / :173 */
#ifndef ESGUI_FOCUS_BOX_OFF_X
#define ESGUI_FOCUS_BOX_OFF_X    0   /* 正 = 右移 */
#endif
#ifndef ESGUI_FOCUS_BOX_OFF_Y
#define ESGUI_FOCUS_BOX_OFF_Y    0   /* 正 = 下移 */
#endif
```

```ini
; platformio.ini:92-93
-DESGUI_FOCUS_BOX_OFF_X=4
-DESGUI_FOCUS_BOX_OFF_Y=5
```

**推导依据**（思源等宽 26px：`ascent 31` / `base_line 23` / 行高 31）：
```
水平 = TEXT_MARGIN_X(8) + 字形左留白(≈1) − PAD_X(8)/2 = 4~5
垂直 = (ascent − base_line) + 字面高/2 − 行高/2 ≈ 5.5 → 取 5
```

**7 个绘制点**（`lib/ESGUI/ESGUI_PageDefaltVtbl.c`）：

| 行号 | 函数 | 水平 | 垂直 | 说明 |
| :--- | :--- | :---: | :---: | :--- |
| 1028 | `esgui_text_menu_defalt_on_draw` | ✅ | ✅ | 文本菜单（原来 `x` 写死 0） |
| 2608 | `esgui_default_message_popwindow_on_draw` | — | ✅ | 消息弹窗 OK 按钮 |
| 2821 | `esgui_default_message_scroll_title_popwindow_on_draw` | — | ✅ | 滚动标题消息弹窗 |
| 3065 | `esgui_default_bool_popwindow_on_draw` | — | ✅ | 布尔弹窗「是否」按钮 |
| 3189 | `esgui_default_bool_scroll_title_popwindow_on_draw` | — | ✅ | 滚动标题布尔弹窗 |
| 3940 | `esgui_default_text_list_popwindow_on_draw` | ✅ | ✅ | 文本列表弹窗 |
| 4128 | `esgui_default_text_list_scroll_title_popwindow_on_draw` | ✅ | ✅ | 滚动标题文本列表弹窗 |

> **未受影响**：位图菜单四角焦点框、3D 菜单线框焦点框、绘图页空框控件。

**改后实测**：

| 样本 | 左 | 右 | 上 | 下 |
| :--- | :---: | :---: | :---: | :---: |
| 首页/设置/文本菜单/3D菜单 | **5** | **5** | 4 | 2~3 |
| 触摸测试 | 4 | 5 | 4 | 3 |

**已知限制**：带下伸部的字母（`g y p j q`）框高装不下，属正常。

---

#### 2. 消息框 OK 按钮显示不全

**现象**：点击条目弹出的消息框，OK 按钮只有上半部分显示出来。

**根因**：`esgui_default_message_popwindow_on_draw` 第 2605 行用硬编码 `-15` 计算按钮位置：
```c
int y = window->window_y + window->window_h - 15;
```
但按钮高度是 `font_height`（31px），导致按钮底部 = `110 - 15 + 31 = 126`，超出弹窗底部 110px 被裁剪。

**修复**：

| 文件 | 行号 | 改前 | 改后 |
| :--- | :---: | :--- | :--- |
| `lib/ESGUI/ESGUI_PageDefaltVtbl.c` | 2605 | `window_h - 15` | `window_h - 10 - data->font_height` |

**原理**：按钮底部固定距弹窗底部 10px，行高变化时自动跟随。

---

#### 3. 消息框宽度超出屏幕

**现象**：消息框宽度撑满整个屏幕。

**根因**：逻辑画布宽度 `ESGUI_LOGIC_W = 216` 像素，消息框宽度也用 216 像素 → 撑满全屏。

**修复**：

| 文件 | 改前 | 改后 |
| :--- | :---: | :---: |
| 6 个 `test_*.c` 里的 `ESGUI_DefaultMessagePopWindowCreate` | 宽 216 | 宽 180 |

**涉及的测试页**：`test_text_menu_page.c`（2处）、`test_3d_menu_page.c`、`test_overlay_page.c`、`test_popup_page.c`（3处）、`test_edit_page.c`、`test_bmp_menu_page.c`

---

### 变更

#### 1. 字库换成「思源等宽 SC 26px」真字库（开源可再分发）

| 项 | 旧（PingFang 30px） | 现（思源等宽 26px） | 影响 |
| :--- | :---: | :---: | :--- |
| 字库来源 | PingFang.ttc index 2（系统） | SourceHanMono-Regular.ttc index 0 | 可随固件开源分发 |
| 渲染字号 | 30px | **26px** | 字略小、笔画更清 |
| `line_height` | 33 | **31** | 行距 39 → 37 |
| `base_line` | 26 | **23** | 决定字墨落点 |
| 位图数据 | 413,379 B | **292,151 B** | Flash 降约 118 KB |
| 字宽 | 比例字宽 | **全等宽** | 数字/表格对齐整齐 |

**连带同步**（漏一项会出现"滑动跳两行/字被裁/框不居中"）：

| 联动项 | 旧 → 新 | 位置 |
| :--- | :---: | :--- |
| `TOUCH_STEP_PX` | 38 → **36** | `src/touch_input.c` |
| `TOUCH_SWIPE_MIN_PX` | 36 → **34** | `src/touch_input.c` |
| 焦点框偏移 | 0 → **4/5** | `platformio.ini:92-93` |
| 绘图页排版 | 按新行高微调 | `src/test_draw_page.c` |
| 字库数据 | 重新生成 | `src/font_big.c`（285 KB） |

---

#### 2. `tools/gen_font_big.py` 更新

- 默认字体/字号：PingFang 30px → **思源等宽 26px**
- 新增 `--chars '※℃✓'`：临时追加字符

---

### 新增

| 内容 | 位置 | 说明 |
| :--- | :--- | :--- |
| 焦点框偏移宏 | `lib/ESGUI/ESGUI_DefaultConfig.h:160/173` | 默认 0，本工程覆盖为 4/5 |
| 焦点框居中考校脚本 | `tools/check_focus_box.py` | 换字号后必跑 |
| 换字体指南 | `change_font.md` | 字号实测对照表、联动参数 |
| Python 缓存忽略 | `.gitignore` | 追加 `__pycache__/` |

---

### 文档

| 文件 | 改动 |
| :--- | :--- |
| `README.md` | 新增「选中白框」小节、工具清单、FAQ |
| `change_font.md` | 换字体流程、排查表、当前值速查 |
| `changelog.MD` | 本文件 |

---

### 验证（本轮实测）

```bash
~/.platformio/penv/bin/pio run          # exit=0
```

| 检查项 | 结果 |
| :--- | :--- |
| 编译 | **exit=0** |
| RAM / Flash | 17.0%（55,632 B）/ 12.2%（797,360 B） |
| 宏生效 | `idedata.json` 确认 OFF_X=4, OFF_Y=5 |
| 焦点框居中 | check_focus_box.py → 全 OK（左5/右5、上4/下2~3） |
| 字符覆盖 | check_font_chars.py → 全绿 |

---

### 变更文件清单

| 文件 | 状态 | 说明 |
| :--- | :--- | :--- |
| `lib/ESGUI/ESGUI_DefaultConfig.h` | 改 | 新增焦点框偏移宏 |
| `lib/ESGUI/ESGUI_PageDefaltVtbl.c` | 改 | 7处焦点框+OK按钮位置修复 |
| `platformio.ini` | 改 | OFF_X=4, OFF_Y=5 |
| `src/font_big.c/.h` | 改 | 思源等宽 26px（292 KB） |
| `src/touch_input.c` | 改 | TOUCH_STEP_PX 36, SWIPE_MIN_PX 34 |
| `src/test_draw_page.c` | 改 | 按新行高微调 |
| `src/test_*.c`（6个） | 改 | 消息框宽 216→180 |
| `tools/gen_font_big.py` | 改 | 默认值更新 |
| `tools/check_focus_box.py` | **新增** | 焦点框居中考校 |
| `change_font.md` | **新增** | 换字体指南 |
| `changelog.MD` | **新增** | 本文件 |
| `README.md` | 改 | 文档更新 |
| `.gitignore` | 改 | 追加 `__pycache__/` |
| `myfont/SourceHanMono-Regular.ttc` | 新增（未跟踪） | 字库源文件（22 MB） |

---

### 升级 / 复现须知

1. **用对 pio**：`~/.platformio/penv/bin/pio`
2. **换字库必做三件事**：重新生成 → 同步 TOUCH_STEP_PX → 跑 check_focus_box.py
3. **回退焦点框改动**：删掉 platformio.ini 里的 `-DESGUI_FOCUS_BOX_OFF_*`
4. **回退整轮字体**：`git checkout HEAD -- src/font_big.c src/touch_input.c ...`
5. `myfont/`（22 MB）可选提交或用 Git LFS

---

## 已提交历史（对照）

| 提交 | 日期 | 说明 |
| :--- | :--- | :--- |
| `4542e97` | 2026-09-23 | 添加了大文字显示（PingFang 30px 真字库） |
| `e2ffeee` | 2026-09-23 | 修改了触摸位置不正确的问题 |
| `78f5b0c` | 2026-09-23 | 增加了修改指南 |
| `f4da175` | 2026-09-23 | 每个UI模块的测试页面 |
| `c091674` | 2026-09-15 | Initial commit |

#### 4. 标题下方分割线与标题重合

**现象**：每个页面的标题下面的竖线离标题太近，有重合。

**根因**：`esgui_default_text_menu_on_draw` 第 949 行，分割线直接画在 `title_h` 位置。

**修复**：

| 文件 | 行号 | 改前 | 改后 |
| :--- | :---: | :--- | :--- |
| `lib/ESGUI/ESGUI_PageDefaltVtbl.c` | 949 | `pd->title_h` | `pd->title_h + ESGUI_TITLE_LINE_OFFSET` |

**原理**：现在分割线从 `title_h + 2`（默认值）位置开始绘制，与标题底部的间距为 2 像素。
