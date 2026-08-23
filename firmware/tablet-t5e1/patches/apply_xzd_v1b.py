#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""v1 外置平板固件改造：RESULT 页渲染替换（内模组结果同步）。"""
import re
import sys

PATH = "/Users/lwl/TuyaOpenIDE/projects/copy/source/embedded/src/app_xzd.c"

with open(PATH, "r", encoding="utf-8") as fh:
    data = fh.read()

pattern = re.compile(
    r"static void __xzd_render_result\(void\)\n.*?(?=static void __xzd_render_error)",
    re.S)

new_result = """static void __xzd_render_result(void)
{
    char line[96];
    char tbuf[24];
    bool out = (strcmp(sg_action, "out") == 0);

    xzd_ui_fill_full(sg_ui_fb, XZD_COLOR_BG);
    __xzd_render_status_bar(sg_ui_fb, false);

    /* mode pill：数据来自内模组扫码事件（GET /api/scan/latest） */
    if (out) {
        xzd_ui_fill_round_rect(sg_ui_fb, 16, 26, 224, 44, 9,
                               XZD_COLOR_BLUE_SOFT, 255);
        xzd_ui_draw_text(sg_ui_fb, 24, 28, "内模组识别成功 取出",
                         XZD_COLOR_BLUE2, 255);
    } else {
        xzd_ui_fill_round_rect(sg_ui_fb, 16, 26, 224, 44, 9,
                               XZD_COLOR_GREEN_SOFT, 255);
        xzd_ui_draw_text(sg_ui_fb, 24, 28, "内模组识别成功 放入",
                         XZD_COLOR_GREEN_D, 255);
    }

    /* product name */
    __xzd_draw_text_ellipsis(sg_ui_fb, 24, 50,
                             sg_result.name[0] ? sg_result.name : "未知食材",
                             208, XZD_COLOR_INK, 255);

    /* info card */
    xzd_ui_fill_round_rect(sg_ui_fb, 12, 76, 228, 190, 14,
                           XZD_COLOR_CARD, 255);

    snprintf(tbuf, sizeof(tbuf), "%s",
             sg_result.scanned_at[0] ? sg_result.scanned_at : "--");
    __xzd_render_row(sg_ui_fb, 88, "时间", tbuf, XZD_COLOR_INK);

    if (out) {
        snprintf(line, sizeof(line), "%d 件", (int)sg_stock_count);
        __xzd_render_row(sg_ui_fb, 116, "当前库存", line, XZD_COLOR_INK);
        if (sg_result.days_left >= 0) {
            snprintf(line, sizeof(line), "%s 剩 %d 天",
                     sg_result.expiry_date[0] ? sg_result.expiry_date : "--",
                     sg_result.days_left);
        } else {
            snprintf(line, sizeof(line), "%s",
                     sg_result.expiry_date[0] ? sg_result.expiry_date : "--");
        }
        __xzd_render_row(sg_ui_fb, 144, "到期日", line, XZD_COLOR_RED2);
        __xzd_render_row(sg_ui_fb, 172, "建议容器",
                         sg_result.suggested_container[0] ?
                             sg_result.suggested_container : "--",
                         XZD_COLOR_INK);
    } else {
        if (sg_result.days_left >= 0) {
            snprintf(line, sizeof(line), "至 %s 剩 %d 天",
                     sg_result.expiry_date[0] ? sg_result.expiry_date : "--",
                     sg_result.days_left);
        } else {
            snprintf(line, sizeof(line), "至 %s",
                     sg_result.expiry_date[0] ? sg_result.expiry_date : "--");
        }
        __xzd_render_row(sg_ui_fb, 116, "保质期", line, XZD_COLOR_RED2);
        __xzd_render_row(sg_ui_fb, 144, "建议容器",
                         sg_result.suggested_container[0] ?
                             sg_result.suggested_container : "--",
                         XZD_COLOR_INK);
        __xzd_render_row(sg_ui_fb, 172, "入账", "库存 +1（云端已入账）",
                         XZD_COLOR_GREEN_D);
    }

    /* 完成按钮（v1：入账由内模组扫码自动完成，本页仅展示） */
    if (out) {
        xzd_ui_fill_round_rect(sg_ui_fb, 12, 258, 228, 292, 14,
                               XZD_COLOR_INK, 255);
    } else {
        xzd_ui_fill_round_rect_grad(sg_ui_fb, 12, 258, 228, 292, 14,
                                    XZD_COLOR_RED_TOP, XZD_COLOR_RED_D);
    }
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 268, "✓ 完成，回待机",
                            XZD_COLOR_WHITE, 255);
    __xzd_ui_flush();
}

"""

new_data, n = pattern.subn(new_result, data, count=1)
if n == 1:
    data = new_data
    print("OK: render_result v1")
else:
    print("FAIL: render_result", file=sys.stderr)
    sys.exit(2)

# 错误页按钮文案：v1 重试 = 重新发起动作下发
old_btn = '        btn = "重新扫描";'
new_btn = '        btn = "重试";'
if old_btn in data:
    data = data.replace(old_btn, new_btn, 1)
    print("OK: error retry label")
elif new_btn in data:
    print("SKIP: error retry label")
else:
    print("FAIL: error retry label", file=sys.stderr)
    sys.exit(2)

with open(PATH, "w", encoding="utf-8") as fh:
    fh.write(data)
print("DONE")
