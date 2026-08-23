#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""v1 外置平板固件改造：状态机 / 触控 / 新页面渲染（结构部分）。
锚点不匹配即退出，不破坏文件。"""
import re
import sys

PATH = "/Users/lwl/TuyaOpenIDE/projects/copy/source/embedded/src/app_xzd.c"

with open(PATH, "r", encoding="utf-8") as fh:
    data = fh.read()


def repl(anchor, addition, label, count=1):
    global data
    if anchor not in data:
        print("FAIL: %s (anchor missing)" % label, file=sys.stderr)
        sys.exit(2)
    if addition in data:
        print("SKIP: %s (already applied)" % label)
        return
    data = data.replace(anchor, addition, count)
    print("OK: %s" % label)


# 1) 头注释 -> v1
repl(
"""/**
 * @file app_xzd.c
 * @brief 食刻 main state machine (reference UI implemented on hardware).
 *
 * Flow: BOOT -> IDLE -> [放入|取出] -> SCAN (live preview + 3s translucent
 * countdown) -> auto JPEG capture + shutter click -> RECOGNIZING (upload) ->
 * RESULT in/out / LOW_CONF / NET_FAIL / OFFLINE.
 *
 * All screens are drawn in the native portrait 240x320 space defined by the
 * reference UI (硬件界面-烧录AI参考). The camera preview is rotated 90 deg
 * counter-clockwise and downscaled to fill the portrait screen. The preview
 * is rendered directly from the camera's raw UYVY frames (deterministic byte
 * order), so no JPEG decode is involved in the live view.
 */""",
"""/**
 * @file app_xzd.c
 * @brief 食刻外置平板 v1 门外交互屏 (reference UI implemented on hardware).
 *
 * Flow: BOOT -> STANDBY (放入/取出 + 临期提醒)
 *   -> 点【放入/取出】-> POST /api/device/task -> WAIT_SCAN
 *   -> 轮询 GET /api/scan/latest（内模组扫码事件）-> RESULT in/out
 *   -> 回 STANDBY；关门结算有可疑项 -> SUMMARY（确认后回 STANDBY）
 * 辅助：STOCK（库存页）/ OFFLINE（断网本地模式）。
 *
 * 扫码识别由内模组完成（条码优先 + 语音），平板不再拍照上传；摄像头
 * GC2145 仅保留安装自检（帧回调直接忽略）。
 * UI 参考：frontend/tablet-ui/（v1 页面集）。
 */""",
"header")

# 2) 状态枚举
repl(
"""typedef enum {
    XZD_STATE_BOOT = 0,
    XZD_STATE_STANDBY,
    XZD_STATE_PREVIEW,
    XZD_STATE_UPLOADING,
    XZD_STATE_RESULT,
    XZD_STATE_ERROR,
    XZD_STATE_OFFLINE,
} XZD_STATE_E;""",
"""typedef enum {
    XZD_STATE_BOOT = 0,
    XZD_STATE_STANDBY,
    XZD_STATE_WAIT_SCAN,
    XZD_STATE_RESULT,
    XZD_STATE_ERROR,
    XZD_STATE_OFFLINE,
    XZD_STATE_STOCK,
    XZD_STATE_SUMMARY,
} XZD_STATE_E;""",
"state enum")

# 3a) 等待扫码计时变量
repl(
"static uint32_t              sg_boot_start_ms = 0;",
"""static uint32_t              sg_boot_start_ms = 0;
static uint32_t              sg_wait_start_ms = 0;   /* WAIT_SCAN 进入时刻 */
static uint32_t              sg_wait_poll_ms = 0;    /* 上次轮询时刻 */""",
"wait vars")

# 3b) 汇总演示数据（后端 /api/summary/pending 就绪后由云端填充）
repl(
"static XZD_ERR_E             sg_err = XZD_ERR_NONE;",
"""static XZD_ERR_E             sg_err = XZD_ERR_NONE;
/* 汇总页统计（v1 骨架：后端 /api/summary/pending 就绪前为演示数据） */
static int                    sg_sum_in = 3;
static int                    sg_sum_out = 1;
static int                    sg_sum_mid = 1;
static int                    sg_sum_unknown = 2;""",
"summary vars")

# 4) 帧回调：v1 无预览
repl(
"""    if (frame->fmt != TUYA_FRAME_FMT_YUV422) {
        return OPRT_OK;
    }
    /* Live preview: scale + rotate this raw UYVY/YUYV frame onto the panel. */
    if (sg_state == XZD_STATE_PREVIEW) {
        __xzd_preview_render(frame->data, frame->width, frame->height);
    }
    return OPRT_OK;""",
"""    if (frame->fmt != TUYA_FRAME_FMT_YUV422) {
        return OPRT_OK;
    }
    /* v1：平板不再做相机预览（扫码由内模组完成），原始帧直接忽略。 */
    return OPRT_OK;""",
"frame cb")

# 5) JPEG 回调：v1 不再平板拍照
repl(
"""    if (frame->data_len > XZD_JPEG_BUF_CAP) {
        return OPRT_OK;
    }

    /* Capture a frame for upload when the countdown requests it. */
    if (sg_capture_req && !sg_capture_done && NULL != sg_jpeg_buf) {
        memcpy(sg_jpeg_buf, frame->data, frame->data_len);
        sg_jpeg_len = frame->data_len;
        sg_capture_done = true;
        sg_capture_req = false;
    }

    return OPRT_OK;""",
"""    if (frame->data_len > XZD_JPEG_BUF_CAP) {
        return OPRT_OK;
    }

    /* v1：不再由平板拍照上传（内模组完成），JPEG 帧直接忽略。 */
    return OPRT_OK;""",
"jpeg cb")

# 6) start_scan -> start_wait + tick_wait_scan
repl(
"""static void __xzd_start_scan(const char *action)
{
    snprintf(sg_action, sizeof(sg_action), "%s", action);
    snprintf(sg_last_event, sizeof(sg_last_event), "SCAN_START");
    sg_state = XZD_STATE_PREVIEW;
    sg_countdown_disp = 3;
    sg_countdown_last = 0;
    sg_countdown_start_ms = tal_system_get_millisecond();
    sg_capture_req = false;
    sg_capture_done = false;
    sg_capture_wait_start = 0;
    sg_freeze_req = false;
    sg_frozen = false;
    sg_upload_finished = false;
    PR_NOTICE("xzd: start scan action=%s", sg_action);
    __xzd_report("SCAN START");
}""",
"""static void __xzd_start_wait(const char *action)
{
    snprintf(sg_action, sizeof(sg_action), "%s", action);
    snprintf(sg_last_event, sizeof(sg_last_event), "SCAN_WAIT");
    if (xzd_net_post_task(action)) {
        sg_state = XZD_STATE_WAIT_SCAN;
        sg_wait_start_ms = tal_system_get_millisecond();
        sg_wait_poll_ms = 0;
        __xzd_render_wait_scan();
        __xzd_report("TASK POSTED");
    } else {
        sg_state = XZD_STATE_ERROR;
        sg_err = XZD_ERR_NET;
        __xzd_render_error();
        __xzd_report("TASK FAIL");
    }
}

