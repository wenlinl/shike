/**
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
 */
#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tal_uart.h"

#include "tdl_camera_manage.h"
#include "tdl_display_manage.h"
#include "tdl_tp_manage.h"
#include "tdl_button_manage.h"

#include "app_xzd.h"
#include "app_xzd_cfg.h"
#include "app_xzd_ui.h"
#include "app_xzd_net.h"
#include "app_xzd_audio.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define XZD_CAM_WIDTH           640
#define XZD_CAM_HEIGHT          480
#define XZD_CAM_FPS             15
#define XZD_JPEG_BUF_CAP        (200 * 1024)
#define XZD_JPEG_MAX_SIZE_KB    60
#define XZD_JPEG_MIN_SIZE_KB    30
#define XZD_COUNTDOWN_MS        3000
#define XZD_CAPTURE_WAIT_MS     1200
#define XZD_DISP_W              240   /* native portrait viewport */
#define XZD_DISP_H              320
#define XZD_TP_POINT_MAX        5
#define XZD_STOCK_KV_KEY        "xzd_stock"

/* Result-page container chip geometry (portrait coordinates). */
#define XZD_CHIP_W              64
#define XZD_CHIP_H              24
#define XZD_CHIP_Y1             146
#define XZD_CHIP_Y2             174
#define XZD_CHIP_X0             22
#define XZD_CHIP_GAP            4

/* Diagnostic/report channel: UART0 (download port = USB serial on this board) */
#define XZD_REPORT_UART         TUYA_UART_NUM_0
#define XZD_REPORT_BAUD         115200

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef enum {
    XZD_STATE_BOOT = 0,
    XZD_STATE_STANDBY,
    XZD_STATE_PREVIEW,
    XZD_STATE_UPLOADING,
    XZD_STATE_RESULT,
    XZD_STATE_ERROR,
    XZD_STATE_OFFLINE,
} XZD_STATE_E;

typedef enum {
    XZD_ERR_NONE = 0,
    XZD_ERR_NET,
    XZD_ERR_CAPTURE,
    XZD_ERR_LOW_CONF,
} XZD_ERR_E;

typedef struct {
    char action[4];
    char container[XZD_SCAN_CONTAINER_LEN];
    uint8_t seq;
    uint8_t parity;
} XZD_UPLOAD_JOB_T;

const char *const XZD_CONTAINER_NAMES[XZD_CONTAINER_COUNT] = {
    "冰箱", "零食柜", "药盒", "调料柜", "主食柜"
};

/***********************************************************
***********************variable define**********************
***********************************************************/
static TDL_CAMERA_HANDLE_T   sg_cam_hdl = NULL;
static TDL_DISP_HANDLE_T     sg_disp_hdl = NULL;
static TDL_DISP_DEV_INFO_T   sg_disp_info;
static TDL_TP_HANDLE_T       sg_tp_hdl = NULL;
static TDL_BUTTON_HANDLE     sg_btn_hdl = NULL;

static TDL_DISP_FRAME_BUFF_T *sg_fb_out[2];        /* preview double buffer */
static volatile bool         sg_fb_out_busy[2];
static TDL_DISP_FRAME_BUFF_T *sg_ui_fb = NULL;     /* UI page buffer */
static TDL_DISP_FRAME_BUFF_T *sg_ui_base = NULL;   /* frozen photo copy */
static volatile bool         sg_ui_fb_busy = false;

static uint8_t              *sg_jpeg_buf = NULL;   /* captured JPEG (PSRAM) */
static uint8_t              *sg_upload_jpeg[2] = {NULL, NULL};
static volatile uint32_t     sg_upload_len[2] = {0, 0};
static volatile uint8_t      sg_upload_parity = 0;
static volatile bool         sg_upload_busy = false;
static volatile uint32_t     sg_jpeg_len = 0;
static volatile bool         sg_capture_req = false;
static volatile bool         sg_capture_done = false;
static volatile bool         sg_freeze_req = false;
static volatile bool         sg_frozen = false;
static uint32_t              sg_capture_wait_start = 0;

static volatile XZD_STATE_E  sg_state = XZD_STATE_STANDBY;
static char                  sg_action[4] = "in";
static char                  sg_container[XZD_SCAN_CONTAINER_LEN] = XZD_DEFAULT_CONTAINER;
static volatile uint8_t      sg_countdown_disp = 3;
static uint8_t               sg_countdown_last = 0;
static uint32_t              sg_countdown_start_ms = 0;
static uint32_t              sg_boot_start_ms = 0;

static QUEUE_HANDLE          sg_upload_q = NULL;
static volatile bool         sg_upload_finished = false;
static volatile XZD_NET_RESULT_E sg_last_net_result = XZD_NET_ERR_HTTP;
static volatile uint8_t      sg_upload_seq_sent = 0;
static volatile uint8_t      sg_upload_seq_done = 0;
static XZD_SCAN_RESULT_T     sg_result;
static uint32_t              sg_upload_start_ms = 0;
static uint8_t               sg_dots = 0;
static uint32_t              sg_last_dots_ms = 0;

static XZD_ERR_E             sg_err = XZD_ERR_NONE;
static int32_t               sg_stock_count = 0;
static XZD_SCAN_RESULT_T     sg_last_result;

static bool                  sg_tp_pressed = false;
static volatile bool         sg_btn_pressed = false;
static bool                  sg_calibrate = false;
static bool                  sg_report_uart_ready = false;
static THREAD_HANDLE         sg_upload_thrd = NULL;
static bool                  sg_last_online = false;
static THREAD_HANDLE         sg_housekeep_thrd = NULL;
static XZD_DEVICE_STATE_T    sg_devstate;
static volatile bool         sg_devstate_valid = false;
static char                  sg_last_event[24] = "";
static volatile bool         sg_ui_refresh = false;

/***********************************************************
***********************function define**********************
***********************************************************/
static void __xzd_report_uart_init(void)
{
    /* Disabled: UART0 is owned by the SDK debug log (460800) on this build.
     * The old 115200 report channel must not re-program the log UART. */
    sg_report_uart_ready = false;
}

static void __xzd_report(const char *line)
{
    char buf[160];
    int  len;

    if (false == sg_report_uart_ready) {
        __xzd_report_uart_init();
    }
    if (false == sg_report_uart_ready) {
        return;
    }
    len = snprintf(buf, sizeof(buf), "[XZD] %s\r\n", line);
    if (len > 0) {
        tal_uart_write(XZD_REPORT_UART, (const uint8_t *)buf, (uint32_t)len);
    }
}

static void __xzd_report_tap(uint16_t x, uint16_t y)
{
    char buf[48];
    int len = snprintf(buf, sizeof(buf), "TAP %u,%u", x, y);

    if (len > 0) {
        __xzd_report(buf);
    }
}

static void __xzd_render_standby(void);
static void __xzd_render_boot(void);

static void __xzd_out_fb_free_cb(TDL_DISP_FRAME_BUFF_T *fb)
{
    uint32_t i;

    for (i = 0; i < 2; i++) {
        if (sg_fb_out[i] == fb) {
            sg_fb_out_busy[i] = false;
            break;
        }
    }
}

static void __xzd_ui_fb_free_cb(TDL_DISP_FRAME_BUFF_T *fb)
{
    (void)fb;
    sg_ui_fb_busy = false;
}

static void __xzd_ui_flush(void)
{
    uint32_t t0 = tal_system_get_millisecond();

    while (sg_ui_fb_busy && (tal_system_get_millisecond() - t0) < 200) {
        tal_system_sleep(2);
    }
    sg_ui_fb_busy = true;
    sg_ui_fb->free_cb = __xzd_ui_fb_free_cb;
    tdl_disp_dev_flush(sg_disp_hdl, sg_ui_fb);
}

static void __xzd_stock_save(void)
{
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d", (int)sg_stock_count);

    if (n > 0) {
        tal_kv_set(XZD_STOCK_KV_KEY, (const uint8_t *)buf, (size_t)n);
    }
}

static void __xzd_stock_load(void)
{
    uint8_t *v = NULL;
    size_t len = 0;

    if (tal_kv_get(XZD_STOCK_KV_KEY, &v, &len) == 0 && v && len > 0 && len < 15) {
        v[len] = '\0';
        sg_stock_count = atoi((char *)v);
        tal_kv_free(v);
    }
}

/***********************************************************
********************* camera callbacks *********************
***********************************************************/
/* Render one raw YUV422 frame into a panel buffer: rotate it 90 degrees and
 * downscale it to fill the portrait 240x320 screen. The DVP delivers
 * VYUY bytes (V0 Y0 U0 Y1): luma at the odd byte, V at +0 and U at +2 —
 * reading them the other way round made saturated reds render as blue.
 * Rotation direction, vertical mirror and an edge crop (sensor edge noise ->
 * white bars) are configurable. Every pixel of the frame buffer is covered
 * (no stale rows -> no "snow"). */
