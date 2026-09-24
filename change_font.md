# 换字体 / 调字号指南（ESGUI 字库）

字库不是运行时可调参数，而是**编译前进来的数据**：用脚本把 TTF/TTC 渲染成 ESGUI 的 `Font`
结构 → 生成 `src/font_big.c/.h` → 编译进固件 → `platformio.ini` 里用宏指定它。

```
myfont/*.ttc / 系统字体
      │  python3 tools/gen_font_big.py --size 26 --index 0 --chars '※'
      ▼
src/font_big.c + src/font_big.h      （页式 1bpp 位图 + 字形表 + cmap）
      │  platformio.ini：-DESGUI_DEFAULT_FONT=font_big -DESGUI_KEY_BOARD_FONT=font_big -include font_big.h
      ▼
固件（字库数据放在 Flash，约 230~410 KB）
```

本工程当前用的是 **思源等宽 Source Han Mono SC（开源可再分发）26px**。

---

## 1. 只调字号（最常用）

```bash
python3 tools/gen_font_big.py --size 24     # 更小
python3 tools/gen_font_big.py --size 26     # 当前值
python3 tools/gen_font_big.py --size 30     # 更大
python3 tools/gen_font_big.py --size 26 --preview   # 只预览，不写文件（先看效果）
```

脚本会打印度量与排版估算，例如 26px：

```
度量: ascent=31 base_line=23 line_height=31  空白字形=1  位图=285.3 KB
排版估算: 汉字步进=26px（每行可放 8 个汉字），行距=31px
```

> 注意：脚本里那句"每行可放几个汉字"是按逻辑屏总宽 216px 估的；菜单还要扣掉左右边距和
> 右侧进度条（约 200px 可用），所以**实际每行少 1 个字**。

### 逻辑屏 216x272 下的实测对照（思源等宽，可直接抄）

| `--size` | line_height | 每行汉字 | 一屏行数 | 位图大小 | 说明 |
| --- | --- | --- | --- | --- | --- |
| 22 | 26 | 9 | 7 | 230 KB | 最小，信息密度最高 |
| 24 | 28 | 8 | 7 | 248 KB | 小屏友好 |
| **26** | **31** | **7** | **6** | **285 KB** | **当前值**（比 30 小一档，每行/每屏反而更多） |
| 28 | 33 | 7 | 6 | 377 KB | 与 26 差不多，字形再大一点 |
| 30 | 35 | 6 | 5 | 406 KB | 大字，一屏 5 行 |

（"每行汉字"按可用宽 ~200px 计；"一屏行数"按 272 高、行距 = line_height + `ESGUI_ITEM_SPACING`(6) 计）

### 改完字号必须同步的地方

| 联动项 | 位置 | 怎么改 |
| --- | --- | --- |
| **滑动一格阈值** | `src/touch_input.c` 的 `TOUCH_STEP_PX` / `TOUCH_SWIPE_MIN_PX` | 设成"≈ 一行的高度" = line_height + 6（26px 字 → 37 → 取 36 / 34） |
| 键盘键高 | `platformio.ini` 的 `-DESGUI_KEY_BOARD_KEY_H` | 必须 ≥ line_height，否则键上的字被裁；当前 36 够用 |
| 弹窗尺寸 | 各 `test_*_page.c` 里 `*PopWindowCreate(..., w, h, ...)` | 行高变小可多留白（不影响使用）；变大则要一起放大，否则文字被裁 |
| 逻辑分辨率（可选） | `src/tft_drv.h` 的 `ESGUI_LOGIC_W/H`（8 的倍数） | 只有字号变化很大时才需要；改完 `TFT_OFFSET_X/Y` 会自动重新居中 |
| 3D 模型大小（可选） | `-DESGUI_3D_MENU_MODEL_SCALE` | 模型大小 = 焦点框 × 百分比，焦点框随行高变化，想保持视觉大小可微调 |
| **焦点框相对文字的位置** | `-DESGUI_FOCUS_BOX_OFF_X` / `-DESGUI_FOCUS_BOX_OFF_Y` | 字库的 `ascent`/`base_line` 变了，"选中白框"就会重新变得不居中：右移 ≈ `TEXT_MARGIN_X + 字形左留白 − PAD_X/2`，下移 ≈ `(ascent − base_line) + 字面高/2 − 行高/2`。跑 `python3 tools/check_focus_box.py 测试 触摸测试` 微调到左右差 ≤1、上下差 ≤3 |