/* WAIT_SCAN 主循环：每 3s 轮询云端最近一次扫码事件，15s 无结果提示重试。 */
static void __xzd_tick_wait_scan(uint32_t now)
{
    XZD_SCAN_RESULT_T r;

    if ((now - sg_wait_start_ms) >= 15000) {
        sg_state = XZD_STATE_ERROR;
        sg_err = XZD_ERR_NET;
        __xzd_render_error();
        __xzd_report("WAIT SCAN TIMEOUT");
        return;
    }
    if ((now - sg_wait_poll_ms) < 3000) {
        return;
    }
    sg_wait_poll_ms = now;
    if (xzd_net_fetch_latest_scan(&r) && r.name[0] != '\\0') {
        sg_result = r;
        sg_last_result = r;
        xzd_net_last_result_save(&r);
        sg_state = XZD_STATE_RESULT;
        __xzd_render_result();
        __xzd_report("SCAN EVENT");
    }
}""",
"start_wait + tick")

# 7) 删除拍照主路径函数（on_captured / tick_preview / tick_uploading）
pattern = re.compile(
    r"static void __xzd_on_captured\(void\)\n.*?(?=static void __xzd_confirm_action)",
    re.S)
new_data, n = pattern.subn("", data, count=1)
if n == 1:
    data = new_data
    print("OK: drop capture path functions")
else:
    print("FAIL: capture path functions", file=sys.stderr)
    sys.exit(2)

# 8) confirm_action 语义：v1 云端自动入账，仅关闭结果页
repl(
"""static void __xzd_confirm_action(void)
{
    if (strcmp(sg_action, "out") == 0) {
        if (sg_stock_count > 0) {
            sg_stock_count--;
        }
    } else {
        sg_stock_count++;
    }
    __xzd_stock_save();
    PR_NOTICE("xzd: confirm %s, stock=%d", sg_action, (int)sg_stock_count);
    __xzd_goto_standby();
}""",
"""static void __xzd_confirm_action(void)
{
    /* v1：库存入账由内模组扫码自动完成（POST /api/scan），平板仅关闭结果页 */
    __xzd_goto_standby();
}