static void __xzd_yuv422_raw_fill(const uint8_t *src, uint16_t sw, uint16_t sh,
                                  uint8_t ori, bool flip_v, uint16_t crop,
                                  TDL_DISP_FRAME_BUFF_T *dst, bool is_swap)
{
    uint16_t xs = crop;
    uint16_t ys = crop;
    uint16_t xe = (crop * 2 < sw) ? (uint16_t)(sw - 1 - crop) : (uint16_t)(sw - 1);
    uint16_t ye = (crop * 2 < sh) ? (uint16_t)(sh - 1 - crop) : (uint16_t)(sh - 1);
    uint16_t cw = (uint16_t)(xe - xs + 1);
    uint16_t ch = (uint16_t)(ye - ys + 1);
    uint32_t band = ((uint32_t)ch * XZD_DISP_W) / XZD_DISP_H;
    uint32_t bx0 = xs + ((uint32_t)(cw > band ? cw - band : 0) / 2);
    uint32_t bx1 = bx0 + band - 1;
    uint16_t lx, ly;

    for (ly = 0; ly < XZD_DISP_H; ly++) {
        for (lx = 0; lx < XZD_DISP_W; lx++) {
            uint32_t oy = flip_v ? (uint32_t)(XZD_DISP_H - 1 - ly) : ly;
            uint32_t sy0, sy1, sx0, sx1;
            uint32_t cy;
            uint32_t pair;
            const uint8_t *pc;
            uint32_t ysum = 0;
            uint32_t cnt = 0;
            uint32_t yy, xx;
            int32_t c, u, v, r, g, b;
            uint16_t fx, fy, rgb;

            switch (ori & 3U) {
            case 0: /* no rotation: portrait centre crop of the source */
                sy0 = ys + ((uint32_t)oy * ch) / XZD_DISP_H;
                sy1 = ys + (((uint32_t)(oy + 1) * ch) / XZD_DISP_H) - 1;
                sx0 = (uint32_t)bx0 + ((uint32_t)lx * band) / XZD_DISP_W;
                sx1 = (uint32_t)bx0 + (((uint32_t)(lx + 1) * band) / XZD_DISP_W) - 1;
                break;
            case 1: /* 90 deg CCW: output col -> source rows, output row ->
                     * source columns reversed (screen top = source right) */
                sy0 = ys + ((uint32_t)lx * ch) / XZD_DISP_W;
                sy1 = ys + (((uint32_t)(lx + 1) * ch) / XZD_DISP_W) - 1;
                sx0 = xe - (((uint32_t)(oy + 1) * cw) / XZD_DISP_H);
                sx1 = xe - ((uint32_t)oy * cw) / XZD_DISP_H;
                break;
            case 2: /* 180 deg: mirrored no-rotation crop */
                sy0 = ye - (((uint32_t)(oy + 1) * ch) / XZD_DISP_H);
                sy1 = ye - ((uint32_t)oy * ch) / XZD_DISP_H;
                sx0 = (uint32_t)bx1 - (((uint32_t)(lx + 1) * band) / XZD_DISP_W);
                sx1 = (uint32_t)bx1 - ((uint32_t)lx * band) / XZD_DISP_W;
                break;
            default: /* 90 deg CW: output col -> source rows reversed,
                      * output row -> source columns (screen top = source left) */
                sy0 = ye - (((uint32_t)(lx + 1) * ch) / XZD_DISP_W);
                sy1 = ye - ((uint32_t)lx * ch) / XZD_DISP_W;
                sx0 = xs + ((uint32_t)oy * cw) / XZD_DISP_H;
                sx1 = xs + (((uint32_t)(oy + 1) * cw) / XZD_DISP_H) - 1;
                break;
            }
            if (sy1 < sy0) {
                sy1 = sy0;
            }
            if (sy1 >= ye) {
                sy1 = ye;
            }
            if (sy0 < ys) {
                sy0 = ys;
            }
            if (sx1 < sx0) {
                sx1 = sx0;
            }
            if (sx1 >= xe) {
                sx1 = xe;
            }
            if (sx0 < xs) {
                sx0 = xs;
            }
            cy = (sy0 + sy1) / 2;
            pair = (sx0 >= 2) ? (sx0 & ~1U) : 0;
            pc = src + cy * (uint32_t)sw * 2 + pair * 2;
            v = (int32_t)pc[0] - 128;       /* VYUY: V0 Y0 U0 Y1 */
            u = (int32_t)pc[2] - 128;

            for (yy = sy0; yy <= sy1; yy++) {
                const uint8_t *rrow = src + yy * (uint32_t)sw * 2;
                for (xx = sx0; xx <= sx1; xx++) {
                    ysum += rrow[xx * 2 + 1];   /* VYUY luma at +1 */
                    cnt++;
                }
            }
            if (cnt == 0) {
                cnt = 1;
            }
            c = (int32_t)(ysum / cnt) - 16;
            c = (c * 6) / 5;                /* modest luma gain for visibility */
            r = (298 * c + 409 * v + 128) >> 8;
            g = (298 * c - 100 * u - 208 * v + 128) >> 8;
            b = (298 * c + 516 * u + 128) >> 8;
            r = (r < 0) ? 0 : (r > 255) ? 255 : r;
            g = (g < 0) ? 0 : (g > 255) ? 255 : g;
            b = (b < 0) ? 0 : (b > 255) ? 255 : b;
            rgb = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));

            xzd_ui_logical_to_fb(dst, lx, ly, &fx, &fy);
            ((uint16_t *)dst->frame)[(uint32_t)fy * dst->width + fx] =
                is_swap ? (uint16_t)((rgb >> 8) | (rgb << 8)) : rgb;
        }
    }
}

/* Render one raw camera YUV frame + the countdown overlay into a free panel
 * buffer. Runs inside the camera frame callback. */
static void __xzd_preview_render(const uint8_t *yuv, uint16_t w, uint16_t h)
{
    int32_t free_idx = -1;
    uint32_t i;

    if (NULL == sg_fb_out[0] || NULL == sg_fb_out[1] || NULL == yuv ||
        w == 0 || h == 0) {
        return;
    }
    for (i = 0; i < 2; i++) {
        if (false == sg_fb_out_busy[i]) {
            free_idx = (int32_t)i;
            break;
        }
    }
    if (free_idx < 0) {
        return; /* both buffers busy: drop this frame */
    }

    __xzd_yuv422_raw_fill(yuv, w, h, XZD_PREVIEW_ORI, XZD_PREVIEW_FLIP_VERT,
                          XZD_PREVIEW_CROP, sg_fb_out[free_idx],
                          sg_disp_info.is_swap);

    /* Freeze one clean frame (without the digit) for the "正在识别" page. */
    if (sg_freeze_req && !sg_frozen && NULL != sg_ui_base) {
        memcpy(sg_ui_base->frame, sg_fb_out[free_idx]->frame, sg_ui_base->len);
        sg_frozen = true;
    }

    /* Reference SCAN page: action pill, corner brackets, translucent big
     * countdown digit and bottom hint over the live preview. */
    if (sg_countdown_disp >= 1 && sg_countdown_disp <= 3) {
        char line[32];

        snprintf(line, sizeof(line), "%s %s",
                 strcmp(sg_action, "out") == 0 ? "取出" : "放入", sg_container);
        xzd_ui_fill_round_rect(sg_fb_out[free_idx], 66, 12, 174, 36, 12,
                               XZD_COLOR_BLACK, 98);
        xzd_ui_draw_text_center(sg_fb_out[free_idx], XZD_DISP_W / 2, 16,
                                line, XZD_COLOR_WHITE, 255);
        xzd_ui_draw_corners(sg_fb_out[free_idx], 18, 18, 222, 302, 22,
                            XZD_COLOR_CORNER, 235);
        xzd_ui_draw_big_digit(sg_fb_out[free_idx], (char)('0' + sg_countdown_disp),
                              XZD_DISP_W / 2, 155, XZD_COLOR_WHITE, 150);
        xzd_ui_fill_round_rect(sg_fb_out[free_idx], 32, 272, 208, 314, 21,
                               XZD_COLOR_BLACK, 130);
        xzd_ui_draw_text_center(sg_fb_out[free_idx], XZD_DISP_W / 2, 280,
                                "请对准镜头", XZD_COLOR_WHITE, 255);
        snprintf(line, sizeof(line), "%d 秒后拍照", (int)sg_countdown_disp);
        xzd_ui_draw_text_center(sg_fb_out[free_idx], XZD_DISP_W / 2, 296,
                                line, XZD_COLOR_WHITE, 210);
    }

    sg_fb_out[free_idx]->free_cb = __xzd_out_fb_free_cb;
    sg_fb_out_busy[free_idx] = true;
    tdl_disp_dev_flush(sg_disp_hdl, sg_fb_out[free_idx]);
}

