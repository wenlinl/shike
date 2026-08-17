/**
 * @file app_xzd_net.h
 * @brief Networking for XianZhidao: WiFi STA + scan photo upload.
 */
#ifndef __APP_XZD_NET_H__
#define __APP_XZD_NET_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XZD_SCAN_NAME_LEN        64
#define XZD_SCAN_DATETIME_LEN    32
#define XZD_SCAN_CONTAINER_LEN   24
#define XZD_SCAN_NOTE_LEN        96

/** Parsed result of one /api/scan response (item[0]). */
typedef struct {
    char   name[XZD_SCAN_NAME_LEN];
    char   scanned_at[XZD_SCAN_DATETIME_LEN];
    char   expiry_date[XZD_SCAN_DATETIME_LEN];
    int    days_left;
    char   suggested_container[XZD_SCAN_CONTAINER_LEN];
    float  confidence;
    char   note[XZD_SCAN_NOTE_LEN];
    int    stock_total;      /* device stock after this scan (server truth) */
} XZD_SCAN_RESULT_T;

/** Snapshot of GET /api/device/state (device view, cached for offline). */
typedef struct {
    int  stock_total;
    int  soon_count;
    int  expired_count;
    char soon_name[2][XZD_SCAN_NAME_LEN];
    int  soon_days[2];
    char last_name[XZD_SCAN_NAME_LEN];
    int  last_days;
    char last_container[XZD_SCAN_CONTAINER_LEN];
} XZD_DEVICE_STATE_T;

typedef enum {
    XZD_NET_OK = 0,
    XZD_NET_ERR_OFFLINE,   /* no link / WiFi down */
    XZD_NET_ERR_HTTP,      /* transport / TLS / send failed or timeout */
    XZD_NET_ERR_SERVER,    /* HTTP status != 200 or code != 0 */
    XZD_NET_ERR_LOW_CONF,  /* parsed but confidence too low / no item */
    XZD_NET_ERR_BAD_JSON,  /* response body unparseable */
} XZD_NET_RESULT_E;

/**
 * @brief Init KV + TLS + netmgr WiFi STA (blocking-connect via reconnect table).
 */
OPERATE_RET xzd_net_init(void);

/** @brief true when the network link is currently up. */
bool xzd_net_is_online(void);

/** @brief Debug/status line emitted over the app's UART report channel. */
void xzd_app_report(const char *line);

/**
 * @brief Periodic network housekeeping (call from the app loop):
 *        runs one WiFi scan ~20 s after boot when still offline and reports
 *        which APs are visible over UART for debugging.
 */
void xzd_net_poll(void);

/**
 * @brief Blocking multipart/form-data POST of the JPEG to /api/scan.
 * @note Called from the upload worker thread. `jpeg` must stay valid while
 *       this runs.
 */
XZD_NET_RESULT_E xzd_net_upload_scan(const char *action, const char *container,
                                     const uint8_t *jpeg, uint32_t jpeg_len,
                                     XZD_SCAN_RESULT_T *out);

/** @brief Persist the last good result for the offline fallback page. */
void xzd_net_last_result_save(const XZD_SCAN_RESULT_T *r);

/** @brief Load the persisted last result; returns false if none stored. */
bool xzd_net_last_result_load(XZD_SCAN_RESULT_T *out);

/**
 * @brief POST /api/device/heartbeat (JSON) and return server stockTotal.
 * @param event Optional device event string (LINK_UP/SCAN_START/SCAN_OK/...).
 * @param stock_total_out Receives server stock total; -1 if unavailable.
 * @return true on HTTP 200 + code==0.
 */
bool xzd_net_heartbeat(const char *event, const char *msg, int *stock_total_out);

/**
 * @brief GET /api/device/state?deviceId=... (blocking).
 * @return true when parsed successfully.
 */
bool xzd_net_fetch_device_state(XZD_DEVICE_STATE_T *out);

/**
 * @brief GET /api/items (the full shared inventory, same as the web pages).
 *        Fills stock_total with the item count and soon/expired stats so the
 *        hardware screen mirrors the web data (all items, not just this
 *        device's own stock).
 */
bool xzd_net_fetch_items(XZD_DEVICE_STATE_T *out);

/** @brief Persist the last device-state snapshot (offline fallback). */
void xzd_net_devstate_save(const XZD_DEVICE_STATE_T *s);

/** @brief Load the persisted device-state snapshot; false if none stored. */
bool xzd_net_devstate_load(XZD_DEVICE_STATE_T *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_XZD_NET_H__ */
