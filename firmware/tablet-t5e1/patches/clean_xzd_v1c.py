#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""清理 v0 残留死代码（-Werror unused 报错迭代清理）。"""
import re
import sys

PATH = "/Users/lwl/TuyaOpenIDE/projects/copy/source/embedded/src/app_xzd.c"

with open(PATH, "r", encoding="utf-8") as fh:
    data = fh.read()


def drop_var(text, line, label):
    if line in text:
        text = text.replace(line + "\n", "", 1)
        print("OK: drop %s" % label)
    else:
        print("SKIP/WARN: %s not found" % label)
    return text


# 1) 预览渲染两个函数（yuv 填充 + preview 渲染）整体删除
pattern = re.compile(
    r"static void __xzd_yuv422_raw_fill\(const uint8_t \*src.*?(?=static OPERATE_RET __xzd_frame_cb)",
    re.S)
new_data, n = pattern.subn("", data, count=1)
if n == 1:
    data = new_data
    print("OK: drop preview render fns")
else:
    print("FAIL: preview fns", file=sys.stderr)
    sys.exit(2)

# 2) 倒计时 / 上传 dot 变量（编译报 unused）
for line, label in [
    ("static volatile uint8_t      sg_countdown_disp = 3;", "sg_countdown_disp"),
    ("static uint8_t               sg_countdown_last = 0;", "sg_countdown_last"),
    ("static uint32_t              sg_countdown_start_ms = 0;", "sg_countdown_start_ms"),
    ("static uint8_t               sg_dots = 0;", "sg_dots"),
    ("static uint32_t              sg_last_dots_ms = 0;", "sg_last_dots_ms"),
    ("static uint32_t              sg_upload_start_ms = 0;", "sg_upload_start_ms"),
    ("static uint32_t              sg_capture_wait_start = 0;", "sg_capture_wait_start"),
]:
    data = drop_var(data, line, label)

with open(PATH, "w", encoding="utf-8") as fh:
    fh.write(data)
print("DONE")