/* 汇总页全部确认：后端 POST /api/summary/confirm 就绪前先本地清零并返回。
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
"confirm + summary confirm")

# 9) render_uploading -> wait_scan / stock / summary 三个新页面
pattern = re.compile(
    r"static void __xzd_render_uploading\(void\)\n.*?(?=static void __xzd_render_result)",
    re.S)
new_render = """static void __xzd_render_wait_scan(void)
{
    xzd_ui_fill_full(sg_ui_fb, XZD_COLOR_BG);
    __xzd_render_status_bar(sg_ui_fb, true);

    /* 动作徽标 */
    if (strcmp(sg_action, "out") == 0) {
        xzd_ui_fill_round_rect_bordered(sg_ui_fb, 12, 30, 228, 58, 12, 2,
                                        XZD_COLOR_RED2, XZD_COLOR_CARD);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 38, "正在取出",
                                XZD_COLOR_RED2, 255);
    } else {
        xzd_ui_fill_round_rect_grad(sg_ui_fb, 12, 30, 228, 58, 12,
                                    XZD_COLOR_RED_TOP, XZD_COLOR_RED_D);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 38, "正在放入",
                                XZD_COLOR_WHITE, 255);
    }

    xzd_ui_draw_spinner(sg_ui_fb, XZD_DISP_W / 2, 128, 10, XZD_COLOR_RED2, 80,
                        (int16_t)((tal_system_get_millisecond() / 80) % 12));
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 168, "已通知内模组",
                            XZD_COLOR_INK, 255);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 194, "请把食材条码对准镜头",
                            XZD_COLOR_INK2, 255);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 214, "结果将语音播报并显示在本屏",
                            XZD_COLOR_INK2, 255);

    xzd_ui_fill_round_rect_bordered(sg_ui_fb, 12, 272, 228, 310, 14, 2,
                                    XZD_COLOR_LINE, XZD_COLOR_CARD);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 284, "取消，回待机",
                            XZD_COLOR_INK2, 255);
    __xzd_ui_flush();
}

