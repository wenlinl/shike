/**
 * @file app_xzd_net.c
 * @brief WiFi STA + HTTPS multipart upload + scan JSON parsing.
 *
 * The device holds no API key: it only posts the JPEG to
 * https://tuvmkt.com/api/scan and renders the JSON the backend returns.
 */
#include "tuya_cloud_types.h"
#include "tal_api.h"

#include "http_client_interface.h"
#include "netmgr.h"
#include "netconn_wifi.h"
#include "tuya_tls.h"
#include "tuya_register_center.h"

#include "app_xzd_net.h"
#include "app_xzd_cfg.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define XZD_NET_BODY_CAP       (260 * 1024)   /* multipart body incl. JPEG */
#define XZD_NET_RESP_CAP       (16 * 1024)    /* response JSON cap */
#define XZD_NET_BOUNDARY       "----xzd7f3k9q2a1b"
#define XZD_KV_LAST_KEY        "xzd_last"
#define XZD_KV_STATE_KEY       "xzd_devstate"

/***********************************************************
***********************variable define**********************
***********************************************************/
static volatile netmgr_status_e sg_link_status = NETMGR_LINK_DOWN;
static bool sg_scan_done = false;
static uint32_t sg_net_start_ms = 0;
/* Serialises every HTTPS call (upload vs heartbeat/state sync). The platform
 * TLS stack shares one global mbedTLS entropy/CTR_DRBG instance; concurrent
 * handshakes race on it and crash (fault) or hang the device mid-scan. */
static MUTEX_HANDLE sg_http_mutex = NULL;

/***********************************************************
***********************function define**********************
***********************************************************/
static OPERATE_RET __xzd_link_cb(void *data)
{
    if (NULL != data) {
        sg_link_status = *((netmgr_status_e *)data);
        PR_NOTICE("xzd net: link status -> %s", NETMGR_STATUS_TO_STR(sg_link_status));
        if (sg_link_status == NETMGR_LINK_UP || sg_link_status == NETMGR_LINK_UP_SWITH) {
            xzd_app_report("LINK UP");
        } else {
            xzd_app_report("LINK DOWN");
        }
    }
    return OPRT_OK;
}

OPERATE_RET xzd_net_init(void)
{
    OPERATE_RET rt = OPRT_OK;
    netconn_wifi_info_t wifi_info = {0};
    char line[96];

    sg_net_start_ms = tal_system_get_millisecond();
    tal_kv_init(&(tal_kv_cfg_t){
        .seed = "xzd-seed-8k3j2fq",
        .key  = "xzd-kv-key-2f9ab7",
    });
    tuya_tls_init();
    tuya_register_center_init();
    if (sg_http_mutex == NULL) {
        tal_mutex_create_init(&sg_http_mutex);
    }

    tal_event_subscribe(EVENT_LINK_STATUS_CHG, "xzd_net", __xzd_link_cb, SUBSCRIBE_TYPE_NORMAL);

    rt = netmgr_init(NETCONN_WIFI);
    if (rt != OPRT_OK) {
        PR_ERR("xzd net: netmgr_init failed %d", rt);
        snprintf(line, sizeof(line), "NET INIT FAIL %d", rt);
        xzd_app_report(line);
        return rt;
    }
    xzd_app_report("NET INIT OK");

    snprintf(wifi_info.ssid, sizeof(wifi_info.ssid), "%s", XZD_WIFI_SSID);
    snprintf(wifi_info.pswd, sizeof(wifi_info.pswd), "%s", XZD_WIFI_PASS);
    PR_NOTICE("xzd net: connecting to SSID '%s'", wifi_info.ssid);
    snprintf(line, sizeof(line), "WIFI SET '%s'", wifi_info.ssid);
    xzd_app_report(line);

    rt = netmgr_conn_set(NETCONN_WIFI, NETCONN_CMD_SSID_PSWD, &wifi_info);
    if (rt != OPRT_OK) {
        PR_ERR("xzd net: netmgr_conn_set failed %d", rt);
        snprintf(line, sizeof(line), "WIFI SET FAIL %d", rt);
        xzd_app_report(line);
    }
    return rt;
}

