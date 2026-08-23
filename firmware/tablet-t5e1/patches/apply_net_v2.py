#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""net 层 v2：汇总页拉取 pending + 确认（幂等锚点插入）。"""
import sys

ROOT = "/Users/lwl/TuyaOpenIDE/projects/copy/source/embedded"
HDR = ROOT + "/include/app_xzd_net.h"
SRC = ROOT + "/src/app_xzd_net.c"

# ---------- net.h ----------
with open(HDR, "r", encoding="utf-8") as fh:
    h = fh.read()

h_anchor = "bool xzd_net_fetch_latest_scan(XZD_SCAN_RESULT_T *out);"
h_add = """bool xzd_net_fetch_latest_scan(XZD_SCAN_RESULT_T *out);

/** 汇总批次摘要（GET /api/summary/pending 解析结果） */
typedef struct {
    char batch_id[40];
    int  in_count;
    int  out_count;
    int  mid_out_count;
    int  unknown_count;
    int  has_pending;   /* 1 = 有待确认批次 */
} XZD_SUMMARY_T;

/**
 * @brief v1: GET /api/summary/pending?deviceId=...（平板 Token 鉴权）。
 * @return true 表示 HTTP 成功且解析完成；是否有批次看 out->has_pending。
 */
bool xzd_net_fetch_pending_summary(XZD_SUMMARY_T *out);

/**
 * @brief v1: POST /api/summary/confirm {"batchId":"..."}（平板 Token 鉴权）。
 * @return true on HTTP 200 + code==0。
 */
bool xzd_net_confirm_summary(const char *batch_id);"""

if "xzd_net_fetch_pending_summary" in h:
    print("SKIP: net.h exists")
elif h_anchor in h:
    h = h.replace(h_anchor, h_add, 1)
    with open(HDR, "w", encoding="utf-8") as fh:
        fh.write(h)
    print("OK: net.h updated")
else:
    print("FAIL: net.h anchor", file=sys.stderr)
    sys.exit(2)

# ---------- net.c ----------
with open(SRC, "r", encoding="utf-8") as fh:
    s = fh.read()

tail_marker = "    ok = (out->name[0] != '\\0');\n__done:\n    if (resp.buffer) {\n        http_client_free(&resp);\n    }\n    return ok;\n}"
if "xzd_net_fetch_pending_summary" in s:
    print("SKIP: net.c exists")
    sys.exit(0)
if tail_marker not in s:
    print("FAIL: net.c tail anchor", file=sys.stderr)
    sys.exit(2)

s_add = tail_marker + """

bool xzd_net_fetch_pending_summary(XZD_SUMMARY_T *out)
{
    http_client_response_t resp = {0};
    char path[96];
    double code;
    bool ok = false;
    const char *data = NULL;
    const char *batch = NULL;
    const char *end = NULL;

    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->has_pending = 0;
    if (!xzd_net_is_online()) {
        return false;
    }
    snprintf(path, sizeof(path), "/api/summary/pending?deviceId=%s", XZD_DEVICE_ID);
    if (!__xzd_http_json(path, "GET", NULL, 0, 8000, &resp)) {
        return false;
    }
    code = __xzd_json_get_number((const char *)resp.body, resp.body_length, "code");
    if ((int)code != 0) {
        goto __done;
    }
    data = __xzd_json_find_key((const char *)resp.body, resp.body_length, "data");
    if (data == NULL || *data == 'n') {   /* "data":null = 无待确认批次 */
        goto __done;
    }
    batch = __xzd_json_find_key(data, (size_t)((const char *)resp.body + resp.body_length - data),
                                "batch");
    if (batch == NULL || *batch != '{') {
        goto __done;
    }
    end = batch;
    {
        int depth = 0;
        while (end < (const char *)resp.body + resp.body_length) {
            if (*end == '{') {
                depth++;
            } else if (*end == '}') {
                depth--;
                if (depth == 0) {
                    end++;
                    break;
                }
            }
            end++;
        }
    }
    __xzd_json_get_string(batch, (size_t)(end - batch), "id",
                          out->batch_id, sizeof(out->batch_id));
    out->in_count = (int)__xzd_json_get_number(batch, (size_t)(end - batch), "inCount");
    out->out_count = (int)__xzd_json_get_number(batch, (size_t)(end - batch), "outCount");
    out->mid_out_count = (int)__xzd_json_get_number(batch, (size_t)(end - batch), "midOutCount");
    out->unknown_count = (int)__xzd_json_get_number(batch, (size_t)(end - batch), "unknownCount");
    out->has_pending = (out->batch_id[0] != '\\0') ? 1 : 0;
    ok = true;
__done:
    if (resp.buffer) {
        http_client_free(&resp);
    }
    return ok;
}

bool xzd_net_confirm_summary(const char *batch_id)
{
    http_client_response_t resp = {0};
    char body[160];
    int n;
    double code;
    bool ok = false;

    if (batch_id == NULL || batch_id[0] == '\\0') {
        return false;
    }
    if (!xzd_net_is_online()) {
        return false;
    }
    n = snprintf(body, sizeof(body), "{\\"batchId\\":\\"%s\\"}", batch_id);
    if (n <= 0 || n >= (int)sizeof(body)) {
        return false;
    }
    if (!__xzd_http_json("/api/summary/confirm", "POST", body, (uint32_t)n,
                         8000, &resp)) {
        return false;
    }
    code = __xzd_json_get_number((const char *)resp.body, resp.body_length, "code");
    ok = ((int)code == 0);
    if (resp.buffer) {
        http_client_free(&resp);
    }
    return ok;
}"""

s = s.replace(tail_marker, s_add, 1)
with open(SRC, "w", encoding="utf-8") as fh:
    fh.write(s)
print("OK: net.c updated")
print("DONE")
