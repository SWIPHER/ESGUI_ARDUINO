#!/usr/bin/env python3
"""检查界面字符串里的字符是否都在**当前生效的字库**里（缺字在屏上只留一个空档）。

用法：
  python3 tools/check_font_chars.py --scan            # 扫描 src/ 所有字符串字面量
  python3 tools/check_font_chars.py '要检查的字符串'    # 只检查给定字符串
  python3 tools/check_font_chars.py --font <字库.c> --scan   # 指定字库文件

默认字库：src/font_big.c（真·30px 字库）存在就用它，否则回退到框架自带的
lib/ESGUI/Font/eui_test_font.c。
"""

import glob
import os
import re
import sys

FONT_CANDIDATES = [
    "src/font_big.c",                       # 本工程生成的真·大字号字库
    "lib/ESGUI/Font/eui_test_font.c",       # 框架自带 16px 字库
]
FONT = FONT_CANDIDATES[0]


def find_font():
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            return p
    raise SystemExit("找不到字库文件：%s" % FONT_CANDIDATES)


def load_codes(path):
    """解析字库里的"稀疏码点表"（不依赖数组名，靠 .unicode_list 引用找）。

    注意：cmap 表里第一项（ASCII 连续段）的 .unicode_list 是 ESGUI_NULL，
    要跳过它，取真正指向数组的那一项。
    """
    src = open(path, encoding="utf-8", errors="ignore").read()
    name = None
    for m in re.finditer(r"\.unicode_list\s*=\s*(\w+)", src):
        if m.group(1) != "ESGUI_NULL":
            name = m.group(1)
            break
    if name is None:
        raise SystemExit("在 %s 里找不到稀疏码点表引用" % path)
    m2 = re.search(r"static const eui_uint16_t\s+%s\[\]\s*=\s*\{(.*?)\};" % re.escape(name),
                   src, re.S)
    if not m2:
        raise SystemExit("在 %s 里找不到数组 %s" % (path, name))
    return [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", m2.group(1))]


def main():
    codes = load_codes(FONT)
    chars = set(codes)
    text = sys.argv[1] if len(sys.argv) > 1 else ""
    missing = sorted({ch for ch in text if ord(ch) not in chars and ord(ch) > 0x7F})
    print("字库:", FONT)
    print("total sparse glyphs:", len(codes))
    print("cjk glyphs:", sum(1 for c in codes if 0x4E00 <= c <= 0x9FFF))
    print("missing:", "".join(missing) if missing else "(none)")


def strip_comments(src):
    """去掉 C 注释（保留字符串/字符字面量），避免注释里的引号造成误报。"""
    out = []
    i = 0
    n = len(src)
    while i < n:
        ch = src[i]
        nxt = src[i + 1] if i + 1 < n else ""
        if ch == '"' or ch == "'":
            quote = ch
            out.append(ch)
            i += 1
            while i < n:
                out.append(src[i])
                if src[i] == "\\" and i + 1 < n:
                    out.append(src[i + 1])
                    i += 2
                    continue
                if src[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if ch == "/" and nxt == "/":
            while i < n and src[i] != "\n":
                i += 1
            continue
        if ch == "/" and nxt == "*":
            i += 2
            while i + 1 < n and not (src[i] == "*" and src[i + 1] == "/"):
                i += 1
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def scan_sources():
    """扫描 src/*.c|h 里**字符串字面量**中用到的汉字，报告字体缺失的（缺字在屏幕上只留空档）。

    注释会被剥掉，所以注释里缺字不会误报。
    """
    import glob
    import re
    codes = set(load_codes(FONT))
    bad = {}
    lit_re = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
    for path in sorted(glob.glob("src/*.c")) + sorted(glob.glob("src/*.h")):
        if path.endswith("test_assets.c") or path.endswith("test_assets.h"):
            continue
        src = strip_comments(open(path, encoding="utf-8", errors="ignore").read())
        # 丢掉预处理指令行（#error/#warning 的文本不上屏，不需要字库支持）
        src = "\n".join(l for l in src.split("\n") if not l.lstrip().startswith("#"))
        miss = set()
        for lit in lit_re.findall(src):
            for ch in lit:
                if ord(ch) > 0x7F and ord(ch) not in codes:
                    miss.add(ch)
        if miss:
            bad[path] = "".join(sorted(miss))
    if not bad:
        print("OK: src/ 里所有界面字符串用到的非 ASCII 字符都在", FONT, "里")
        return 0
    print("以下字符在字库里没有（屏幕上会留一个空档），请替换成 ASCII 写法：")
    for path, miss in bad.items():
        print(path, "->", miss)
    return 1


if __name__ == "__main__":
    # --font <文件> 可指定字库；否则用默认（存在 src/font_big.c 就用它）
    if "--font" in sys.argv:
        i = sys.argv.index("--font")
        FONT = sys.argv[i + 1]
        del sys.argv[i:i + 2]
    else:
        FONT = find_font()
    if "--scan" in sys.argv:
        sys.exit(scan_sources())
    main()
