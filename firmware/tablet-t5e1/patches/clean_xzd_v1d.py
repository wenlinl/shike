#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""清理 v0 残留：preview 双缓冲 free 回调（v1 无预览）。"""
import re
import sys

PATH = "/Users/lwl/TuyaOpenIDE/projects/copy/source/embedded/src/app_xzd.c"

with open(PATH, "r", encoding="utf-8") as fh:
    data = fh.read()

pattern = re.compile(
    r"static void __xzd_out_fb_free_cb\(TDL_DISP_FRAME_BUFF_T \*fb\)\n.*?(?=static void __xzd_ui_fb_free_cb)",
    re.S)
new_data, n = pattern.subn("", data, count=1)
if n == 1:
    data = new_data
    print("OK: drop out_fb_free_cb")
else:
    print("FAIL: out_fb_free_cb", file=sys.stderr)
    sys.exit(2)

with open(PATH, "w", encoding="utf-8") as fh:
    fh.write(data)
print("DONE")