各测试页的坐标**不用改**：绘图页 / 触摸页 / 系统信息页的排版全部按"字体行高"计算
（它们内部用 `#define LH (ESGUI_DEFAULT_FONT.line_height)`），换字号会自动跟随。

```bash
# 生成 + 编译 + 烧录一条龙
python3 tools/gen_font_big.py --size 24 && ~/.platformio/penv/bin/pio run -t upload
```

---

## 2. 换字体

### 2.1 用系统字体

```bash
python3 tools/gen_font_big.py --list-faces --font /System/Library/Fonts/PingFang.ttc
#  index 0: ('PingFang HK', 'Regular')
#  index 2: ('PingFang SC', 'Regular')   ← 简体中文面
python3 tools/gen_font_big.py --font /System/Library/Fonts/PingFang.ttc --index 2 --size 26
```

### 2.2 用自带/下载的字体（推荐开源字体，便于随固件再分发）

把字体文件放到 `myfont/` 下，`--font` 写相对路径：

```bash
ls myfont/
#  SourceHanMono-Regular.ttc
python3 tools/gen_font_big.py --list-faces --font myfont/SourceHanMono-Regular.ttc
#  index 0: ('Source Han Mono SC', 'Regular')   ← 本工程当前使用
python3 tools/gen_font_big.py --font myfont/SourceHanMono-Regular.ttc --index 0 --size 26
```

常用开源 CJK 字体（都能直接喂给脚本）：

| 字体 | 特点 | 备注 |
| --- | --- | --- |
| **Source Han Mono / 思源等宽** | 汉字与 ASCII 全等宽，表格/代码风格最整齐 | 当前使用；已放在 `myfont/` |
| Source Han Sans / 思源黑体 | 无衬线黑体，屏幕可读性好 | 一个 ttc 含 SC/TC/JP/KR 多面，注意选 index |
| Noto Sans Mono CJK | Google 版思源，风格接近 | 同上 |
| 文泉驿等宽 / WenQuanYi | 体积小 | 小字号下笔画更清楚 |

- `.ttc` 是**字体集合**（一个文件里多套字），必须用 `--index` 选面；
- `.ttf` / `.otf` 是单字体，直接 `--font 路径`（不用 `--index`）；
- 字重：`--list-faces` 会显示 `Regular / Medium / Semibold…`，深色小屏上 Medium 往往更清楚。

### 2.3 换成官方 ESGUI Font Generator（或你自己写的生成器）产出的字库

格式完全一致，可以互换，三步：

1. 把产出的 `.c/.h` 放进 `src/`（例如 `src/my_font.c/.h`，导出符号名假定为 `my_font`）；
2. 核对 `Font` 结构里 `line_height` / `base_line` 与你的渲染尺寸一致（框架排版全靠这两个值）；
3. 改 `platformio.ini` 的这三行（`-I src` 已经有了）：

```ini
    -DESGUI_DEFAULT_FONT=my_font
    -DESGUI_KEY_BOARD_FONT=my_font
    -include my_font.h
```

校验：

```bash
python3 tools/font_metrics.py  --font src/my_font.c 'A中'
python3 tools/check_font_chars.py --font src/my_font.c --scan
```

---

## 3. 字库格式（自己写生成器 / 改脚本时看）

`src/font_big.c` 与框架自带 `lib/ESGUI/Font/eui_test_font.c` **格式完全一致**：

| 结构 | 关键点 |
| --- | --- |
| `Bitmap`（位图 blob） | **页式**：每页 8 行；`stride = box_w`（= 字形像素宽）字节/页；页数 = `ceil(box_h/8)`；字节内 bit n = 页内第 n 行；各字形首尾相接，`bitmap_index` = 字节偏移 |
| `FontGlyph` | `bitmap_index / adv_w / ofs_x / ofs_y / box_w / box_h`；绘制落点 `gy = 行顶 + base_line + ofs_y`，所以 `ofs_y` = 字形顶相对**基线**的偏移（负值在基线上方） |
| `FontCmap` | 第 0 项 `type=0`：ASCII 连续段（0x20~0x7E，95 个，`glyph_id_start=0`）；第 1 项 `type=1`：稀疏表（其余字符，**必须升序**，框架用二分查找） |
| `Font.line_height` | 行高（决定菜单行距、标题高度、`eui_get_text_height` 的返回值） |
| `Font.base_line` | 基线到行顶的距离（决定字形垂直落点） |