static OPERATE_RET __xzd_frame_cb(TDL_CAMERA_HANDLE_T hdl, TDL_CAMERA_FRAME_T *frame)
{
    (void)hdl;
    if (NULL == frame || NULL == frame->data) {
        return OPRT_INVALID_PARM;
    }
    if (frame->fmt != TUYA_FRAME_FMT_YUV422) {
        return OPRT_OK;
    }
    /* Live preview: scale + rotate this raw UYVY/YUYV frame onto the panel. */
    if (sg_state == XZD_STATE_PREVIEW) {
        __xzd_preview_render(frame->data, frame->width, frame->height);
    }
    return OPRT_OK;
}

static OPERATE_RET __xzd_jpeg_cb(TDL_CAMERA_HANDLE_T hdl, TDL_CAMERA_FRAME_T *frame)
{
    (void)hdl;

    if (NULL == frame || NULL == frame->data || frame->fmt != TUYA_FRAME_FMT_JPEG) {
        return OPRT_OK;
    }
    if (!frame->is_complete || frame->data_len == 0) {
        return OPRT_OK;
    }
    if (frame->data_len > XZD_JPEG_BUF_CAP) {
        return OPRT_OK;
    }

    /* Capture a frame for upload when the countdown requests it. */
    if (sg_capture_req && !sg_capture_done && NULL != sg_jpeg_buf) {
        memcpy(sg_jpeg_buf, frame->data, frame->data_len);
        sg_jpeg_len = frame->data_len;
        sg_capture_done = true;
        sg_capture_req = false;
    }

    return OPRT_OK;
}

/***********************************************************
************************ init pieces ***********************
***********************************************************/
static OPERATE_RET __xzd_disp_init(void)
{
    OPERATE_RET rt = OPRT_OK;
    uint8_t bpp;
    uint32_t out_len;

    memset(&sg_disp_info, 0, sizeof(sg_disp_info));
    sg_fb_out[0] = NULL;
    sg_fb_out[1] = NULL;

    sg_disp_hdl = tdl_disp_find_dev(DISPLAY_NAME);
    if (NULL == sg_disp_hdl) {
        PR_ERR("xzd: display %s not found", DISPLAY_NAME);
        return OPRT_NOT_FOUND;
    }
    rt = tdl_disp_dev_get_info(sg_disp_hdl, &sg_disp_info);
    if (rt != OPRT_OK) {
        PR_ERR("xzd: get display info failed rt=%d", rt);
        return rt;
    }
    rt = tdl_disp_dev_open(sg_disp_hdl);
    if (rt != OPRT_OK) {
        PR_ERR("xzd: open display failed rt=%d", rt);
        return rt;
    }
    tdl_disp_set_brightness(sg_disp_hdl, 100);

    PR_NOTICE("xzd: display %ux%u fmt=%d swap=%d rot=%d",
              sg_disp_info.width, sg_disp_info.height, sg_disp_info.fmt,
              sg_disp_info.is_swap, sg_disp_info.rotation);
    {
        char line[64];
        snprintf(line, sizeof(line), "DISP %ux%u swap=%d rot=%d",
                 sg_disp_info.width, sg_disp_info.height,
                 sg_disp_info.is_swap, sg_disp_info.rotation);
        __xzd_report(line);
    }

    xzd_ui_set_swap(sg_disp_info.is_swap);

    bpp = tdl_disp_get_fmt_bpp(sg_disp_info.fmt);
    bpp = (uint8_t)((bpp + 7) / 8);
    out_len = (uint32_t)sg_disp_info.width * sg_disp_info.height * bpp;

    /* Panel mounting compensation (mirror both axes if mounted upside-down). */
    xzd_ui_set_flip(XZD_SCREEN_FLIP_HORIZONTAL, XZD_SCREEN_FLIP_VERTICAL);

    for (uint32_t i = 0; i < 2; i++) {
        sg_fb_out[i] = tdl_disp_create_frame_buff(DISP_FB_TP_PSRAM, out_len);
        if (NULL == sg_fb_out[i]) {
            PR_ERR("xzd: preview fb %u alloc failed", i);
            return OPRT_MALLOC_FAILED;
        }
        sg_fb_out[i]->x_start = 0;
        sg_fb_out[i]->y_start = 0;
        sg_fb_out[i]->fmt     = sg_disp_info.fmt;
        sg_fb_out[i]->width   = sg_disp_info.width;
        sg_fb_out[i]->height  = sg_disp_info.height;
        sg_fb_out[i]->len     = out_len;
        sg_fb_out[i]->free_cb = NULL;
        sg_fb_out_busy[i]     = false;
    }

    sg_ui_fb = tdl_disp_create_frame_buff(DISP_FB_TP_PSRAM, out_len);
    sg_ui_base = tdl_disp_create_frame_buff(DISP_FB_TP_PSRAM, out_len);
    if (NULL == sg_ui_fb || NULL == sg_ui_base) {
        PR_ERR("xzd: ui fb alloc failed");
        return OPRT_MALLOC_FAILED;
    }
    sg_ui_fb->x_start = 0;
    sg_ui_fb->y_start = 0;
    sg_ui_fb->fmt     = sg_disp_info.fmt;
    sg_ui_fb->width   = sg_disp_info.width;
    sg_ui_fb->height  = sg_disp_info.height;
    sg_ui_fb->len     = out_len;
    sg_ui_fb->free_cb = NULL;
    sg_ui_base->x_start = 0;
    sg_ui_base->y_start = 0;
    sg_ui_base->fmt     = sg_disp_info.fmt;
    sg_ui_base->width   = sg_disp_info.width;
    sg_ui_base->height  = sg_disp_info.height;
    sg_ui_base->len     = out_len;
    sg_ui_base->free_cb = NULL;

    sg_jpeg_buf = (uint8_t *)tal_psram_malloc(XZD_JPEG_BUF_CAP);
    if (NULL == sg_jpeg_buf) {
        PR_ERR("xzd: jpeg buffer alloc failed");
        return OPRT_MALLOC_FAILED;
    }
    sg_upload_jpeg[0] = (uint8_t *)tal_psram_malloc(XZD_JPEG_BUF_CAP);
    sg_upload_jpeg[1] = (uint8_t *)tal_psram_malloc(XZD_JPEG_BUF_CAP);
    if (NULL == sg_upload_jpeg[0] || NULL == sg_upload_jpeg[1]) {
        PR_ERR("xzd: upload jpeg buffer alloc failed");
        return OPRT_MALLOC_FAILED;
    }

    return OPRT_OK;
}

static OPERATE_RET __xzd_touch_init(void)
{
    sg_tp_hdl = tdl_tp_find_dev(DISPLAY_NAME);
    if (NULL == sg_tp_hdl) {
        PR_ERR("xzd: touch device '%s' not found", DISPLAY_NAME);
        return OPRT_NOT_FOUND;
    }
    return tdl_tp_dev_open(sg_tp_hdl);
}

static void __xzd_button_cb(char *name, TDL_BUTTON_TOUCH_EVENT_E event, void *argc)
{
    (void)name;
    (void)argc;
    if (event == TDL_BUTTON_PRESS_DOWN) {
        sg_btn_pressed = true;
    } else if (event == TDL_BUTTON_LONG_PRESS_START) {
        sg_calibrate = !sg_calibrate;
        __xzd_render_standby();
    }
}

static OPERATE_RET __xzd_button_init(void)
{
    OPERATE_RET rt;
    TDL_BUTTON_CFG_T cfg = {
        .long_start_valid_time   = 2000,
        .long_keep_timer         = 500,
        .button_debounce_time    = 30,
        .button_repeat_valid_count = 0,
        .button_repeat_valid_time  = 0,
    };

    rt = tdl_button_create(BUTTON_NAME, &cfg, &sg_btn_hdl);
    if (rt != OPRT_OK || NULL == sg_btn_hdl) {
        PR_WARN("xzd: button create failed %d (continue without it)", rt);
        return OPRT_OK;
    }
    tdl_button_event_register(sg_btn_hdl, TDL_BUTTON_PRESS_DOWN, __xzd_button_cb);
    tdl_button_event_register(sg_btn_hdl, TDL_BUTTON_LONG_PRESS_START, __xzd_button_cb);
    return OPRT_OK;
}

