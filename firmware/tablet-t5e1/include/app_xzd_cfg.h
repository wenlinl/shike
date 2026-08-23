/**
 * @file app_xzd_cfg.h
 * @brief 食刻 firmware configuration.
 *
 * Fill in the WiFi credentials of the deployment network before flashing.
 * No cloud API key is ever stored on the device — the firmware only talks to
 * the project backend at https://tuvmkt.com/api/scan.
 */
#ifndef __APP_XZD_CFG_H__
#define __APP_XZD_CFG_H__

/* ---- WiFi STA credentials (network provisioning for the deployment) ---- */
#define XZD_WIFI_SSID        "your-wifi-ssid"
#define XZD_WIFI_PASS        "123456789"

/* ---- Scan API contract ---- */
#define XZD_SERVER_HOST      "tuvmkt.com"
#define XZD_SERVER_PORT      443
#define XZD_SERVER_PATH      "/api/scan"
#define XZD_DEVICE_ID        "xzd-t5e1-001"
#define XZD_UPLOAD_TIMEOUT_MS 30000

/* Default storage container sent with each scan ("冰箱|零食柜|药盒|调料柜|主食柜").
 * A future UI step can let the user pick this on the standby page. */
#define XZD_DEFAULT_CONTAINER "冰箱"

/* Panel mounting. The reference UI is native portrait 240x320; keep both 0
 * unless the physical panel is mounted upside-down (180 degrees), in which
 * case set both to 1. */
#define XZD_SCREEN_FLIP_VERTICAL 0
#define XZD_SCREEN_FLIP_HORIZONTAL 0

/* Camera preview: the GC2145 DVP delivers raw VYUY frames (V0 Y0 U0 Y1) at
 * 640x480; the preview is rendered directly from those raw frames (not JPEG
 * decode) into the portrait 240x320 screen.
 * - ORI: how the source is mapped onto the screen:
 *     0 = no rotation (portrait centre crop of the landscape frame),
 *     1 = 90 deg counter-clockwise, 2 = 180 deg, 3 = 90 deg clockwise.
 *   Start from 0; if the object is upside-down switch to 2, if it appears
 *   rotated 90 deg the wrong way try 1 or 3.
 * - FLIP_VERT: mirror the preview top-bottom (1 = inverted).
 * - CROP: pixels cropped from each edge of the source frame; the sensor's
 *   edge columns/rows can carry noise, which would show as a flickering
 *   white bar along the matching screen edge. */
#define XZD_PREVIEW_ORI        2
#define XZD_PREVIEW_FLIP_VERT  0
#define XZD_PREVIEW_CROP       24

/* ---- backend integration ---- */
#define XZD_DEVICE_NAME        "食刻-T5E1"
#define XZD_FW_VERSION         "1.0.0"
#define XZD_HEARTBEAT_MS       5000
#define XZD_STATE_FETCH_MS     5000

/* ---- UI branding (matches the reference screens) ---- */
#define XZD_UI_BRAND           "食刻"
#define XZD_UI_TAGLINE         "让每一餐都新鲜安心"
#define XZD_UI_LOGO_CHAR       "食"
#define XZD_UI_VERSION         "固件 v1.0.0 T5-E1"
#define XZD_BOOT_WAIT_MS       2500

/* Storage container list shown as selectable chips on the result page. */
#define XZD_CONTAINER_COUNT    5
extern const char *const XZD_CONTAINER_NAMES[XZD_CONTAINER_COUNT];

/* Minimum confidence for a scan result to be accepted (0.0 ~ 1.0). */
#define XZD_MIN_CONFIDENCE   0.50f

#endif /* __APP_XZD_CFG_H__ */