static void __xzd_render_stock(void)
{
    char line[96];
    uint8_t i;

    xzd_ui_fill_full(sg_ui_fb, XZD_COLOR_BG);
    __xzd_render_status_bar(sg_ui_fb, true);
    xzd_ui_draw_text(sg_ui_fb, 12, 28, "库存", XZD_COLOR_INK, 255);
    xzd_ui_draw_text(sg_ui_fb, 60, 28, sg_devstate_valid ? "● 已同步" : "○ 本地缓存",
                     sg_devstate_valid ? XZD_COLOR_GREEN_OK : XZD_COLOR_AMBER, 255);

    /* 统计卡 */
    xzd_ui_fill_round_rect(sg_ui_fb, 12, 48, 228, 88, 12, XZD_COLOR_CARD, 255);
    snprintf(line, sizeof(line), "在库 %d 种",
             sg_devstate_valid ? (int)sg_devstate.stock_total : (int)sg_stock_count);
    xzd_ui_draw_text(sg_ui_fb, 20, 56, line, XZD_COLOR_INK, 255);
    snprintf(line, sizeof(line), "临期 %d  过期 %d",
             sg_devstate_valid ? (int)sg_devstate.soon_count : 0,
             sg_devstate_valid ? (int)sg_devstate.expired_count : 0);
    xzd_ui_draw_text(sg_ui_fb, 20, 74, line, XZD_COLOR_AMBER_INK, 255);

    /* 最近临期项（device/state 只携带前 2 项，避免 /api/items 大响应崩堆） */
    if (sg_devstate_valid && sg_devstate.soon_count > 0) {
        for (i = 0; i < 2 && i < (uint8_t)sg_devstate.soon_count; i++) {
            uint16_t y0 = (uint16_t)(104 + i * 40);

            xzd_ui_fill_round_rect(sg_ui_fb, 12, y0, 228, (uint16_t)(y0 + 32),
                                   10, XZD_COLOR_CARD, 255);
            __xzd_draw_text_ellipsis(sg_ui_fb, 20, (uint16_t)(y0 + 8),
                                     sg_devstate.soon_name[i], 130,
                                     XZD_COLOR_INK, 255);
            if (sg_devstate.soon_days[i] == 0) {
                snprintf(line, sizeof(line), "今天到期");
                __xzd_draw_text_right(sg_ui_fb, 220, (uint16_t)(y0 + 8), line,
                                      XZD_COLOR_RED2, 255);
            } else {
                snprintf(line, sizeof(line), "剩 %d 天", sg_devstate.soon_days[i]);
                __xzd_draw_text_right(sg_ui_fb, 220, (uint16_t)(y0 + 8), line,
                                      XZD_COLOR_AMBER, 255);
            }
        }
    } else {
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 122, "暂无临期提醒",
                                XZD_COLOR_INK2, 255);
    }

    /* 最近一次识别（云端 lastResult） */
    if (sg_devstate_valid && sg_devstate.last_name[0] != '\\0') {
        snprintf(line, sizeof(line), "最近: %s 剩 %d 天",
                 sg_devstate.last_name, sg_devstate.last_days);
        __xzd_draw_text_ellipsis(sg_ui_fb, 12, 196, line, 216,
                                 XZD_COLOR_INK2, 255);
    }

    /* 底部：关门结算入口（后端 summary/pending 就绪后改为自动进入） */
    xzd_ui_fill_round_rect_grad(sg_ui_fb, 12, 268, 228, 308, 14,
                                XZD_COLOR_RED_TOP, XZD_COLOR_RED_D);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 280, "关门结算",
                            XZD_COLOR_WHITE, 255);
    __xzd_ui_flush();
}

static void __xzd_render_summary(void)
{
    char line[96];

    xzd_ui_fill_full(sg_ui_fb, XZD_COLOR_BG);
    __xzd_render_status_bar(sg_ui_fb, true);
    xzd_ui_draw_text(sg_ui_fb, 12, 28, "关门结算", XZD_COLOR_INK, 255);

    /* 统计四格 */
    snprintf(line, sizeof(line), "%d", sg_sum_in);
    xzd_ui_fill_round_rect(sg_ui_fb, 12, 46, 108, 86, 10, XZD_COLOR_CARD, 255);
    xzd_ui_draw_text_center(sg_ui_fb, 60, 54, line, XZD_COLOR_INK, 255);
    xzd_ui_draw_text_center(sg_ui_fb, 60, 72, "放入", XZD_COLOR_INK2, 255);
    snprintf(line, sizeof(line), "%d", sg_sum_out);
    xzd_ui_fill_round_rect(sg_ui_fb, 132, 46, 228, 86, 10, XZD_COLOR_CARD, 255);
    xzd_ui_draw_text_center(sg_ui_fb, 180, 54, line, XZD_COLOR_INK, 255);
    xzd_ui_draw_text_center(sg_ui_fb, 180, 72, "取出", XZD_COLOR_INK2, 255);
    snprintf(line, sizeof(line), "%d", sg_sum_mid);
    xzd_ui_fill_round_rect(sg_ui_fb, 12, 94, 108, 134, 10, XZD_COLOR_CARD, 255);
    xzd_ui_draw_text_center(sg_ui_fb, 60, 102, line, XZD_COLOR_INK, 255);
    xzd_ui_draw_text_center(sg_ui_fb, 60, 120, "中途取出", XZD_COLOR_INK2, 255);
    snprintf(line, sizeof(line), "%d", sg_sum_unknown);
    xzd_ui_fill_round_rect(sg_ui_fb, 132, 94, 228, 134, 10,
                           sg_sum_unknown > 0 ? XZD_COLOR_ERR_SOFT : XZD_COLOR_CARD,
                           255);
    xzd_ui_draw_text_center(sg_ui_fb, 180, 102, line,
                            sg_sum_unknown > 0 ? XZD_COLOR_RED2 : XZD_COLOR_INK, 255);
    xzd_ui_draw_text_center(sg_ui_fb, 180, 120, "未识别", XZD_COLOR_INK2, 255);

    /* 提示条 */
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
}