static OPERATE_RET __xzd_camera_init(void)
{
    OPERATE_RET rt;
    TDL_CAMERA_DEV_INFO_T cam_info;
    TDL_CAMERA_CFG_T cam_cfg;

    memset(&cam_info, 0, sizeof(cam_info));
    memset(&cam_cfg, 0, sizeof(cam_cfg));

    sg_cam_hdl = tdl_camera_find_dev(CAMERA_NAME);
    if (NULL == sg_cam_hdl) {
        PR_ERR("xzd: camera '%s' not found", CAMERA_NAME);
        return OPRT_NOT_FOUND;
    }
    rt = tdl_camera_dev_get_info(sg_cam_hdl, &cam_info);
    if (rt != OPRT_OK) {
        PR_ERR("xzd: get camera info failed %d", rt);
        return rt;
    }
    /* The T5AI DVP delivers YUYV (tkl_dvp.c: YUV_FORMAT_YUYV); the driver's
     * yuv_order field is not initialised (garbage), so the preview converter
     * hardcodes YUYV instead of trusting it. */
    PR_NOTICE("xzd: camera max %ux%u yuv=YUYV", cam_info.max_width, cam_info.max_height);
    {
        char line[64];
        snprintf(line, sizeof(line), "CAM %ux%u order=YUYV",
                 XZD_CAM_WIDTH, XZD_CAM_HEIGHT);
        __xzd_report(line);
    }

    cam_cfg.fps         = XZD_CAM_FPS;
    cam_cfg.width       = XZD_CAM_WIDTH;
    cam_cfg.height      = XZD_CAM_HEIGHT;
    cam_cfg.out_fmt     = TDL_CAMERA_FMT_JPEG_YUV422_BOTH;
    cam_cfg.get_frame_cb        = __xzd_frame_cb;
    cam_cfg.get_encoded_frame_cb = __xzd_jpeg_cb;
    cam_cfg.encoded_quality.jpeg_cfg = (JPEG_CFG){
        .enable   = 1,
        .max_size = XZD_JPEG_MAX_SIZE_KB,
        .min_size = XZD_JPEG_MIN_SIZE_KB,
    };

    return tdl_camera_dev_open(sg_cam_hdl, &cam_cfg);
}

/***********************************************************
************************ upload task ***********************
***********************************************************/
static void __xzd_upload_task(void *arg)
{
    XZD_UPLOAD_JOB_T job;
    uint32_t jpeg_len;

    (void)arg;
    while (1) {
        if (tal_queue_fetch(sg_upload_q, &job, SEM_WAIT_FOREVER) != OPRT_OK) {
            continue;
        }
        jpeg_len = sg_upload_len[job.parity];
        PR_NOTICE("xzd: uploading action=%s parity=%u jpeg=%u", job.action, job.parity, jpeg_len);
        sg_last_net_result = xzd_net_upload_scan(job.action, job.container,
                                                 sg_upload_jpeg[job.parity], jpeg_len, &sg_result);
        sg_upload_seq_done = job.seq;
        sg_upload_finished = true;
        sg_upload_busy = false;
        PR_NOTICE("xzd: upload finished result=%d", (int)sg_last_net_result);
    }
}

/***********************************************************
******************** housekeeping task *********************
***********************************************************/
/* Periodic backend sync: heartbeat (device online + event log + stockTotal)
 * then GET /api/device/state for the standby/offline pages. Runs on its own
 * thread; the UI is only touched through the sg_ui_refresh flag so the main
 * loop does the actual rendering. */
static void __xzd_housekeeping_task(void *arg)
{
    bool hb_turn = true;

    (void)arg;
    tal_system_sleep(5000); /* let BOOT -> IDLE settle */
    while (1) {
        if (xzd_net_is_online()) {
            /* Alternate heartbeat and inventory fetch so each 5 s cycle only
             * performs ONE blocking HTTPS call (the task watchdog resets the
             * board if a single cycle blocks too long). */
            if (hb_turn) {
                int stock = -1;

                if (xzd_net_heartbeat(sg_last_event[0] ? sg_last_event : NULL,
                                      NULL, &stock)) {
                    sg_last_event[0] = '\0';
                }
                if (stock >= 0) {
                    sg_stock_count = stock;
                    __xzd_stock_save();
                }
                {
                    char line[64];
                    snprintf(line, sizeof(line), "HB OK stock=%d",
                             stock >= 0 ? stock : -1);
                    __xzd_report(line);
                }
            } else {
                XZD_DEVICE_STATE_T ds;

                /* Fetch the device state (stock + soon/expired + last result).
                 * Deliberately NOT the full /api/items list: that response has
                 * grown past 13 KB and corrupts the platform TLS heap on this
                 * board (housekeeping thread crashes in free()). The device
                 * state payload is ~1 KB and carries everything the standby
                 * page shows. */
                if (xzd_net_fetch_device_state(&ds)) {
                    sg_devstate = ds;
                    sg_devstate_valid = true;
                    xzd_net_devstate_save(&ds);
                    if (ds.stock_total >= 0) {
                        sg_stock_count = ds.stock_total;
                        __xzd_stock_save();
                    }
                    {
                        char line[64];
                        snprintf(line, sizeof(line),
                                 "ITEMS total=%d soon=%d expired=%d",
                                 ds.stock_total, ds.soon_count, ds.expired_count);
                        __xzd_report(line);
                    }
                    sg_ui_refresh = true;
                } else {
                    __xzd_report("ITEMS FAIL");
                }
            }
            hb_turn = !hb_turn;
        } else {
            __xzd_report("HB SKIP OFFLINE");
        }
        tal_system_sleep(XZD_HEARTBEAT_MS);
    }
}

/***********************************************************
************************ UI renderers **********************
***********************************************************/
/* Right-align text so it ends at x1. */
static void __xzd_draw_text_right(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x1, uint16_t y,
                                  const char *s, uint16_t color, uint8_t alpha)
{
    uint16_t w = xzd_ui_text_width(s);
    uint16_t x = (w >= x1) ? 0 : (uint16_t)(x1 - w);

    xzd_ui_draw_text(fb, x, y, s, color, alpha);
}

/* One-line text truncated with "…" when wider than max_w pixels. */
static void __xzd_draw_text_ellipsis(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x, uint16_t y,
                                     const char *s, uint16_t max_w,
                                     uint16_t color, uint8_t alpha)
{
    const char *p = s;
    uint16_t w = 0;
    uint16_t n = 0;

    if (xzd_ui_text_width(s) <= max_w) {
        xzd_ui_draw_text(fb, x, y, s, color, alpha);
        return;
    }
    while (*p) {
        uint8_t c0 = (uint8_t)p[0];
        uint8_t len;
        uint16_t cw;

        if (c0 < 0x80) {
            len = 1;
            cw = 8;
        } else if (c0 < 0xE0) {
            len = 2;
            cw = 16;
        } else {
            len = 3;
            cw = 16;
        }
        if (w + cw > max_w - 16) {
            break;
        }
        w = (uint16_t)(w + cw);
        p += len;
        n = (uint16_t)(p - s);
    }
    if (n == 0) {
        return;
    }
    {
        char line[40];
        uint16_t ln = (n < sizeof(line) - 3) ? n : (uint16_t)(sizeof(line) - 3);

        memcpy(line, s, ln);
        line[ln] = '\0';
        xzd_ui_draw_text(fb, x, y, line, color, alpha);
        xzd_ui_draw_text(fb, (uint16_t)(x + w), y, "…", color, alpha);
    }
}

/* One row of the result card: left label + right-aligned value. */
static void __xzd_render_row(TDL_DISP_FRAME_BUFF_T *fb, uint16_t y,
                             const char *label, const char *value,
                             uint16_t vcolor)
{
    xzd_ui_draw_text(fb, 24, y, label, XZD_COLOR_INK2, 255);
    __xzd_draw_text_right(fb, 216, y, value, vcolor, 255);
}

static void __xzd_civil_from_posix(int64_t posix, int *y, int *m, int *d,
                                   int *hh, int *mi)
{
    int64_t z = posix / 86400;
    int64_t sod = posix % 86400;
    int64_t era;
    uint32_t doe, yoe, doy, mp;

    if (sod < 0) {
        sod += 86400;
        z -= 1;
    }
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (uint32_t)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    *y = (int)((int64_t)yoe + era * 400);
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = *y + (*m <= 2 ? 1 : 0);
    *hh = (int)(sod / 3600);
    *mi = (int)((sod % 3600) / 60);
}

/* Top status bar: real local clock (UTC+8, synced from the server heartbeat)
 * with the last scan's timestamp as fallback, plus the online dot + date. */