void xzd_net_poll(void)
{
    uint32_t now = tal_system_get_millisecond();
    AP_IF_S *ap = NULL;
    uint32_t num = 0;
    uint32_t i;
    int n;
    char line[180];

    if (sg_scan_done || xzd_net_is_online() || sg_net_start_ms == 0) {
        return;
    }
    if ((now - sg_net_start_ms) < 20000) {
        return;
    }
    sg_scan_done = true;

    if (tal_wifi_all_ap_scan(&ap, &num) != OPRT_OK) {
        xzd_app_report("WIFI SCAN FAIL");
        return;
    }
    n = snprintf(line, sizeof(line), "WIFI SCAN %u:", (unsigned)num);
    for (i = 0; i < num && n < (int)sizeof(line) - 28; i++) {
        n += snprintf(line + n, (size_t)((int)sizeof(line) - n),
                      "%s%s(%u)", i ? "," : "",
                      (const char *)ap[i].ssid, (unsigned)ap[i].channel);
    }
    xzd_app_report(line);
    tal_wifi_release_ap(ap);
}

bool xzd_net_is_online(void)
{
    return (sg_link_status == NETMGR_LINK_UP || sg_link_status == NETMGR_LINK_UP_SWITH);
}

/***********************************************************
********************* tiny JSON helpers ********************
***********************************************************/
static const char *__xzd_json_skip_ws(const char *p, const char *end)
{
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) {
        p++;
    }
    return p;
}

static const char *__xzd_json_find_key(const char *body, size_t len, const char *key)
{
    const char *p = body;
    const char *end = body + len;
    size_t klen = strlen(key);

    while (p + klen + 2 <= end) {
        const char *hit = (const char *)memchr(p, '"', (size_t)(end - p));
        if (NULL == hit) {
            return NULL;
        }
        if ((size_t)(end - hit) >= klen + 2 && hit[1] == key[0] &&
            memcmp(hit + 1, key, klen) == 0 && hit[klen + 1] == '"') {
            const char *v = __xzd_json_skip_ws(hit + klen + 2, end);
            if (v < end && *v == ':') {
                return __xzd_json_skip_ws(v + 1, end);
            }
            return NULL;
        }
        p = hit + 1;
    }
    return NULL;
}

static bool __xzd_json_get_string(const char *body, size_t len, const char *key,
                                  char *out, size_t out_sz)
{
    const char *v = __xzd_json_find_key(body, len, key);
    const char *p;
    const char *end = body + len;
    size_t n = 0;

    if (NULL == v || *v != '"' || out_sz == 0) {
        return false;
    }
    p = v + 1;
    while (p < end && *p != '"' && n + 1 < out_sz) {
        if (*p == '\\') {
            p++; /* skip backslash, copy the escaped char raw */
            if (p >= end) {
                break;
            }
        }
        *out++ = *p++;
        n++;
    }
    *out = '\0';
    return true;
}

static double __xzd_json_get_number(const char *body, size_t len, const char *key)
{
    const char *v = __xzd_json_find_key(body, len, key);
    const char *end = body + len;
    double val = 0.0;
    double frac = 0.1;
    bool neg = false;
    bool any = false;

    if (NULL == v) {
        return 0.0;
    }
    if (*v == '-') {
        neg = true;
        v++;
    }
    while (v < end && *v >= '0' && *v <= '9') {
        val = val * 10.0 + (double)(*v - '0');
        any = true;
        v++;
    }
    if (v < end && *v == '.') {
        v++;
        while (v < end && *v >= '0' && *v <= '9') {
            val += (double)(*v - '0') * frac;
            frac *= 0.1;
            any = true;
            v++;
        }
    }
    if (!any) {
        return 0.0;
    }
    return neg ? -val : val;
}

