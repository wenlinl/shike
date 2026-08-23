#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Apply v1 net-layer additions to the TuyaOpenIDE copy project (anchor-based,
idempotent). Run from the repo root with escalated permissions."""
import os
import sys

ROOT = "/Users/lwl/TuyaOpenIDE/projects/copy/source/embedded"
HDR = os.path.join(ROOT, "include", "app_xzd_net.h")
SRC = os.path.join(ROOT, "src", "app_xzd_net.c")

HDR_ANCHOR = "bool xzd_net_devstate_load(XZD_DEVICE_STATE_T *out);\n\n#ifdef __cplusplus"
HDR_ADD = """bool xzd_net_devstate_load(XZD_DEVICE_STATE_T *out);

/**
 * @brief v1: POST /api/device/task (平板选择动作后下发内模组，经云端中转).
 * @param action "in" or "out".
 * @return true on HTTP 200 + code==0.
 */
bool xzd_net_post_task(const char *action);

/**
 * @brief v1: GET /api/scan/latest?deviceId=... (内模组最近一次扫码事件).
 *        契约草案：{"code":0,"data":{...,"action":"in"|"out"}}；无新事件时
 *        name 为空 / 接口返回空 data。后端契约就绪后按实际字段微调。
 * @return true 且 out->name[0]!='\\0' 表示有可展示事件。
 */
bool xzd_net_fetch_latest_scan(XZD_SCAN_RESULT_T *out);

#ifdef __cplusplus"""

SRC_ANCHOR = "    snprintf(out->last_container, sizeof(out->last_container), \"%s\", f[9]);\n    return true;\n}"
SRC_ADD = """    snprintf(out->last_container, sizeof(out->last_container), "%s", f[9]);
    return true;
}

/* ============================ v1 additions ============================ */

bool xzd_net_post_task(const char *action)
{
    http_client_response_t resp = {0};
    char body[128];
    int n;
    double code;
    bool ok = false;

    if (action == NULL || (strcmp(action, "in") != 0 && strcmp(action, "out") != 0)) {
        return false;
    }
    if (!xzd_net_is_online()) {
        return false;
    }
    n = snprintf(body, sizeof(body), "{\\"deviceId\\":\\"%s\\",\\"action\\":\\"%s\\"}",
                 XZD_DEVICE_ID, action);
    if (n <= 0 || n >= (int)sizeof(body)) {
        return false;
    }
    if (!__xzd_http_json("/api/device/task", "POST", body, (uint32_t)n,
                         8000, &resp)) {
        return false;
    }
    code = __xzd_json_get_number((const char *)resp.body, resp.body_length, "code");
    ok = ((int)code == 0);
    if (resp.buffer) {
        http_client_free(&resp);
    }
    return ok;
}

bool xzd_net_fetch_latest_scan(XZD_SCAN_RESULT_T *out)
{
    http_client_response_t resp = {0};
    char path[96];
    double code;
    bool ok = false;
    const char *name_key = NULL;

    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (!xzd_net_is_online()) {
        return false;
    }
    snprintf(path, sizeof(path), "/api/scan/latest?deviceId=%s", XZD_DEVICE_ID);
    if (!__xzd_http_json(path, "GET", NULL, 0, 8000, &resp)) {
        return false;
    }
    code = __xzd_json_get_number((const char *)resp.body, resp.body_length, "code");
    if ((int)code != 0) {
        goto __done;
    }
    name_key = __xzd_json_find_key((const char *)resp.body, resp.body_length, "name");
    if (name_key == NULL) {
        goto __done;   /* 无新事件：接口不返回 name 或返回空 data */
    }
    __xzd_json_get_string((const char *)resp.body, resp.body_length, "name",
                          out->name, sizeof(out->name));
    __xzd_json_get_string((const char *)resp.body, resp.body_length, "scannedAt",
                          out->scanned_at, sizeof(out->scanned_at));
    __xzd_json_get_string((const char *)resp.body, resp.body_length, "expiryDate",
                          out->expiry_date, sizeof(out->expiry_date));
    out->days_left = (int)__xzd_json_get_number((const char *)resp.body,
                                                resp.body_length, "daysLeft");
    __xzd_json_get_string((const char *)resp.body, resp.body_length,
                          "suggestedContainer", out->suggested_container,
                          sizeof(out->suggested_container));
    out->stock_total = (int)__xzd_json_get_number((const char *)resp.body,
                                                  resp.body_length, "stockTotal");
    ok = (out->name[0] != '\\0');
__done:
    if (resp.buffer) {
        http_client_free(&resp);
    }
    return ok;
}"""


def apply(path, anchor, addition, label):
    with open(path, "r", encoding="utf-8") as fh:
        data = fh.read()
    if addition in data:
        print("%s: already applied, skip" % label)
        return
    if anchor not in data:
        print("%s: ANCHOR NOT FOUND" % label, file=sys.stderr)
        sys.exit(2)
    data = data.replace(anchor, addition, 1)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(data)
    print("%s: applied" % label)


apply(HDR, HDR_ANCHOR, HDR_ADD, "app_xzd_net.h")
apply(SRC, SRC_ANCHOR, SRC_ADD, "app_xzd_net.c")
