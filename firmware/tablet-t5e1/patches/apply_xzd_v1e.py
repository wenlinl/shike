#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""app_xzd.c v1e：汇总页接真实 pending/confirm（进入拉取、渲染真实数据、确认提交）。"""
import sys

PATH = "/Users/lwl/TuyaOpenIDE/projects/copy/source/embedded/src/app_xzd.c"

with open(PATH, "r", encoding="utf-8") as fh:
    data = fh.read()


def repl(anchor, addition, label):
    global data
    if anchor not in data:
        print("FAIL: %s" % label, file=sys.stderr)
        sys.exit(2)
    if addition in data:
        print("SKIP: %s" % label)
        return
    data = data.replace(anchor, addition, 1)
    print("OK: %s" % label)


# 1) 汇总变量：默认 0 + 批次 id / has_pending
repl(
"""static int                    sg_sum_in = 3;
static int                    sg_sum_out = 1;
static int                    sg_sum_mid = 1;
static int                    sg_sum_unknown = 2;""",
"""static int                    sg_sum_in = 0;
static int                    sg_sum_out = 0;
static int                    sg_sum_mid = 0;
static int                    sg_sum_unknown = 0;
static char                   sg_sum_batch_id[40] = "";  /* 待确认批次 id（云端） */
static int                    sg_sum_has_pending = 0;    /* 是否有待确认批次 */""",
"summary vars")

# 2) 新增：进入汇总页拉取 pending
repl(
"""/* 汇总页全部确认：后端 POST /api/summary/confirm 就绪前先本地清零并返回。
 * 契约就绪后应改为：POST -> 成功回 STANDBY，失败保留本页。 */
static void __xzd_summary_confirm(void)
{
    sg_sum_in = 0;
    sg_sum_out = 0;
    sg_sum_mid = 0;
    sg_sum_unknown = 0;
    PR_NOTICE("xzd: summary confirmed (local demo; POST pending backend)");
    __xzd_goto_standby();
}""",
"""/* 进入汇总页时拉取云端待确认批次（GET /api/summary/pending） */
static void __xzd_summary_fetch(void)
{
    XZD_SUMMARY_T sm;

    memset(&sm, 0, sizeof(sm));
    sg_sum_has_pending = 0;
    sg_sum_batch_id[0] = '\\0';
    sg_sum_in = 0;
    sg_sum_out = 0;
    sg_sum_mid = 0;
    sg_sum_unknown = 0;
    if (xzd_net_fetch_pending_summary(&sm) && sm.has_pending) {
        sg_sum_in = sm.in_count;
        sg_sum_out = sm.out_count;
        sg_sum_mid = sm.mid_out_count;
        sg_sum_unknown = sm.unknown_count;
        snprintf(sg_sum_batch_id, sizeof(sg_sum_batch_id), "%s", sm.batch_id);
        sg_sum_has_pending = 1;
        __xzd_report("SUMMARY PENDING");
    } else {
        __xzd_report("SUMMARY NONE");
    }
}

/* 汇总页全部确认：POST /api/summary/confirm -> 成功回 STANDBY，失败保留本页可重试。 */
static void __xzd_summary_confirm(void)
{
    if (sg_sum_has_pending && sg_sum_batch_id[0] != '\\0') {
        if (!xzd_net_confirm_summary(sg_sum_batch_id)) {
            PR_NOTICE("xzd: summary confirm failed, keep page");
            __xzd_report("SUMMARY CONFIRM FAIL");
            __xzd_render_summary();
            return;
        }
        __xzd_report("SUMMARY CONFIRMED");
    }
    sg_sum_in = 0;
    sg_sum_out = 0;
    sg_sum_mid = 0;
    sg_sum_unknown = 0;
    sg_sum_has_pending = 0;
    sg_sum_batch_id[0] = '\\0';
    __xzd_goto_standby();
}""",
"summary fetch + confirm")