static void __xzd_render_status_bar(TDL_DISP_FRAME_BUFF_T *fb, bool with_online)
{
    char time_s[16] = "--:--";
    char date_s[16] = "-- --";
    TIME_T now = tal_time_get_posix();

    if (now > 1600000000) {
        int y, m, d, hh, mi;
        __xzd_civil_from_posix((int64_t)now + 8 * 3600, &y, &m, &d, &hh, &mi);
        snprintf(time_s, sizeof(time_s), "%02d:%02d", hh, mi);
        snprintf(date_s, sizeof(date_s), "%02d-%02d", m, d);
    } else if (sg_last_result.scanned_at[0]) {
        const char *sp = strchr(sg_last_result.scanned_at, ' ');
        if (sp != NULL && sp[1] != '\0') {
            snprintf(time_s, sizeof(time_s), "%s", sp + 1);
        }
        if (strlen(sg_last_result.scanned_at) >= 10) {
            memcpy(date_s, sg_last_result.scanned_at + 5, 5);
            date_s[5] = '\0';
        }
    }
    xzd_ui_draw_text(fb, 8, 3, time_s, XZD_COLOR_INK2, 255);
    if (with_online) {
        xzd_ui_draw_dot(fb, 100, 11, 3,
                        xzd_net_is_online() ? XZD_COLOR_GREEN_OK : XZD_COLOR_INK2);
        xzd_ui_draw_text(fb, 108, 3, xzd_net_is_online() ? "在线" : "离线",
                         XZD_COLOR_INK2, 255);
    } else {
        xzd_ui_draw_dot(fb, 120, 11, 3, XZD_COLOR_GREEN_OK);
    }
    {
        uint16_t dw = xzd_ui_text_width(date_s);
        xzd_ui_draw_text(fb, (uint16_t)(232 - dw), 3, date_s, XZD_COLOR_INK2, 255);
    }
}

static void __xzd_render_standby(void)
{
    char line[96];
    bool near = false;
    int near_cnt = 0;

    if (sg_devstate_valid && sg_devstate.soon_count > 0) {
        near = true;
        near_cnt = sg_devstate.soon_count;
    } else if (sg_last_result.name[0] != '\0' &&
               sg_last_result.days_left >= 0 && sg_last_result.days_left <= 2) {
        near = true;
        near_cnt = 1;
    }

    if (sg_calibrate) {
        /* Touch calibration page: four numbered corner targets (portrait). */
        xzd_ui_fill_full(sg_ui_fb, XZD_COLOR_BG);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 10,
                                "触控标定: 依次点 1 2 3 4", XZD_COLOR_INK, 255);
        xzd_ui_fill_round_rect(sg_ui_fb, 20, 40, 110, 110, 10, XZD_COLOR_RED2, 255);
        xzd_ui_draw_text_center(sg_ui_fb, 65, 67, "1", XZD_COLOR_WHITE, 255);
        xzd_ui_fill_round_rect(sg_ui_fb, 130, 40, 220, 110, 10, XZD_COLOR_GREEN, 255);
        xzd_ui_draw_text_center(sg_ui_fb, 175, 67, "2", XZD_COLOR_WHITE, 255);
        xzd_ui_fill_round_rect(sg_ui_fb, 20, 210, 110, 280, 10, XZD_COLOR_BLUE, 255);
        xzd_ui_draw_text_center(sg_ui_fb, 65, 237, "3", XZD_COLOR_WHITE, 255);
        xzd_ui_fill_round_rect(sg_ui_fb, 130, 210, 220, 280, 10, XZD_COLOR_AMBER, 255);
        xzd_ui_draw_text_center(sg_ui_fb, 175, 237, "4", XZD_COLOR_WHITE, 255);
        __xzd_ui_flush();
        return;
    }

    xzd_ui_fill_full(sg_ui_fb, XZD_COLOR_BG);
    __xzd_render_status_bar(sg_ui_fb, true);

    /* brand row */
    xzd_ui_draw_text(sg_ui_fb, 12, 25, XZD_UI_BRAND, XZD_COLOR_RED2, 255);
    xzd_ui_draw_text(sg_ui_fb, 46, 25, XZD_UI_TAGLINE, XZD_COLOR_INK2, 255);

    /* expiry reminder card */
    xzd_ui_fill_round_rect(sg_ui_fb, 10, 46, 230, 82, 14, XZD_COLOR_AMBER_SOFT, 255);
    if (near) {
        snprintf(line, sizeof(line), "%d 种食材即将过期", near_cnt);
        xzd_ui_draw_text(sg_ui_fb, 20, 51, line, XZD_COLOR_INK, 255);
        if (sg_devstate_valid && sg_devstate.soon_count > 0) {
            if (sg_devstate.soon_count > 1) {
                snprintf(line, sizeof(line), "%s剩%d天 %s剩%d天",
                         sg_devstate.soon_name[0], sg_devstate.soon_days[0],
                         sg_devstate.soon_name[1], sg_devstate.soon_days[1]);
            } else {
                snprintf(line, sizeof(line), "%s剩%d天",
                         sg_devstate.soon_name[0], sg_devstate.soon_days[0]);
            }
        } else {
            if (sg_last_result.days_left == 0) {
                snprintf(line, sizeof(line), "%s 今天到期", sg_last_result.name);
            } else {
                snprintf(line, sizeof(line), "%s 剩%d天", sg_last_result.name,
                         sg_last_result.days_left);
            }
        }
        __xzd_draw_text_ellipsis(sg_ui_fb, 20, 67, line, 200,
                                 XZD_COLOR_AMBER_INK, 255);
    } else {
        xzd_ui_draw_text(sg_ui_fb, 20, 51, "食材状态良好", XZD_COLOR_INK, 255);
        xzd_ui_draw_text(sg_ui_fb, 20, 67, "暂无临期提醒", XZD_COLOR_AMBER_INK, 255);
    }

    /* big action buttons */
    xzd_ui_fill_round_rect_grad(sg_ui_fb, 10, 92, 230, 168, 20,
                                XZD_COLOR_RED_TOP, XZD_COLOR_RED_D);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 122, "放入食材",
                            XZD_COLOR_WHITE, 255);
    xzd_ui_fill_round_rect_bordered(sg_ui_fb, 10, 178, 230, 254, 20, 2,
                                    XZD_COLOR_RED2, XZD_COLOR_CARD);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 208, "取出食材",
                            XZD_COLOR_RED2, 255);

    /* bottom mini stats */
    snprintf(line, sizeof(line), "库存 %d 种", (int)sg_stock_count);
    xzd_ui_draw_text(sg_ui_fb, 10, 268, line, XZD_COLOR_INK2, 255);
    snprintf(line, sizeof(line), "临期 %d", near ? near_cnt : 0);
    xzd_ui_draw_text(sg_ui_fb, 88, 268, line, XZD_COLOR_INK2, 255);
    snprintf(line, sizeof(line), "建议 %s", sg_container);
    xzd_ui_draw_text(sg_ui_fb, 156, 268, line, XZD_COLOR_INK2, 255);
    __xzd_ui_flush();
}

static void __xzd_render_uploading(void)
{
    uint32_t t0;

    if (NULL == sg_ui_base) {
        return;
    }
    /* Wait for any in-flight preview flushes so the recognising page is
     * rendered exactly once and stays static (no bottom-edge flicker). */
    t0 = tal_system_get_millisecond();
    while ((sg_fb_out_busy[0] || sg_fb_out_busy[1]) &&
           (tal_system_get_millisecond() - t0) < 250) {
        tal_system_sleep(2);
    }
    memcpy(sg_ui_fb->frame, sg_ui_base->frame, sg_ui_base->len);
    /* frozen frame + dark mask (deep enough that the sensor's edge noise at
     * the screen bottom blends into the same gray as the rest of the page) */
    xzd_ui_fill(sg_ui_fb, 0, 0, XZD_DISP_W - 1, XZD_DISP_H - 1,
                XZD_COLOR_BLACK, 150);
    xzd_ui_fill_round_rect(sg_ui_fb, 66, 14, 174, 38, 12, XZD_COLOR_GREEN_D, 255);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 18, "照片已上传",
                            XZD_COLOR_WHITE, 255);
    xzd_ui_draw_spinner(sg_ui_fb, XZD_DISP_W / 2, 122, 23, XZD_COLOR_WHITE, 60, 3);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 156, "正在识别…",
                            XZD_COLOR_WHITE, 255);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 180, "云端 AI 解析中",
                            XZD_COLOR_WHITE, 200);
    __xzd_ui_flush();
}

