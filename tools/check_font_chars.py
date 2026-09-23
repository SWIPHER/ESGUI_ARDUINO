#!/usr/bin/env python3
"""临时工具：检查 eui_test_font 是否包含计划使用的汉字。"""
import re
import sys

FONT = "lib/ESGUI/Font/eui_test_font.c"


def load_codes(path):
    src = open(path, encoding="utf-8", errors="ignore").read()
    m = re.search(r"eui_test_font_unicode_list_95\[\]\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("unicode list not found")
    body = m.group(1)
    return [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", body)]


def main():
    codes = load_codes(FONT)
    chars = set(codes)
    text = sys.argv[1] if len(sys.argv) > 1 else ""
    missing = sorted({ch for ch in text if ord(ch) not in chars and ord(ch) > 0x7F})
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
    if "--scan" in sys.argv:
        sys.exit(scan_sources())
    main()