/* "2026-08-16T04:09:00.000Z" (or ±HH:MM offset) -> POSIX epoch. */
static TIME_T __xzd_parse_iso_epoch(const char *s)
{
    int y, mo, d, h, mi, se;
    int off = 0;
    const char *p;
    int64_t era, days;
    unsigned yoe, doy, doe;

    if (s == NULL || strlen(s) < 19) {
        return 0;
    }
    if (s[4] != '-' || s[7] != '-' ||
        (s[10] != 'T' && s[10] != ' ') || s[13] != ':' || s[16] != ':') {
        return 0;
    }
    y  = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    mo = (s[5] - '0') * 10 + (s[6] - '0');
    d  = (s[8] - '0') * 10 + (s[9] - '0');
    h  = (s[11] - '0') * 10 + (s[12] - '0');
    mi = (s[14] - '0') * 10 + (s[15] - '0');
    se = (s[17] - '0') * 10 + (s[18] - '0');
    p = s + 19;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            p++;
        }
    }
    if (*p == '+' || *p == '-') {
        off = (p[1] - '0') * 10 * 3600 + (p[2] - '0') * 3600 +
              (p[3] - '0') * 10 * 60 + (p[4] - '0') * 60;
        if (*p == '-') {
            off = -off;
        }
    }
    y -= (mo <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (153 * (unsigned)(mo + (mo > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    days = era * 146097 + (int64_t)doe - 719468;
    return (TIME_T)(days * 86400 + h * 3600 + mi * 60 + se - off);
}

/***********************************************************
******************* timestamp (UTC, if set) ****************
***********************************************************/
static void __xzd_civil_from_days(int64_t z, int *y, int *m, int *d)
{
    int64_t era;
    uint32_t doe;
    uint32_t yoe;
    uint32_t doy;
    uint32_t mp;
    int64_t ytmp;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (uint32_t)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    ytmp = (int64_t)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(ytmp + (*m <= 2 ? 1 : 0));
}

static void __xzd_timestamp_now(char *out, size_t out_sz)
{
    TIME_T posix = tal_time_get_posix();
    int64_t days;
    int64_t sod;
    int y, m, d, hh, mi, ss;

    if (posix < 1600000000) { /* clock not synchronised yet */
        snprintf(out, out_sz, "");
        return;
    }
    days = (int64_t)posix / 86400;
    sod = (int64_t)posix % 86400;
    __xzd_civil_from_days(days, &y, &m, &d);
    hh = (int)(sod / 3600);
    mi = (int)((sod % 3600) / 60);
    ss = (int)(sod % 60);
    snprintf(out, out_sz, "%04d-%02d-%02d %02d:%02d:%02d", y, m, d, hh, mi, ss);
}

/***********************************************************
*********************** upload *****************************
***********************************************************/
XZD_NET_RESULT_E xzd_net_upload_scan(const char *action, const char *container,
                                     const uint8_t *jpeg, uint32_t jpeg_len,
                                     XZD_SCAN_RESULT_T *out)
{
    XZD_NET_RESULT_E result = XZD_NET_ERR_HTTP;
    http_client_status_t http_status;
    http_client_response_t resp = {0};
    uint8_t *body = NULL;
    uint32_t body_len = 0;
    char timestamp[32] = {0};
    char line2[64];
    char *p;
    int cap = XZD_NET_BODY_CAP;
    int n;
    const char *bnd = XZD_NET_BOUNDARY;
    double code, confidence;

    if (NULL == out) {
        return XZD_NET_ERR_HTTP;
    }
    memset(out, 0, sizeof(*out));

    if (!xzd_net_is_online()) {
        PR_WARN("xzd net: link down, refuse upload");
        xzd_app_report("UPLOAD: OFFLINE");
        return XZD_NET_ERR_OFFLINE;
    }
    if (NULL == jpeg || jpeg_len < 128) {
        PR_ERR("xzd net: invalid jpeg buffer len=%u", jpeg_len);
        return XZD_NET_ERR_HTTP;
    }

    /* One TLS connection at a time: the platform TLS stack shares a global
     * mbedTLS entropy/CTR_DRBG, so an upload running concurrently with a
     * heartbeat handshake corrupts it (crash/hang). */
    if (NULL != sg_http_mutex) {
        tal_mutex_lock(sg_http_mutex);
    }
    body = (uint8_t *)tal_psram_malloc(XZD_NET_BODY_CAP);
    if (NULL == body) {
        PR_ERR("xzd net: body alloc failed");
        if (NULL != sg_http_mutex) {
            tal_mutex_unlock(sg_http_mutex);
        }
        return XZD_NET_ERR_HTTP;
    }

    __xzd_timestamp_now(timestamp, sizeof(timestamp));
    p = (char *)body;

    n = snprintf(p, (size_t)(cap - (int)(p - (char *)body)),
                 "--%s\r\n"
                 "Content-Disposition: form-data; name=\"image\"; filename=\"scan.jpg\"\r\n"
                 "Content-Type: image/jpeg\r\n\r\n",
                 bnd);
    if (n < 0 || n >= cap) {
        goto __exit;
    }
    p += n;

    if ((uint32_t)(cap - (int)(p - (char *)body)) < jpeg_len + 64) {
        goto __exit;
    }
    memcpy(p, jpeg, jpeg_len);
    p += jpeg_len;

    n = snprintf(p, (size_t)(cap - (int)(p - (char *)body)),
                 "\r\n"
                 "--%s\r\n"
                 "Content-Disposition: form-data; name=\"deviceId\"\r\n\r\n%s\r\n"
                 "--%s\r\n"
                 "Content-Disposition: form-data; name=\"action\"\r\n\r\n%s\r\n"
                 "--%s\r\n"
                 "Content-Disposition: form-data; name=\"container\"\r\n\r\n%s\r\n"
                 "--%s\r\n"
                 "Content-Disposition: form-data; name=\"timestamp\"\r\n\r\n%s\r\n"
                 "--%s--\r\n",
                 bnd, XZD_DEVICE_ID, bnd, action ? action : "in", bnd,
                 container ? container : XZD_DEFAULT_CONTAINER, bnd, timestamp, bnd);
    if (n < 0 || n >= (int)(cap - (int)(p - (char *)body))) {
        goto __exit;
    }
    p += n;
    body_len = (uint32_t)(p - (char *)body);

    http_client_header_t headers[] = {
        {.key = "Content-Type",
         .value = "multipart/form-data; boundary=" XZD_NET_BOUNDARY},
        {.key = "User-Agent", .value = "tuyaopen-xzd-t5e1"},
    };

    http_client_request_t req = {
        .host          = XZD_SERVER_HOST,
        .port          = XZD_SERVER_PORT,
        .path          = XZD_SERVER_PATH,
        .tls_no_verify = true, /* prototype: no embedded CA bundle on device */
        .method        = "POST",
        .headers       = headers,
        .headers_count = (uint8_t)(sizeof(headers) / sizeof(headers[0])),
        .body          = body,
        .body_length   = body_len,
        .timeout_ms    = XZD_UPLOAD_TIMEOUT_MS,
    };

    PR_NOTICE("xzd net: POST %s%s body=%u", XZD_SERVER_HOST, XZD_SERVER_PATH, body_len);
    http_status = http_client_request(&req, &resp);
    if (http_status != HTTP_CLIENT_SUCCESS) {
        PR_ERR("xzd net: http request failed: %d", http_status);
        xzd_app_report("UPLOAD: SEND FAIL");
        result = XZD_NET_ERR_HTTP;
        goto __exit;
    }

    PR_NOTICE("xzd net: HTTP %u, body_len=%u", resp.status_code, (uint32_t)resp.body_length);
    snprintf(line2, sizeof(line2), "UPLOAD: HTTP %u LEN %u",
             resp.status_code, (uint32_t)resp.body_length);
    xzd_app_report(line2);
    if (resp.status_code != 200) {
        result = XZD_NET_ERR_SERVER;
        goto __exit;
    }
    if (NULL == resp.body || resp.body_length == 0) {
        result = XZD_NET_ERR_BAD_JSON;
        goto __exit;
    }
    {
        char body_dump[120];
        size_t bl = resp.body_length;
        if (bl > sizeof(body_dump) - 1) {
            bl = sizeof(body_dump) - 1;
        }
        memcpy(body_dump, resp.body, bl);
        body_dump[bl] = '\0';
        snprintf(line2, sizeof(line2), "UPLOAD: BODY %s", body_dump);
        xzd_app_report(line2);
    }

    code = __xzd_json_get_number((const char *)resp.body, resp.body_length, "code");
    if ((int)code != 0) {
        PR_WARN("xzd net: server code != 0: %d", (int)code);
        snprintf(line2, sizeof(line2), "UPLOAD: CODE %d", (int)code);
        xzd_app_report(line2);
        result = XZD_NET_ERR_SERVER;
        goto __exit;
    }

    if (!__xzd_json_get_string((const char *)resp.body, resp.body_length, "name",
                               out->name, sizeof(out->name)) ||
        out->name[0] == '\0') {
        xzd_app_report("UPLOAD: EMPTY ITEM");
        result = XZD_NET_ERR_LOW_CONF; /* nothing recognised */
        goto __exit;
    }
    __xzd_json_get_string((const char *)resp.body, resp.body_length, "scannedAt",
                          out->scanned_at, sizeof(out->scanned_at));
    __xzd_json_get_string((const char *)resp.body, resp.body_length, "expiryDate",
                          out->expiry_date, sizeof(out->expiry_date));
    __xzd_json_get_string((const char *)resp.body, resp.body_length, "suggestedContainer",
                          out->suggested_container, sizeof(out->suggested_container));
    __xzd_json_get_string((const char *)resp.body, resp.body_length, "note",
                          out->note, sizeof(out->note));
    out->days_left = (int)__xzd_json_get_number((const char *)resp.body, resp.body_length, "daysLeft");
    confidence = __xzd_json_get_number((const char *)resp.body, resp.body_length, "confidence");
    out->confidence = (float)confidence;
    out->stock_total = (int)__xzd_json_get_number((const char *)resp.body,
                                                  resp.body_length, "stockTotal");

    PR_NOTICE("xzd net: scan ok '%s' days=%d conf=%.2f container='%s'",
              out->name, out->days_left, out->confidence, out->suggested_container);

    if (out->confidence > 0.0f && out->confidence < XZD_MIN_CONFIDENCE) {
        xzd_app_report("UPLOAD: LOW CONF");
        result = XZD_NET_ERR_LOW_CONF;
        goto __exit;
    }
    xzd_app_report("UPLOAD: OK");
    result = XZD_NET_OK;

__exit:
    if (resp.buffer) {
        http_client_free(&resp);
    }
    if (body) {
        tal_psram_free(body);
    }
    if (NULL != sg_http_mutex) {
        tal_mutex_unlock(sg_http_mutex);
    }
    return result;
}

/***********************************************************
******************* last-result persistence ****************
***********************************************************/
void xzd_net_last_result_save(const XZD_SCAN_RESULT_T *r)
{
    char buf[192];
    int n;

    if (NULL == r) {
        return;
    }
    n = snprintf(buf, sizeof(buf), "%s|%s|%s|%d",
                 r->name, r->expiry_date, r->suggested_container, r->days_left);
    if (n > 0) {
        tal_kv_set(XZD_KV_LAST_KEY, (const uint8_t *)buf, (size_t)n);
    }
}

bool xzd_net_last_result_load(XZD_SCAN_RESULT_T *out)
{
    uint8_t *v = NULL;
    size_t len = 0;
    char buf[192];
    char *p1, *p2, *p3;

    if (NULL == out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (tal_kv_get(XZD_KV_LAST_KEY, &v, &len) != 0 || NULL == v || len == 0) {
        return false;
    }
    if (len >= 191) {
        len = 191;
    }
    memcpy(buf, v, len);
    buf[len] = '\0';
    tal_kv_free(v);

    p1 = strchr(buf, '|');
    if (p1) {
        *p1 = '\0';
        snprintf(out->name, sizeof(out->name), "%s", buf);
        p2 = strchr(p1 + 1, '|');
        if (p2) {
            *p2 = '\0';
            snprintf(out->expiry_date, sizeof(out->expiry_date), "%s", p1 + 1);
            p3 = strchr(p2 + 1, '|');
            if (p3) {
                *p3 = '\0';
                snprintf(out->suggested_container, sizeof(out->suggested_container), "%s", p2 + 1);
                out->days_left = atoi(p3 + 1);
            }
        }
    }
    return (out->name[0] != '\0');
}

/***********************************************************
********** device heartbeat + state sync (backend) *********
***********************************************************/
static bool __xzd_http_json(const char *path, const char *method,
                            const char *body, uint32_t body_len,
                            uint32_t timeout_ms, http_client_response_t *resp)
{
    http_client_status_t status;
    http_client_header_t headers[] = {
        {.key = "Content-Type", .value = "application/json"},
        {.key = "User-Agent", .value = "tuyaopen-xzd-t5e1"},
    };
    http_client_request_t req;
    bool ok = false;

    if (NULL == resp) {
        return false;
    }
    if (NULL != sg_http_mutex) {
        tal_mutex_lock(sg_http_mutex);
    }
    memset(&req, 0, sizeof(req));
    req.host          = XZD_SERVER_HOST;
    req.port          = XZD_SERVER_PORT;
    req.path          = (char *)path;
    req.tls_no_verify = true;
    req.method        = (char *)method;
    req.headers       = headers;
    req.headers_count = (uint8_t)(sizeof(headers) / sizeof(headers[0]));
    req.body          = (uint8_t *)body;
    req.body_length   = body_len;
    req.timeout_ms    = timeout_ms;

    status = http_client_request(&req, resp);
    if (status != HTTP_CLIENT_SUCCESS) {
        PR_WARN("xzd net: %s %s failed: %d", method, path, status);
        goto __exit;
    }
    if (resp->status_code != 200 || resp->body == NULL || resp->body_length == 0) {
        PR_WARN("xzd net: %s %s HTTP %u", method, path, resp->status_code);
        goto __exit;
    }
    ok = true;

__exit:
    if (NULL != sg_http_mutex) {
        tal_mutex_unlock(sg_http_mutex);
    }
    return ok;
}

bool xzd_net_heartbeat(const char *event, const char *msg, int *stock_total_out)
{
    http_client_response_t resp = {0};
    char body[256];
    int n;
    double code;
    int total = -1;
    bool ok = false;

    (void)msg;
    if (stock_total_out != NULL) {
        *stock_total_out = -1;
    }
    if (!xzd_net_is_online()) {
        return false;
    }
    if (event != NULL && event[0] != '\0') {
        n = snprintf(body, sizeof(body),
                     "{\"deviceId\":\"%s\",\"name\":\"%s\",\"firmwareVersion\":\"1.0.0\",\"event\":\"%s\"}",
                     XZD_DEVICE_ID, XZD_DEVICE_ID, event);
    } else {
        n = snprintf(body, sizeof(body),
                     "{\"deviceId\":\"%s\",\"name\":\"%s\",\"firmwareVersion\":\"1.0.0\"}",
                     XZD_DEVICE_ID, XZD_DEVICE_ID);
    }
    if (n <= 0 || n >= (int)sizeof(body)) {
        return false;
    }
    if (!__xzd_http_json("/api/device/heartbeat", "POST", body, (uint32_t)n,
                         8000, &resp)) {
        return false;
    }
    code = __xzd_json_get_number((const char *)resp.body, resp.body_length, "code");
    if ((int)code == 0) {
        total = (int)__xzd_json_get_number((const char *)resp.body,
                                           resp.body_length, "stockTotal");
        {
            /* Sync the device clock from the server's lastSeenAt so the
             * standby header shows the real local time. */
            const char *lst = __xzd_json_find_key((const char *)resp.body,
                                                  resp.body_length, "lastSeenAt");
            if (lst != NULL && *lst == '"') {
                char iso[40];
                size_t n = 0;
                const char *q = lst + 1;
                const char *end = (const char *)resp.body + resp.body_length;
                while (q < end && *q != '"' && n + 1 < sizeof(iso)) {
                    iso[n++] = *q++;
                }
                iso[n] = '\0';
                {
                    TIME_T ep = __xzd_parse_iso_epoch(iso);
                    if (ep > 1600000000) {
                        tal_time_set_posix(ep, 0);
                    }
                }
            }
        }
        ok = true;
    }
    if (resp.buffer) {
        http_client_free(&resp);
    }
    if (stock_total_out != NULL) {
        *stock_total_out = total;
    }
    return ok;
}

static int __xzd_json_array_item_count(const char *body, size_t len, const char *key)
{
    const char *v = __xzd_json_find_key(body, len, key);
    const char *end = body + len;
    int depth = 0;
    int count = 0;

    if (v == NULL || *v != '[') {
        return 0;
    }
    v++;
    while (v < end) {
        if (*v == '{') {
            if (depth == 0) {
                count++;
            }
            depth++;
        } else if (*v == '}') {
            if (depth > 0) {
                depth--;
            }
        } else if (*v == ']' && depth == 0) {
            break;
        }
        v++;
    }
    return count;
}

static void __xzd_json_parse_items(const char *body, size_t len, const char *key,
                                   char names[][XZD_SCAN_NAME_LEN], int *days,
                                   int max_items, int *count_out)
{
    const char *v = __xzd_json_find_key(body, len, key);
    const char *end = body + len;
    int count = 0;

    if (v == NULL || *v != '[') {
        *count_out = 0;
        return;
    }
    v++;
    while (v < end && count < max_items) {
        const char *obj_end;
        int depth = 0;
        char nm[XZD_SCAN_NAME_LEN] = "";
        int dl = 0;

        v = __xzd_json_skip_ws(v, end);
        if (v >= end || *v == ']') {
            break;
        }
        if (*v != '{') {
            v++;
            continue;
        }
        obj_end = v;
        while (obj_end < end) {
            if (*obj_end == '{') {
                depth++;
            } else if (*obj_end == '}') {
                depth--;
                if (depth == 0) {
                    obj_end++;
                    break;
                }
            }
            obj_end++;
        }
        __xzd_json_get_string(v, (size_t)(obj_end - v), "name", nm, sizeof(nm));
        dl = (int)__xzd_json_get_number(v, (size_t)(obj_end - v), "daysLeft");
        if (nm[0]) {
            snprintf(names[count], XZD_SCAN_NAME_LEN, "%s", nm);
            days[count] = dl;
            count++;
        }
        v = obj_end;
    }
    *count_out = count;
}

bool xzd_net_fetch_device_state(XZD_DEVICE_STATE_T *out)
{
    http_client_response_t resp = {0};
    char path[96];
    double code;
    bool ok = false;

    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->stock_total = -1;
    if (!xzd_net_is_online()) {
        return false;
    }
    snprintf(path, sizeof(path), "/api/device/state?deviceId=%s", XZD_DEVICE_ID);
    if (!__xzd_http_json(path, "GET", NULL, 0, 8000, &resp)) {
        return false;
    }
    code = __xzd_json_get_number((const char *)resp.body, resp.body_length, "code");
    if ((int)code != 0) {
        goto __done;
    }
    out->stock_total = (int)__xzd_json_get_number((const char *)resp.body,
                                                  resp.body_length,
                                                  "stockTotal");
    out->expired_count = __xzd_json_array_item_count((const char *)resp.body,
                                                     resp.body_length,
                                                     "expired");
    __xzd_json_parse_items((const char *)resp.body, resp.body_length, "soon",
                           (char (*)[XZD_SCAN_NAME_LEN])out->soon_name,
                           out->soon_days, 2, &out->soon_count);
    {
        const char *lr = __xzd_json_find_key((const char *)resp.body,
                                             resp.body_length, "lastResult");
        if (lr != NULL && *lr == '{') {
            const char *obj_end = lr;
            int depth = 0;

            while (obj_end < (const char *)resp.body + resp.body_length) {
                if (*obj_end == '{') {
                    depth++;
                } else if (*obj_end == '}') {
                    depth--;
                    if (depth == 0) {
                        obj_end++;
                        break;
                    }
                }
                obj_end++;
            }
            __xzd_json_get_string(lr, (size_t)(obj_end - lr), "name",
                                  out->last_name, sizeof(out->last_name));
            out->last_days = (int)__xzd_json_get_number(lr, (size_t)(obj_end - lr),
                                                        "daysLeft");
            __xzd_json_get_string(lr, (size_t)(obj_end - lr), "container",
                                  out->last_container,
                                  sizeof(out->last_container));
        }
    }
    ok = true;
__done:
    if (resp.buffer) {
        http_client_free(&resp);
    }
    return ok;
}

/* Walk the full /api/items array: total count + soon/expired stats (keeps the
 * first two soon items), mirroring what the web pages show. */
static void __xzd_json_scan_items(const char *body, size_t len,
                                  XZD_DEVICE_STATE_T *out)
{
    const char *v = __xzd_json_find_key(body, len, "items");
    const char *end = body + len;
    int total = 0;
    int soon = 0;
    int expired = 0;

    if (v == NULL || *v != '[') {
        return;
    }
    v++;
    while (v < end) {
        const char *obj_end;
        int depth = 0;
        char nm[XZD_SCAN_NAME_LEN] = "";
        int dl = 0;

        v = __xzd_json_skip_ws(v, end);
        if (v >= end || *v == ']') {
            break;
        }
        if (*v != '{') {
            v++;
            continue;
        }
        obj_end = v;
        while (obj_end < end) {
            if (*obj_end == '{') {
                depth++;
            } else if (*obj_end == '}') {
                depth--;
                if (depth == 0) {
                    obj_end++;
                    break;
                }
            }
            obj_end++;
        }
        total++;
        __xzd_json_get_string(v, (size_t)(obj_end - v), "name", nm, sizeof(nm));
        dl = (int)__xzd_json_get_number(v, (size_t)(obj_end - v), "daysLeft");
        if (dl < 0) {
            expired++;
        } else if (dl <= 3) {
            if (soon < 2 && nm[0]) {
                snprintf(out->soon_name[soon], XZD_SCAN_NAME_LEN, "%s", nm);
                out->soon_days[soon] = dl;
            }
            soon++;
        }
        v = obj_end;
    }
    out->stock_total = total;
    out->soon_count = soon;
    out->expired_count = expired;
}

bool xzd_net_fetch_items(XZD_DEVICE_STATE_T *out)
{
    http_client_response_t resp = {0};
    double code;
    bool ok = false;

    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->stock_total = -1;
    if (!xzd_net_is_online()) {
        return false;
    }
    if (!__xzd_http_json("/api/items", "GET", NULL, 0, 8000, &resp)) {
        return false;
    }
    code = __xzd_json_get_number((const char *)resp.body, resp.body_length, "code");
    if ((int)code != 0) {
        goto __done;
    }
    __xzd_json_scan_items((const char *)resp.body, resp.body_length, out);
    ok = true;
__done:
    if (resp.buffer) {
        http_client_free(&resp);
    }
    return ok;
}

void xzd_net_devstate_save(const XZD_DEVICE_STATE_T *s)
{
    char buf[256];
    int n;

    if (s == NULL) {
        return;
    }
    n = snprintf(buf, sizeof(buf), "%d|%d|%d|%s|%d|%s|%d|%s|%d|%s",
                 s->stock_total, s->soon_count, s->expired_count,
                 s->soon_name[0], s->soon_days[0],
                 s->soon_name[1], s->soon_days[1],
                 s->last_name, s->last_days, s->last_container);
    if (n > 0) {
        tal_kv_set(XZD_KV_STATE_KEY, (const uint8_t *)buf, (size_t)n);
    }
}

bool xzd_net_devstate_load(XZD_DEVICE_STATE_T *out)
{
    uint8_t *v = NULL;
    size_t len = 0;
    char buf[256];
    char *f[10];
    int i;

    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (tal_kv_get(XZD_KV_STATE_KEY, &v, &len) != 0 || NULL == v || len == 0) {
        return false;
    }
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, v, len);
    buf[len] = '\0';
    tal_kv_free(v);

    f[0] = buf;
    for (i = 1; i < 10; i++) {
        char *p = strchr(f[i - 1], '|');
        if (p == NULL) {
            return false;
        }
        *p = '\0';
        f[i] = p + 1;
    }
    out->stock_total = atoi(f[0]);
    out->soon_count  = atoi(f[1]);
    out->expired_count = atoi(f[2]);
    snprintf(out->soon_name[0], sizeof(out->soon_name[0]), "%s", f[3]);
    out->soon_days[0] = atoi(f[4]);
    snprintf(out->soon_name[1], sizeof(out->soon_name[1]), "%s", f[5]);
    out->soon_days[1] = atoi(f[6]);
    snprintf(out->last_name, sizeof(out->last_name), "%s", f[7]);
    out->last_days = atoi(f[8]);
    snprintf(out->last_container, sizeof(out->last_container), "%s", f[9]);
    return true;
}