static void __xzd_render_result(void)
{
    char line[96];
    char tbuf[24];
    bool out = (strcmp(sg_action, "out") == 0);
    uint8_t i;

    xzd_ui_fill_full(sg_ui_fb, XZD_COLOR_BG);
    __xzd_render_status_bar(sg_ui_fb, false);

    /* mode pill */
    if (out) {
        xzd_ui_fill_round_rect(sg_ui_fb, 16, 26, 224, 44, 9, XZD_COLOR_BLUE_SOFT, 255);
        xzd_ui_draw_text(sg_ui_fb, 24, 28, "识别成功 取出",
                         XZD_COLOR_BLUE2, 255);
    } else {
        xzd_ui_fill_round_rect(sg_ui_fb, 16, 26, 224, 44, 9, XZD_COLOR_GREEN_SOFT, 255);
        snprintf(line, sizeof(line), "识别成功 置信度 %d%%",
                 (int)(sg_result.confidence * 100.0f + 0.5f));
        xzd_ui_draw_text(sg_ui_fb, 24, 28, line, XZD_COLOR_GREEN_D, 255);
    }

    /* product name */
    __xzd_draw_text_ellipsis(sg_ui_fb, 24, 50,
                             sg_result.name[0] ? sg_result.name : "未知食材",
                             208, XZD_COLOR_INK, 255);

    /* info card */
    xzd_ui_fill_round_rect(sg_ui_fb, 12, 76, 228, out ? 190 : 206, 14,
                           XZD_COLOR_CARD, 255);

    snprintf(tbuf, sizeof(tbuf), "%s",
             sg_result.scanned_at[0] ? sg_result.scanned_at : "--");
    __xzd_render_row(sg_ui_fb, 88, "时间", tbuf, XZD_COLOR_INK);

    if (out) {
        /* Same metric as the standby page / cloud app: the synced inventory
         * row count (sg_stock_count), not the backend's alternate stockTotal. */
        snprintf(line, sizeof(line), "%d 瓶", (int)sg_stock_count);
        __xzd_render_row(sg_ui_fb, 116, "原库存", line, XZD_COLOR_INK);
        snprintf(line, sizeof(line), "%d 瓶",
                 sg_stock_count > 0 ? (int)(sg_stock_count - 1) : 0);
        __xzd_render_row(sg_ui_fb, 144, "取出后", line, XZD_COLOR_INK);
        if (sg_result.days_left >= 0) {
            snprintf(line, sizeof(line), "%s 剩 %d 天",
                     sg_result.expiry_date[0] ? sg_result.expiry_date : "--",
                     sg_result.days_left);
        } else {
            snprintf(line, sizeof(line), "%s",
                     sg_result.expiry_date[0] ? sg_result.expiry_date : "--");
        }
        __xzd_render_row(sg_ui_fb, 172, "到期日", line, XZD_COLOR_RED2);
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

        /* container chips (two rows, selected chip filled red) */
        for (i = 0; i < XZD_CONTAINER_COUNT; i++) {
            uint16_t col = (i < 3) ? i : (uint16_t)(i - 3);
            uint16_t cx0 = (uint16_t)(XZD_CHIP_X0 + col * (XZD_CHIP_W + XZD_CHIP_GAP));
            uint16_t cy0 = (i < 3) ? XZD_CHIP_Y1 : XZD_CHIP_Y2;
            uint16_t cx1 = (uint16_t)(cx0 + XZD_CHIP_W - 1);
            uint16_t cy1 = (uint16_t)(cy0 + XZD_CHIP_H - 1);
            bool sel = (strcmp(sg_container, XZD_CONTAINER_NAMES[i]) == 0);

            if (sel) {
                xzd_ui_fill_round_rect(sg_ui_fb, cx0, cy0, cx1, cy1, 12,
                                       XZD_COLOR_RED2, 255);
                xzd_ui_draw_text_center(sg_ui_fb, (uint16_t)(cx0 + XZD_CHIP_W / 2),
                                        (uint16_t)(cy0 + 4),
                                        XZD_CONTAINER_NAMES[i], XZD_COLOR_WHITE, 255);
            } else {
                xzd_ui_fill_round_rect_bordered(sg_ui_fb, cx0, cy0, cx1, cy1,
                                                12, 1, XZD_COLOR_LINE,
                                                XZD_COLOR_CARD);
                xzd_ui_draw_text_center(sg_ui_fb, (uint16_t)(cx0 + XZD_CHIP_W / 2),
                                        (uint16_t)(cy0 + 4),
                                        XZD_CONTAINER_NAMES[i], XZD_COLOR_INK2, 255);
            }
        }
    }

    /* confirm + retake */
    if (out) {
        xzd_ui_fill_round_rect(sg_ui_fb, 12, 216, 228, 250, 14, XZD_COLOR_INK, 255);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 226, "确认出库",
                                XZD_COLOR_WHITE, 255);
    } else {
        xzd_ui_fill_round_rect_grad(sg_ui_fb, 12, 216, 228, 250, 14,
                                    XZD_COLOR_RED_TOP, XZD_COLOR_RED_D);
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 226, "确认入库",
                                XZD_COLOR_WHITE, 255);
    }
    xzd_ui_fill_round_rect_bordered(sg_ui_fb, 12, 258, 228, 292, 14, 2,
                                    XZD_COLOR_RED2, XZD_COLOR_CARD);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 268, "重拍", XZD_COLOR_RED2, 255);
    __xzd_ui_flush();
}

static void __xzd_render_error(void)
{
    bool low = (sg_err == XZD_ERR_LOW_CONF);
    const char *t1;
    const char *t2;
    const char *t3;
    const char *btn;

    xzd_ui_fill_full(sg_ui_fb, XZD_COLOR_BG);
    __xzd_render_status_bar(sg_ui_fb, false);
    xzd_ui_draw_circle(sg_ui_fb, XZD_DISP_W / 2, 106, 34,
                       low ? XZD_COLOR_ERR_SOFT : XZD_COLOR_AMBER_SOFT, 255);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 98,
                            low ? "?" : "!", XZD_COLOR_RED2, 255);

    if (low) {
        t1 = "没看清，再试一次";
        t2 = "识别置信度过低";
        t3 = "未写入库存，请对准再拍";
        btn = "重新扫描";
    } else {
        t1 = "网络不好，请重试";
        t2 = "照片未上传成功";
        t3 = "本次动作已保留，不会丢失";
        btn = "重试";
    }
    xzd_ui_draw_text(sg_ui_fb, 16, 154, t1, XZD_COLOR_INK, 255);
    xzd_ui_draw_text(sg_ui_fb, 16, 176, t2, XZD_COLOR_INK2, 255);
    xzd_ui_draw_text(sg_ui_fb, 16, 196, t3, XZD_COLOR_INK2, 255);

    xzd_ui_fill_round_rect_grad(sg_ui_fb, 12, 236, 228, 278, 14,
                                XZD_COLOR_RED_TOP, XZD_COLOR_RED_D);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 250, btn, XZD_COLOR_WHITE, 255);
    xzd_ui_fill_round_rect_bordered(sg_ui_fb, 12, 286, 228, 318, 14, 2,
                                    XZD_COLOR_RED2, XZD_COLOR_CARD);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 294, "返回待机",
                            XZD_COLOR_RED2, 255);
    __xzd_ui_flush();
}

static void __xzd_render_offline(void)
{
    char line[96];
    uint16_t note_y = 140;
    uint8_t shown = 0;

    xzd_ui_fill_full(sg_ui_fb, XZD_COLOR_BG);
    xzd_ui_fill(sg_ui_fb, 0, 0, XZD_DISP_W - 1, 26, XZD_COLOR_BROWN_BAR, 255);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 5, "当前离线 本地模式",
                            XZD_COLOR_WHITE, 255);
    xzd_ui_draw_text(sg_ui_fb, 14, 34, "最近库存", XZD_COLOR_INK, 255);
    xzd_ui_draw_text(sg_ui_fb, 80, 34, "网络恢复后自动同步", XZD_COLOR_INK2, 255);

    if (sg_devstate_valid && sg_devstate.soon_count > 0) {
        uint8_t i;
        for (i = 0; i < 2 && i < (uint8_t)sg_devstate.soon_count; i++) {
            uint16_t y0 = (uint16_t)(56 + i * 44);
            uint16_t c = XZD_COLOR_GREEN_OK;

            xzd_ui_fill_round_rect(sg_ui_fb, 12, y0, 228, (uint16_t)(y0 + 36),
                                   12, XZD_COLOR_CARD, 255);
            xzd_ui_draw_dot(sg_ui_fb, 26, (uint16_t)(y0 + 18), 5, XZD_COLOR_AMBER);
            __xzd_draw_text_ellipsis(sg_ui_fb, 38, (uint16_t)(y0 + 10),
                                     sg_devstate.soon_name[i], 110,
                                     XZD_COLOR_INK, 255);
            if (sg_devstate.soon_days[i] == 0) {
                snprintf(line, sizeof(line), "今天到期");
                c = XZD_COLOR_RED2;
            } else {
                snprintf(line, sizeof(line), "剩 %d 天", sg_devstate.soon_days[i]);
                c = XZD_COLOR_AMBER;
            }
            __xzd_draw_text_right(sg_ui_fb, 218, (uint16_t)(y0 + 10), line, c, 255);
            shown++;
        }
        if (sg_devstate.expired_count > 0) {
            snprintf(line, sizeof(line), "%d 种已过期", sg_devstate.expired_count);
            xzd_ui_draw_text(sg_ui_fb, 14, (uint16_t)(56 + shown * 44 + 38),
                             line, XZD_COLOR_RED2, 255);
            note_y = (uint16_t)(140 + shown * 24);
        }
    } else if (sg_last_result.name[0] != '\0') {
        uint16_t c = XZD_COLOR_GREEN_OK;

        xzd_ui_fill_round_rect(sg_ui_fb, 12, 56, 228, 92, 12, XZD_COLOR_CARD, 255);
        xzd_ui_draw_dot(sg_ui_fb, 26, 74, 5, XZD_COLOR_AMBER);
        __xzd_draw_text_ellipsis(sg_ui_fb, 38, 66, sg_last_result.name, 110,
                                 XZD_COLOR_INK, 255);
        if (sg_last_result.days_left == 0) {
            snprintf(line, sizeof(line), "今天到期");
            c = XZD_COLOR_RED2;
        } else if (sg_last_result.days_left > 0 && sg_last_result.days_left <= 3) {
            snprintf(line, sizeof(line), "剩 %d 天", sg_last_result.days_left);
            c = XZD_COLOR_AMBER;
        } else if (sg_last_result.days_left > 0) {
            snprintf(line, sizeof(line), "剩 %d 天", sg_last_result.days_left);
        } else {
            snprintf(line, sizeof(line), "已过期待处理");
            c = XZD_COLOR_RED2;
        }
        __xzd_draw_text_right(sg_ui_fb, 218, 66, line, c, 255);
    } else {
        xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 70, "暂无本地记录",
                                XZD_COLOR_INK2, 255);
    }

    xzd_ui_fill_round_rect(sg_ui_fb, 12, note_y, 228, (uint16_t)(note_y + 48), 12,
                           XZD_COLOR_AMBER_SOFT, 255);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, (uint16_t)(note_y + 8),
                            "断网状态下无法扫描识别",
                            XZD_COLOR_AMBER_INK, 255);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, (uint16_t)(note_y + 28),
                            "请检查网络，恢复后重试",
                            XZD_COLOR_AMBER_INK, 255);

    xzd_ui_fill_round_rect_bordered(sg_ui_fb, 12, 268, 228, 308, 14, 2,
                                    XZD_COLOR_RED2, XZD_COLOR_CARD);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 282, "重新连接",
                            XZD_COLOR_RED2, 255);
    __xzd_ui_flush();
}