# 3) 渲染：提示条真实数据（去掉演示数据字样）
repl(
"""    /* 提示条 */
    if (sg_sum_unknown > 0) {
        xzd_ui_fill_round_rect(sg_ui_fb, 12, 148, 228, 184, 12,
                               XZD_COLOR_AMBER_SOFT, 255);
        snprintf(line, sizeof(line), "有 %d 项需要确认，其余已自动入账",
                 sg_sum_unknown);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 158, line,
                                XZD_COLOR_AMBER_INK, 255);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 174,
                                "（后端 summary 接口就绪前为演示数据）",
                                XZD_COLOR_AMBER_INK, 255);
    } else {
        xzd_ui_fill_round_rect(sg_ui_fb, 12, 148, 228, 184, 12,
                               XZD_COLOR_GREEN_SOFT, 255);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 158,
                                "本次结算无差异，已全部入账",
                                XZD_COLOR_GREEN_D, 255);
    }

    /* 可疑项（演示：未识别 ×2） */
    xzd_ui_fill_round_rect(sg_ui_fb, 12, 196, 228, 240, 12, XZD_COLOR_CARD, 255);
    snprintf(line, sizeof(line), "未识别 × %d", sg_sum_unknown);
    xzd_ui_draw_text(sg_ui_fb, 20, 204, line, XZD_COLOR_INK, 255);
    xzd_ui_draw_text(sg_ui_fb, 130, 204, "抓拍", XZD_COLOR_INK2, 255);
    xzd_ui_draw_text(sg_ui_fb, 20, 222, "请确认取出了什么 / 是否入账", XZD_COLOR_INK2, 255);

    xzd_ui_fill_round_rect_grad(sg_ui_fb, 12, 266, 228, 306, 14,
                                XZD_COLOR_RED_TOP, XZD_COLOR_RED_D);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 278, "✓ 全部确认",
                            XZD_COLOR_WHITE, 255);
    __xzd_ui_flush();
}""",
"""    /* 提示条 */
    if (sg_sum_has_pending && sg_sum_unknown > 0) {
        xzd_ui_fill_round_rect(sg_ui_fb, 12, 148, 228, 184, 12,
                               XZD_COLOR_AMBER_SOFT, 255);
        snprintf(line, sizeof(line), "有 %d 项需要确认，其余已自动入账",
                 sg_sum_unknown);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 158, line,
                                XZD_COLOR_AMBER_INK, 255);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 174,
                                "含未识别，请核对后确认",
                                XZD_COLOR_AMBER_INK, 255);
    } else if (sg_sum_has_pending) {
        xzd_ui_fill_round_rect(sg_ui_fb, 12, 148, 228, 184, 12,
                               XZD_COLOR_GREEN_SOFT, 255);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 158,
                                "本次结算无差异，已全部入账",
                                XZD_COLOR_GREEN_D, 255);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 174,
                                "确认后关闭本次结算",
                                XZD_COLOR_GREEN_D, 255);
    } else {
        xzd_ui_fill_round_rect(sg_ui_fb, 12, 148, 228, 184, 12,
                               XZD_COLOR_GREEN_SOFT, 255);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 158,
                                "当前无待确认批次",
                                XZD_COLOR_GREEN_D, 255);
    }

    /* 可疑项：有未识别时展示明细，否则空态 */
    xzd_ui_fill_round_rect(sg_ui_fb, 12, 196, 228, 240, 12, XZD_COLOR_CARD, 255);
    if (sg_sum_unknown > 0) {
        snprintf(line, sizeof(line), "未识别 × %d", sg_sum_unknown);
        xzd_ui_draw_text(sg_ui_fb, 20, 204, line, XZD_COLOR_INK, 255);
        xzd_ui_draw_text(sg_ui_fb, 130, 204, "抓拍", XZD_COLOR_INK2, 255);
        xzd_ui_draw_text(sg_ui_fb, 20, 222, "请确认取出了什么 / 是否入账", XZD_COLOR_INK2, 255);
    } else {
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 214,
                                "全部已入账，无待确认项",
                                XZD_COLOR_INK2, 255);
    }

    if (sg_sum_has_pending) {
        xzd_ui_fill_round_rect_grad(sg_ui_fb, 12, 266, 228, 306, 14,
                                    XZD_COLOR_RED_TOP, XZD_COLOR_RED_D);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 278, "✓ 全部确认",
                                XZD_COLOR_WHITE, 255);
    } else {
        xzd_ui_fill_round_rect_bordered(sg_ui_fb, 12, 266, 228, 306, 14, 2,
                                        XZD_COLOR_LINE, XZD_COLOR_CARD);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 278, "返回待机",
                                XZD_COLOR_INK2, 255);
    }
    __xzd_ui_flush();
}""",
"render summary real")

# 4) 触控：进汇总页前拉取 pending
repl(
"""        case XZD_STATE_STOCK:
            if (xzd_ui_hit(x, y, 12, 268, 228, 308)) {
                sg_state = XZD_STATE_SUMMARY;
                __xzd_render_summary();
            } else if (xzd_ui_hit(x, y, 180, 8, 232, 30)) {""",
"""        case XZD_STATE_STOCK:
            if (xzd_ui_hit(x, y, 12, 268, 228, 308)) {
                __xzd_summary_fetch();
                sg_state = XZD_STATE_SUMMARY;
                __xzd_render_summary();
            } else if (xzd_ui_hit(x, y, 180, 8, 232, 30)) {""",
"touch stock -> summary fetch")

with open(PATH, "w", encoding="utf-8") as fh:
    fh.write(data)
print("DONE")