"""
new_data, n = pattern.subn(new_render, data, count=1)
if n == 1:
    data = new_data
    print("OK: render upload -> wait/stock/summary")
else:
    print("FAIL: render_uploading replacement", file=sys.stderr)
    sys.exit(2)

# 10) render_error 文案（v1：未收到识别结果 / 动作下发失败）
repl('        t2 = "照片未上传成功";', '        t2 = "未收到识别结果";', "error t2")
repl('        t3 = "本次动作已保留，不会丢失";', '        t3 = "请检查网络后重试";', "error t3")
repl('        t3 = "未写入库存，请对准再拍";', '        t3 = "请再点一次【放入】/【取出】";', "error low t3")

# 11) 触控 switch -> v1
pattern = re.compile(
    r"        switch \(sg_state\) \{\n.*?        default:\n            break;\n        \}",
    re.S)
new_switch = """        switch (sg_state) {
        case XZD_STATE_STANDBY:
            if (xzd_ui_hit(x, y, 10, 92, 230, 168)) {
                __xzd_start_wait("in");
            } else if (xzd_ui_hit(x, y, 10, 178, 230, 254)) {
                __xzd_start_wait("out");
            } else if (xzd_ui_hit(x, y, 10, 46, 230, 82)) {
                sg_state = XZD_STATE_STOCK;
                __xzd_render_stock();
            }
            break;
        case XZD_STATE_WAIT_SCAN:
            if (xzd_ui_hit(x, y, 12, 272, 228, 310)) {
                __xzd_goto_standby();
            }
            break;
        case XZD_STATE_RESULT:
            if (xzd_ui_hit(x, y, 12, 258, 228, 292)) {
                __xzd_confirm_action();
            }
            break;
        case XZD_STATE_STOCK:
            if (xzd_ui_hit(x, y, 12, 268, 228, 308)) {
                sg_state = XZD_STATE_SUMMARY;
                __xzd_render_summary();
            } else if (xzd_ui_hit(x, y, 180, 8, 232, 30)) {
                __xzd_goto_standby();
            }
            break;
        case XZD_STATE_SUMMARY:
            if (xzd_ui_hit(x, y, 12, 266, 228, 306)) {
                __xzd_summary_confirm();
            }
            break;
        case XZD_STATE_ERROR:
            if (xzd_ui_hit(x, y, 12, 236, 228, 278)) {
                __xzd_start_wait(sg_action);
            } else if (xzd_ui_hit(x, y, 12, 286, 228, 318)) {
                __xzd_goto_standby();
            }
            break;
        case XZD_STATE_OFFLINE:
            if (xzd_ui_hit(x, y, 12, 268, 228, 308)) {
                if (xzd_net_is_online()) {
                    __xzd_goto_standby();
                } else {
                    __xzd_render_offline();
                }
            }
            break;
        default:
            break;
        }"""
new_data, n = pattern.subn(new_switch, data, count=1)
if n == 1:
    data = new_data
    print("OK: touch switch")
else:
    print("FAIL: touch switch", file=sys.stderr)
    sys.exit(2)

# 12) loop：按钮取消条件 + WAIT_SCAN tick
repl(
"""        if (sg_state == XZD_STATE_PREVIEW || sg_state == XZD_STATE_UPLOADING) {
            PR_NOTICE("xzd: button cancel -> standby");
            __xzd_goto_standby();
        }""",
"""        if (sg_state == XZD_STATE_WAIT_SCAN || sg_state == XZD_STATE_RESULT) {
            PR_NOTICE("xzd: button cancel -> standby");
            __xzd_goto_standby();
        }""",
"button cancel")
repl(
"""    case XZD_STATE_PREVIEW:
        __xzd_tick_preview(now);
        break;
    case XZD_STATE_UPLOADING:
        __xzd_tick_uploading(now);
        break;
    default:
        break;
    }""",
"""    case XZD_STATE_WAIT_SCAN:
        __xzd_tick_wait_scan(now);
        break;
    default:
        break;
    }""",
"loop switch")

with open(PATH, "w", encoding="utf-8") as fh:
    fh.write(data)
print("DONE")