static void __xzd_render_boot(void)
{
    xzd_ui_fill_round_rect_grad(sg_ui_fb, 0, 0, XZD_DISP_W - 1, XZD_DISP_H - 1, 0,
                                XZD_COLOR_BG, XZD_COLOR_WARM2);
    xzd_ui_fill_round_rect_grad(sg_ui_fb, 82, 64, 158, 140, 22,
                                XZD_COLOR_RED_TOP, XZD_COLOR_RED_D);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 92, XZD_UI_LOGO_CHAR,
                            XZD_COLOR_WHITE, 255);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 156, XZD_UI_BRAND,
                            XZD_COLOR_RED2, 255);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 180, XZD_UI_TAGLINE,
                            XZD_COLOR_INK2, 255);
    xzd_ui_draw_spinner(sg_ui_fb, 96, 230, 8, XZD_COLOR_RED2, 80,
                        (int16_t)((tal_system_get_millisecond() / 80) % 12));
    xzd_ui_draw_text(sg_ui_fb, 112, 222, "正在连接云端…", XZD_COLOR_INK2, 255);
    xzd_ui_draw_text_center(sg_ui_fb, XZD_DISP_W / 2, 300, XZD_UI_VERSION,
                            XZD_COLOR_INK2, 200);
    __xzd_ui_flush();
}

/***********************************************************
************************ flow control **********************
***********************************************************/
static void __xzd_goto_standby(void)
{
    sg_state = XZD_STATE_STANDBY;
    sg_capture_req = false;
    sg_capture_done = false;
    sg_freeze_req = false;
    __xzd_render_standby();
}

static void __xzd_goto_boot(void)
{
    sg_state = XZD_STATE_BOOT;
    sg_boot_start_ms = tal_system_get_millisecond();
    __xzd_render_boot();
}

static void __xzd_goto_offline(void)
{
    sg_state = XZD_STATE_OFFLINE;
    __xzd_render_offline();
}

static void __xzd_start_scan(const char *action)
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
}

static void __xzd_on_captured(void)
{
    XZD_UPLOAD_JOB_T job;
    uint8_t parity;
    uint32_t jlen;

    if (sg_upload_busy || NULL == sg_upload_jpeg[0] || NULL == sg_upload_jpeg[1]) {
        sg_state = XZD_STATE_ERROR;
        sg_err = XZD_ERR_NET;
        __xzd_render_error();
        __xzd_report("UPLOAD BUSY");
        return;
    }

    xzd_audio_play_click();
    sg_state = XZD_STATE_UPLOADING;
    sg_upload_finished = false;
    sg_upload_start_ms = tal_system_get_millisecond();
    sg_dots = 0;
    sg_last_dots_ms = sg_upload_start_ms;

    snprintf(job.action, sizeof(job.action), "%s", sg_action);
    snprintf(job.container, sizeof(job.container), "%s", sg_container);
    sg_upload_seq_sent = (uint8_t)(sg_upload_seq_sent + 1);
    job.seq = sg_upload_seq_sent;
    parity = sg_upload_parity;
    job.parity = parity;
    jlen = sg_jpeg_len;
    memcpy(sg_upload_jpeg[parity], sg_jpeg_buf, jlen);
    sg_upload_len[parity] = jlen;
    sg_upload_parity = (uint8_t)(parity ^ 1);
    sg_upload_busy = true;
    if (sg_upload_q) {
        tal_queue_post(sg_upload_q, &job, 100);
    } else {
        sg_upload_finished = true;
        sg_last_net_result = XZD_NET_ERR_HTTP;
    }
    __xzd_render_uploading();
    __xzd_report("UPLOAD START");
}

static void __xzd_tick_preview(uint32_t now)
{
    uint32_t el = now - sg_countdown_start_ms;
    uint8_t v = 3;

    if (el >= 2000) {
        v = 1;
    } else if (el >= 1000) {
        v = 2;
    }
    sg_countdown_disp = v;
    if (v != sg_countdown_last) {
        sg_countdown_last = v;
        xzd_audio_play_tick(); /* 叮 for every count digit */
    }

    if (el < XZD_COUNTDOWN_MS) {
        return;
    }

    if (!sg_capture_req && !sg_capture_done) {
        sg_capture_req = true;
        sg_freeze_req = true;
        sg_capture_wait_start = now;
    }

    if (sg_capture_done && (sg_frozen || (now - sg_capture_wait_start) > 500)) {
        __xzd_on_captured();
    } else if (sg_capture_req && (now - sg_capture_wait_start) > XZD_CAPTURE_WAIT_MS) {
        sg_capture_req = false;
        sg_capture_done = false;
        sg_state = XZD_STATE_ERROR;
        sg_err = XZD_ERR_CAPTURE;
        __xzd_render_error();
        __xzd_report("CAPTURE TIMEOUT");
    }
}

static void __xzd_tick_uploading(uint32_t now)
{
    if (sg_upload_finished && sg_upload_seq_done == sg_upload_seq_sent) {
        switch (sg_last_net_result) {
        case XZD_NET_OK:
            sg_state = XZD_STATE_RESULT;
            snprintf(sg_last_event, sizeof(sg_last_event), "SCAN_OK");
            xzd_net_last_result_save(&sg_result);
            sg_last_result = sg_result;
            __xzd_render_result();
            __xzd_report("SCAN OK");
            break;
        case XZD_NET_ERR_OFFLINE:
            sg_state = XZD_STATE_OFFLINE;
            snprintf(sg_last_event, sizeof(sg_last_event), "OFFLINE");
            __xzd_render_offline();
            __xzd_report("OFFLINE");
            break;
        case XZD_NET_ERR_LOW_CONF:
            sg_state = XZD_STATE_ERROR;
            snprintf(sg_last_event, sizeof(sg_last_event), "LOW_CONF");
            sg_err = XZD_ERR_LOW_CONF;
            __xzd_render_error();
            __xzd_report("LOW CONFIDENCE");
            break;
        default:
            sg_state = XZD_STATE_ERROR;
            snprintf(sg_last_event, sizeof(sg_last_event), "UPLOAD_FAIL");
            sg_err = XZD_ERR_NET;
            __xzd_render_error();
            __xzd_report("UPLOAD FAIL");
            break;
        }
        return;
    }

    if ((now - sg_upload_start_ms) > (XZD_UPLOAD_TIMEOUT_MS + 10000)) {
        sg_state = XZD_STATE_ERROR;
        sg_err = XZD_ERR_NET;
        __xzd_render_error();
        __xzd_report("UPLOAD TIMEOUT");
        return;
    }

}