生成器（`tools/gen_font_big.py`）是怎么算这两个值的：

```python
line_height = 全部字形的最底像素 - 最顶像素     # 紧凑：不重叠也不浪费
base_line   = ascent - 最顶像素                  # ascent 来自 font.getmetrics()
ofs_y       = 字形顶 - base_line                 # 写入 FontGlyph
```
（`ascent` 是"上伸线到基线"的距离，Pillow 的 `font.getmetrics()` 给出。）

---

## 4. 字符集：加字 / 换字

- 默认字符集 = **ASCII（0x20~0x7E）+ 框架老字库的全部 3701 汉字 + 21 个常用全角标点**
  （`EXTRA_CHARS`：`：，。、；？！（）《》【】“”‘’—…·％`）；
- 临时加字（命令行）：

```bash
python3 tools/gen_font_big.py --chars '※℃✓√×°'
```

- 长期加字：改 `tools/gen_font_big.py` 里的 `EXTRA_CHARS`；字形是**从字体文件里现渲染**的，
  只要该字体包含这个字符就能生成；
- 想只保留一小部分字（省 Flash）：改 `load_charset()` 的返回值（例如只返回自己的字表），
  或把 `OLD_FONT` 换成一个更小的字库文件。

---

## 5. 校验与排查

```bash
python3 tools/gen_font_big.py --size 26 --preview   # 终端里看字形点阵（换字体后先看这个）
python3 tools/font_metrics.py '触摸测试 面板123,456' # 量字符串像素宽（估算一行放得下不）
python3 tools/check_font_chars.py --scan            # 界面文字是否有当前字库缺的字符
python3 tools/check_focus_box.py 测试 触摸测试        # 选中白框相对文字是否居中（换字体/字号后必跑）
```

| 症状 | 原因 / 处理 |
| --- | --- |
| 屏上出现空档（缺字） | 字库没这个字符：`--chars` 加上它，或换个覆盖更全的字体，再 `--scan` 复查 |
| 字糊、笔画粗成一团 | 用了"像素放大"（`TFT_ZOOM>1`）或二值化阈值太低；保持 `TFT_ZOOM=1`，必要时 `--threshold 120` 试 |
| 字上下错位 / 压到下一行 | 字库的 `line_height` / `base_line` 与实际渲染尺寸不符（用官方生成器时最容易踩）；用本脚本重新生成最省事 |
| **选中白框没框正：字在框里偏右下 / 框把字下边切掉** | 字库的 `base_line` 变了 → 焦点框偏移量过期。`check_focus_box.py` 量出留白后调 `-DESGUI_FOCUS_BOX_OFF_X`（左右）与 `-DESGUI_FOCUS_BOX_OFF_Y`（上下）；注意 `g/y/p` 等下伸部字母的墨迹比行高还高，框高 = 行高时下边必然装不下，属正常 |
| 界面文字被裁掉右边 | 字号偏大或逻辑屏太窄：减小 `--size` 或加大 `ESGUI_LOGIC_W`；也可以用 `font_metrics.py` 量一下 |
| 菜单一行放不下、出现横向滚动 | 正常行为（长文本会自动滚动）；若不想滚动就把标签写短 |
| 改完字号后滑动一格跳两行 | 忘了同步 `TOUCH_STEP_PX`（见第 1 节表格） |

---

## 6. 本工程当前值（速查）

| 项 | 值 |
| --- | --- |
| 字体 | `myfont/SourceHanMono-Regular.ttc`，`--index 0`（Source Han Mono SC） |
| 字号 / 度量 | 26px；`line_height=31`、`base_line=23`、汉字步进 26 |
| 排版 | 逻辑屏 216x272 → 每行 7 个汉字、一屏 6 行（行距 37px） |
| 字库文件 | `src/font_big.c/.h`（Flash 285 KB，符号名 `font_big`） |
| 接线 | `platformio.ini`：`-DESGUI_DEFAULT_FONT=font_big -DESGUI_KEY_BOARD_FONT=font_big -include font_big.h`（另有 `-I src`） |
| 联动 | `TOUCH_STEP_PX 36` / `TOUCH_SWIPE_MIN_PX 34`（一行 37px） |
| 焦点框微调 | `ESGUI_FOCUS_BOX_OFF_X=4` / `ESGUI_FOCUS_BOX_OFF_Y=5`（实测留白 左5/右5、上4/下3） |
| 生成命令 | `python3 tools/gen_font_big.py` （不带参数 = 复现当前字库） |