static void __xzd_confirm_action(void)
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
}

static void __xzd_handle_touch(void)
{
    TDL_TP_POS_T points[XZD_TP_POINT_MAX];
    uint8_t count = 0;
    bool pressed;

    if (NULL == sg_tp_hdl) {
        return;
    }
    if (tdl_tp_dev_read(sg_tp_hdl, XZD_TP_POINT_MAX, points, &count) != OPRT_OK) {
        return;
    }
    pressed = (count > 0);

    if (pressed && !sg_tp_pressed) {
        uint16_t x = 0;
        uint16_t y = 0;

        if (sg_calibrate) {
            /* Report raw touch + which of 4 candidate transforms (identity,
             * mirror X, mirror Y, mirror both) hit which numbered target
             * (1=TL, 2=TR, 3=BL, 4=BR in logical portrait space). */
            uint16_t rx = points[0].x;
            uint16_t ry = points[0].y;
            char line[128];
            int n = snprintf(line, sizeof(line), "CAL raw=%u,%u", rx, ry);
            int t;

            for (t = 0; t < 4; t++) {
                uint16_t mx, my;

                mx = rx;
                my = ry;
                if (t & 1) {
                    mx = (uint16_t)(XZD_UI_W - 1 - mx);
                }
                if (t & 2) {
                    my = (uint16_t)(XZD_UI_H - 1 - my);
                }
                if (xzd_ui_hit(mx, my, 20, 40, 110, 110)) {
                    n += snprintf(line + n, sizeof(line) - (size_t)n, " t%d:1", t);
                } else if (xzd_ui_hit(mx, my, 130, 40, 220, 110)) {
                    n += snprintf(line + n, sizeof(line) - (size_t)n, " t%d:2", t);
                } else if (xzd_ui_hit(mx, my, 20, 210, 110, 280)) {
                    n += snprintf(line + n, sizeof(line) - (size_t)n, " t%d:3", t);
                } else if (xzd_ui_hit(mx, my, 130, 210, 220, 280)) {
                    n += snprintf(line + n, sizeof(line) - (size_t)n, " t%d:4", t);
                }
            }
            __xzd_report(line);
            sg_tp_pressed = pressed;
            return;
        }

        /* Panel/touch coordinates -> logical portrait coordinates. */
        if (NULL != sg_ui_fb) {
            xzd_ui_fb_to_logical(sg_ui_fb, points[0].x, points[0].y, &x, &y);
        }
        __xzd_report_tap(x, y);

        switch (sg_state) {
        case XZD_STATE_STANDBY:
            if (xzd_ui_hit(x, y, 10, 92, 230, 168)) {
                __xzd_start_scan("in");
            } else if (xzd_ui_hit(x, y, 10, 178, 230, 254)) {
                __xzd_start_scan("out");
            }
            break;
        case XZD_STATE_RESULT:
            if (strcmp(sg_action, "in") == 0) {
                uint8_t i;
                for (i = 0; i < XZD_CONTAINER_COUNT; i++) {
                    uint16_t col = (i < 3) ? i : (uint16_t)(i - 3);
                    uint16_t cx0 = (uint16_t)(XZD_CHIP_X0 + col * (XZD_CHIP_W + XZD_CHIP_GAP));
                    uint16_t cy0 = (i < 3) ? XZD_CHIP_Y1 : XZD_CHIP_Y2;

                    if (xzd_ui_hit(x, y, cx0, cy0,
                                   (uint16_t)(cx0 + XZD_CHIP_W - 1),
                                   (uint16_t)(cy0 + XZD_CHIP_H - 1))) {
                        snprintf(sg_container, sizeof(sg_container), "%s",
                                 XZD_CONTAINER_NAMES[i]);
                        __xzd_render_result();
                        break;
                    }
                }
            }
            if (xzd_ui_hit(x, y, 12, 216, 228, 250)) {
                __xzd_confirm_action();
            } else if (xzd_ui_hit(x, y, 12, 258, 228, 292)) {
                __xzd_start_scan(sg_action);
            }
            break;
        case XZD_STATE_ERROR:
            if (xzd_ui_hit(x, y, 12, 236, 228, 278)) {
                __xzd_start_scan(sg_action);
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
        }
    }
    sg_tp_pressed = pressed;
}

/***********************************************************
************************ public API ************************
***********************************************************/
OPERATE_RET app_xzd_init(void)
{
    OPERATE_RET rt;
    THREAD_CFG_T thrd = {0};

    PR_NOTICE("xzd app init");
    __xzd_report("xzd app init");

    rt = __xzd_disp_init();
    if (rt != OPRT_OK) {
        __xzd_report("init failed: display");
        return rt;
    }
    __xzd_report("init ok: display");
    rt = __xzd_touch_init();
    if (rt != OPRT_OK) {
        __xzd_report("init failed: touch");
        return rt;
    }
    __xzd_report("init ok: touch");
    __xzd_button_init();
    __xzd_report("init ok: button");
    rt = __xzd_camera_init();
    if (rt != OPRT_OK) {
        __xzd_report("init failed: camera");
        return rt;
    }
    __xzd_report("init ok: camera");
    xzd_audio_init();
    __xzd_report("init ok: audio");
    xzd_net_init();
    __xzd_report("init ok: net");

    __xzd_stock_load();
    if (!xzd_net_last_result_load(&sg_last_result)) {
        memset(&sg_last_result, 0, sizeof(sg_last_result));
    }
    if (!xzd_net_devstate_load(&sg_devstate)) {
        memset(&sg_devstate, 0, sizeof(sg_devstate));
        sg_devstate_valid = false;
    } else {
        sg_devstate_valid = true;
    }

    if (tal_queue_create_init(&sg_upload_q, sizeof(XZD_UPLOAD_JOB_T), 2) != OPRT_OK) {
        PR_ERR("xzd: upload queue create failed");
        return OPRT_COM_ERROR;
    }
    thrd.stackDepth = 1024 * 8;
    thrd.priority = THREAD_PRIO_1;
    thrd.thrdname = "xzd_upload";
    tal_thread_create_and_start(&sg_upload_thrd, NULL, NULL, __xzd_upload_task, NULL, &thrd);
    /* HTTPS + 13 KB inventory JSON parsing needs a deep stack: the previous
     * 4 KB left only ~31 bytes of headroom, overflowed into the heap and
     * crashed free() (reset reason 2). */
    thrd.stackDepth = 1024 * 8;
    thrd.priority = THREAD_PRIO_2;
    thrd.thrdname = "xzd_housekeep";
    tal_thread_create_and_start(&sg_housekeep_thrd, NULL, NULL,
                                __xzd_housekeeping_task, NULL, &thrd);

    sg_last_online = xzd_net_is_online();
    sg_calibrate = false; /* long-press the physical button to enter calibration */
    __xzd_goto_boot();
    PR_NOTICE("xzd app ready");
    __xzd_report("ready");
    return OPRT_OK;
}

void app_xzd_loop(void)
{
    uint32_t now = tal_system_get_millisecond();
    bool online = xzd_net_is_online();

    xzd_net_poll();

    /* Keep the standby page's network status line in sync with the link. */
    if (online != sg_last_online) {
        sg_last_online = online;
        if (sg_state == XZD_STATE_STANDBY) {
            __xzd_render_standby();
        } else if (online && sg_state == XZD_STATE_OFFLINE) {
            __xzd_goto_standby();
        }
    }

    /* Housekeeping thread asked for a standby re-render (new backend data). */
    if (sg_ui_refresh) {
        sg_ui_refresh = false;
        if (sg_state == XZD_STATE_STANDBY) {
            __xzd_render_standby();
        }
    }

    if (sg_btn_pressed) {
        sg_btn_pressed = false;
        if (sg_state == XZD_STATE_PREVIEW || sg_state == XZD_STATE_UPLOADING) {
            PR_NOTICE("xzd: button cancel -> standby");
            __xzd_goto_standby();
        }
    }

    switch (sg_state) {
    case XZD_STATE_BOOT:
        if ((now - sg_boot_start_ms) >= XZD_BOOT_WAIT_MS) {
            if (xzd_net_is_online()) {
                snprintf(sg_last_event, sizeof(sg_last_event), "LINK_UP");
                __xzd_goto_standby();
                __xzd_report("BOOT -> IDLE");
            } else {
                snprintf(sg_last_event, sizeof(sg_last_event), "OFFLINE");
                __xzd_goto_offline();
                __xzd_report("BOOT -> OFFLINE");
            }
        }
        break;
    case XZD_STATE_PREVIEW:
        __xzd_tick_preview(now);
        break;
    case XZD_STATE_UPLOADING:
        __xzd_tick_uploading(now);
        break;
    default:
        break;
    }

    __xzd_handle_touch();
}

void xzd_app_report(const char *line)
{
    __xzd_report(line);
}
