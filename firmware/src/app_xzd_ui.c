/**
 * @file app_xzd_ui.c
 * @brief Framebuffer UI drawing (RGB565 + alpha) for XianZhidao.
 */
#include "tuya_cloud_types.h"
#include "tal_api.h"

#include "app_xzd_ui.h"
#include "app_xzd_font.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define XZD_UI_CJK_W         XZD_FONT_CJK_W
#define XZD_UI_CJK_H         XZD_FONT_CJK_H
#define XZD_UI_ASCII_H       XZD_FONT_ASCII_H

/***********************************************************
***********************variable define**********************
***********************************************************/
static bool sg_is_swap = false;
static bool sg_flip_x = false;
static bool sg_flip_y = false;
static uint8_t sg_flip_row[512];

/***********************************************************
***********************function define**********************
***********************************************************/
static uint16_t __xzd_swap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

void xzd_ui_set_swap(bool is_swap)
{
    sg_is_swap = is_swap;
}

void xzd_ui_set_flip(bool flip_x, bool flip_y)
{
    sg_flip_x = flip_x;
    sg_flip_y = flip_y;
}

void xzd_ui_logical_to_fb(TDL_DISP_FRAME_BUFF_T *fb, uint16_t lx, uint16_t ly,
                          uint16_t *fx, uint16_t *fy)
{
    uint16_t x = lx;
    uint16_t y = ly;

    if (NULL == fb || NULL == fx || NULL == fy) {
        return;
    }
    /* Native portrait mapping (240x320) with optional axis mirrors for a
     * panel that is physically mounted upside-down. */
    if (sg_flip_x) {
        x = (uint16_t)((fb->width - 1) - x);
    }
    if (sg_flip_y) {
        y = (uint16_t)((fb->height - 1) - y);
    }
    *fx = x;
    *fy = y;
}

void xzd_ui_fb_to_logical(TDL_DISP_FRAME_BUFF_T *fb, uint16_t fx, uint16_t fy,
                          uint16_t *lx, uint16_t *ly)
{
    uint16_t x = fx;
    uint16_t y = fy;

    if (NULL == fb || NULL == lx || NULL == ly) {
        return;
    }
    /* Axis mirrors are involutive, so this is also the inverse map. */
    if (sg_flip_x) {
        x = (uint16_t)((fb->width - 1) - x);
    }
    if (sg_flip_y) {
        y = (uint16_t)((fb->height - 1) - y);
    }
    *lx = x;
    *ly = y;
}

void xzd_ui_flip_vertical(TDL_DISP_FRAME_BUFF_T *fb)
{
    uint32_t row_bytes;
    uint16_t y;

    if (NULL == fb || NULL == fb->frame || fb->height == 0) {
        return;
    }
    row_bytes = fb->len / fb->height;
    if (row_bytes == 0 || row_bytes > sizeof(sg_flip_row)) {
        return;
    }
    for (y = 0; y < fb->height / 2; y++) {
        uint8_t *a = fb->frame + (uint32_t)y * row_bytes;
        uint8_t *b = fb->frame + (uint32_t)(fb->height - 1 - y) * row_bytes;
        memcpy(sg_flip_row, a, row_bytes);
        memcpy(a, b, row_bytes);
        memcpy(b, sg_flip_row, row_bytes);
    }
}

void xzd_ui_flip_horizontal(TDL_DISP_FRAME_BUFF_T *fb)
{
    uint32_t row_bytes;
    uint16_t y;
    uint16_t x;

    if (NULL == fb || NULL == fb->frame || fb->height == 0 || fb->width == 0) {
        return;
    }
    row_bytes = fb->len / fb->height;
    for (y = 0; y < fb->height; y++) {
        uint16_t *row = (uint16_t *)(fb->frame + (uint32_t)y * row_bytes);
        for (x = 0; x < fb->width / 2; x++) {
            uint16_t tmp = row[x];
            row[x] = row[fb->width - 1 - x];
            row[fb->width - 1 - x] = tmp;
        }
    }
}

static uint16_t __xzd_blend565(uint16_t bg, uint16_t fg, uint8_t alpha)
{
    uint32_t a;
    uint32_t r, g, b;

    if (alpha >= 255) {
        return fg;
    }
    if (alpha == 0) {
        return bg;
    }
    a = alpha;
    r = (((uint32_t)(fg >> 11) & 0x1F) * a + ((uint32_t)(bg >> 11) & 0x1F) * (255 - a)) / 255;
    g = (((uint32_t)(fg >> 5) & 0x3F) * a + ((uint32_t)(bg >> 5) & 0x3F) * (255 - a)) / 255;
    b = (((uint32_t)fg & 0x1F) * a + ((uint32_t)bg & 0x1F) * (255 - a)) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static uint16_t __xzd_fb_get(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x, uint16_t y)
{
    uint16_t v;
    uint16_t fx = x;
    uint16_t fy = y;

    if (NULL == fb || NULL == fb->frame || x >= XZD_UI_W || y >= XZD_UI_H) {
        return 0;
    }
    xzd_ui_logical_to_fb(fb, x, y, &fx, &fy);
    v = ((uint16_t *)fb->frame)[(uint32_t)fy * fb->width + fx];
    return sg_is_swap ? __xzd_swap16(v) : v;
}

static void __xzd_fb_put(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x, uint16_t y, uint16_t color)
{
    uint16_t fx = x;
    uint16_t fy = y;

    if (NULL == fb || NULL == fb->frame || x >= XZD_UI_W || y >= XZD_UI_H) {
        return;
    }
    xzd_ui_logical_to_fb(fb, x, y, &fx, &fy);
    ((uint16_t *)fb->frame)[(uint32_t)fy * fb->width + fx] = sg_is_swap ? __xzd_swap16(color) : color;
}

void xzd_ui_fill(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                 uint16_t x1, uint16_t y1, uint16_t color, uint8_t alpha)
{
    uint16_t x, y;

    if (NULL == fb || NULL == fb->frame) {
        return;
    }
    if (x1 >= XZD_UI_W) {
        x1 = (uint16_t)(XZD_UI_W - 1);
    }
    if (y1 >= XZD_UI_H) {
        y1 = (uint16_t)(XZD_UI_H - 1);
    }
    if (alpha >= 255) {
        for (y = y0; y <= y1; y++) {
            for (x = x0; x <= x1; x++) {
                __xzd_fb_put(fb, x, y, color);
            }
        }
        return;
    }
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            __xzd_fb_put(fb, x, y, __xzd_blend565(__xzd_fb_get(fb, x, y), color, alpha));
        }
    }
}

void xzd_ui_fill_full(TDL_DISP_FRAME_BUFF_T *fb, uint16_t color)
{
    uint32_t i;
    uint32_t n;

    if (NULL == fb || NULL == fb->frame) {
        return;
    }
    n = (uint32_t)fb->width * fb->height;
    for (i = 0; i < n; i++) {
        ((uint16_t *)fb->frame)[i] = sg_is_swap ? __xzd_swap16(color) : color;
    }
}

static uint32_t __xzd_utf8_decode(const char **pp)
{
    const uint8_t *p = (const uint8_t *)*pp;
    uint8_t c = p[0];

    if (c < 0x80) {
        *pp += 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0) {
        *pp += 2;
        return (uint32_t)(((c & 0x1F) << 6) | (p[1] & 0x3F));
    }
    if ((c & 0xF0) == 0xE0) {
        *pp += 3;
        return (uint32_t)(((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
    }
    *pp += 1;
    return (uint32_t)'?';
}

uint16_t xzd_ui_text_width(const char *s)
{
    uint16_t w = 0;
    const char *p = s;

    if (NULL == s) {
        return 0;
    }
    while (*p) {
        uint32_t cp = __xzd_utf8_decode(&p);
        if (cp < 0x80) {
            XZD_GLYPH_T g;
            w = (uint16_t)(w + (xzd_font_get_ascii((char)cp, &g) ? g.w : 8));
        } else {
            w = (uint16_t)(w + XZD_UI_CJK_W);
        }
    }
    return w;
}

static void __xzd_draw_glyph(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x, uint16_t y,
                             const XZD_GLYPH_T *g, uint16_t color, uint8_t alpha)
{
    uint16_t gx, gy;
    uint8_t row_bytes;
    const uint8_t *row;

    if (NULL == g || NULL == g->bits || g->w == 0 || g->h == 0) {
        return;
    }
    row_bytes = (uint8_t)((g->w + 7) / 8);
    for (gy = 0; gy < g->h; gy++) {
        row = g->bits + (uint32_t)gy * row_bytes;
        for (gx = 0; gx < g->w; gx++) {
            if (row[gx >> 3] & (0x80 >> (gx & 7))) {
                uint16_t px = x + gx;
                uint16_t py = y + gy;
                if (px < XZD_UI_W && py < XZD_UI_H) {
                    if (alpha >= 255) {
                        __xzd_fb_put(fb, px, py, color);
                    } else {
                        __xzd_fb_put(fb, px, py, __xzd_blend565(__xzd_fb_get(fb, px, py), color, alpha));
                    }
                }
            }
        }
    }
}

static void __xzd_draw_missing(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x, uint16_t y,
                               uint16_t w, uint16_t color, uint8_t alpha)
{
    uint16_t i;

    for (i = 0; i < w; i++) {
        __xzd_fb_put(fb, (uint16_t)(x + i), y, __xzd_blend565(__xzd_fb_get(fb, (uint16_t)(x + i), y), color, alpha));
        __xzd_fb_put(fb, (uint16_t)(x + i), (uint16_t)(y + 15),
                     __xzd_blend565(__xzd_fb_get(fb, (uint16_t)(x + i), (uint16_t)(y + 15)), color, alpha));
    }
    for (i = 1; i < 15; i++) {
        __xzd_fb_put(fb, x, (uint16_t)(y + i), __xzd_blend565(__xzd_fb_get(fb, x, (uint16_t)(y + i)), color, alpha));
        __xzd_fb_put(fb, (uint16_t)(x + w - 1), (uint16_t)(y + i),
                     __xzd_blend565(__xzd_fb_get(fb, (uint16_t)(x + w - 1), (uint16_t)(y + i)), color, alpha));
    }
}

void xzd_ui_draw_text(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x, uint16_t y,
                      const char *s, uint16_t color, uint8_t alpha)
{
    const char *p = s;
    uint16_t cx = x;

    if (NULL == fb || NULL == s) {
        return;
    }
    while (*p) {
        uint32_t cp = __xzd_utf8_decode(&p);
        if (cp < 0x80) {
            XZD_GLYPH_T g;
            if (xzd_font_get_ascii((char)cp, &g)) {
                __xzd_draw_glyph(fb, cx, y, &g, color, alpha);
                cx = (uint16_t)(cx + g.w);
            } else {
                cx = (uint16_t)(cx + 8);
            }
        } else {
            XZD_GLYPH_T g;
            if (xzd_font_get_cjk((uint16_t)cp, &g)) {
                __xzd_draw_glyph(fb, cx, y, &g, color, alpha);
            } else {
                __xzd_draw_missing(fb, cx, y, XZD_UI_CJK_W, color, alpha);
            }
            cx = (uint16_t)(cx + XZD_UI_CJK_W);
        }
    }
}

void xzd_ui_draw_text_center(TDL_DISP_FRAME_BUFF_T *fb, uint16_t cx, uint16_t y,
                             const char *s, uint16_t color, uint8_t alpha)
{
    uint16_t w = xzd_ui_text_width(s);
    uint16_t x;

    if (w >= cx) {
        x = 0;
    } else {
        x = (uint16_t)(cx - w / 2);
    }
    xzd_ui_draw_text(fb, x, y, s, color, alpha);
}

void xzd_ui_draw_big_digit(TDL_DISP_FRAME_BUFF_T *fb, char digit,
                           uint16_t cx, uint16_t cy, uint16_t color, uint8_t alpha)
{
    XZD_GLYPH_T g;
    uint16_t scale;
    uint16_t dw, dh;
    uint16_t x0, y0;
    uint16_t gx, gy;

    if (!xzd_font_get_big_digit(digit, &g)) {
        return;
    }
    if (g.w == 0 || g.h == 0) {
        return;
    }
    /* Integer nearest-neighbour scale: the largest factor that fits the
     * ~120x150 countdown box (24x24 base -> x5 = 120x120). */
    scale = (uint16_t)(120U / g.w);
    if (((uint16_t)(150U / g.h)) < scale) {
        scale = (uint16_t)(150U / g.h);
    }
    if (scale < 1) {
        scale = 1;
    }
    dw = (uint16_t)(g.w * scale);
    dh = (uint16_t)(g.h * scale);
    x0 = (cx >= dw / 2) ? (uint16_t)(cx - dw / 2) : 0;
    y0 = (cy >= dh / 2) ? (uint16_t)(cy - dh / 2) : 0;
    for (gy = 0; gy < g.h; gy++) {
        uint8_t row_bytes = (uint8_t)((g.w + 7) / 8);
        const uint8_t *row = g.bits + (uint32_t)gy * row_bytes;

        for (gx = 0; gx < g.w; gx++) {
            uint16_t px0, py0, px1, py1;

            if (!(row[gx >> 3] & (0x80 >> (gx & 7)))) {
                continue;
            }
            px0 = (uint16_t)(x0 + gx * scale);
            py0 = (uint16_t)(y0 + gy * scale);
            px1 = (uint16_t)(px0 + scale - 1);
            py1 = (uint16_t)(py0 + scale - 1);
            xzd_ui_fill(fb, px0, py0, px1, py1, color, alpha);
        }
    }
}

void xzd_ui_draw_rect(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                      uint16_t x1, uint16_t y1, uint16_t color, uint8_t alpha)
{
    xzd_ui_fill(fb, x0, y0, x1, y0, color, alpha);
    xzd_ui_fill(fb, x0, y1, x1, y1, color, alpha);
    xzd_ui_fill(fb, x0, y0, x0, y1, color, alpha);
    xzd_ui_fill(fb, x1, y0, x1, y1, color, alpha);
}

void xzd_ui_draw_button(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                        uint16_t x1, uint16_t y1, uint16_t bg, uint16_t fg,
                        const char *label)
{
    uint16_t label_w = xzd_ui_text_width(label);
    uint16_t cx = (uint16_t)((x0 + x1) / 2);
    uint16_t cy = (uint16_t)((y0 + y1) / 2);
    uint16_t lx = (label_w >= cx) ? 0 : (uint16_t)(cx - label_w / 2);
    uint16_t ly = (cy >= 8) ? (uint16_t)(cy - 8) : 0;

    xzd_ui_fill(fb, x0, y0, x1, y1, bg, 255);
    xzd_ui_draw_rect(fb, x0, y0, x1, y1, fg, 200);
    xzd_ui_draw_text(fb, lx, ly, label, fg, 255);
}

bool xzd_ui_hit(uint16_t x, uint16_t y, uint16_t x0, uint16_t y0,
                uint16_t x1, uint16_t y1)
{
    return (x >= x0 && x <= x1 && y >= y0 && y <= y1);
}

/***********************************************************
****************** extended drawing helpers ****************
***********************************************************/
static uint32_t __xzd_isqrt(uint32_t v)
{
    uint32_t x = v;
    uint32_t y = 0;
    uint32_t m = 0x40000000U;

    while (m > x) {
        m >>= 2;
    }
    while (m) {
        if (x >= y + m) {
            x -= y + m;
            y = (y >> 1) + m;
        } else {
            y >>= 1;
        }
        m >>= 2;
    }
    return y;
}

void xzd_ui_fill_round_rect(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                            uint16_t x1, uint16_t y1, uint16_t r,
                            uint16_t color, uint8_t alpha)
{
    uint16_t y;
    uint16_t h;

    if (NULL == fb || NULL == fb->frame) {
        return;
    }
    if (x1 >= XZD_UI_W) {
        x1 = (uint16_t)(XZD_UI_W - 1);
    }
    if (y1 >= XZD_UI_H) {
        y1 = (uint16_t)(XZD_UI_H - 1);
    }
    if (x1 < x0 || y1 < y0) {
        return;
    }
    h = (uint16_t)(y1 - y0 + 1);
    if (r > h / 2) {
        r = (uint16_t)(h / 2);
    }
    for (y = y0; y <= y1; y++) {
        uint16_t d = (uint16_t)(y - y0);
        uint16_t d2 = (uint16_t)(y1 - y);
        uint16_t dd = (d < d2) ? d : d2;
        uint16_t inset = 0;
        uint16_t lx, rx;

        if (dd < r) {
            uint32_t t = (uint32_t)r * r - (uint32_t)(r - dd) * (r - dd);
            inset = (uint16_t)(r - __xzd_isqrt(t));
        }
        lx = (uint16_t)(x0 + inset);
        rx = (uint16_t)(x1 - inset);
        if (rx < lx) {
            continue;
        }
        xzd_ui_fill(fb, lx, y, rx, y, color, alpha);
    }
}

void xzd_ui_fill_round_rect_bordered(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0,
                                     uint16_t y0, uint16_t x1, uint16_t y1,
                                     uint16_t r, uint8_t bw,
                                     uint16_t color, uint16_t fill)
{
    uint16_t b = bw;

    if (NULL == fb || NULL == fb->frame) {
        return;
    }
    if (x1 < x0 || y1 < y0) {
        return;
    }
    xzd_ui_fill_round_rect(fb, x0, y0, x1, y1, r, color, 255);
    if (b * 2 >= (x1 - x0 + 1) || b * 2 >= (y1 - y0 + 1)) {
        return;
    }
    xzd_ui_fill_round_rect(fb, (uint16_t)(x0 + b), (uint16_t)(y0 + b),
                           (uint16_t)(x1 - b), (uint16_t)(y1 - b),
                           r > b ? (uint16_t)(r - b) : 0, fill, 255);
}

void xzd_ui_fill_round_rect_grad(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                                 uint16_t x1, uint16_t y1, uint16_t r,
                                 uint16_t c_top, uint16_t c_bot)
{
    uint16_t y;
    uint16_t h;

    if (NULL == fb || NULL == fb->frame) {
        return;
    }
    if (x1 >= XZD_UI_W) {
        x1 = (uint16_t)(XZD_UI_W - 1);
    }
    if (y1 >= XZD_UI_H) {
        y1 = (uint16_t)(XZD_UI_H - 1);
    }
    if (x1 < x0 || y1 < y0) {
        return;
    }
    h = (uint16_t)(y1 - y0 + 1);
    if (r > h / 2) {
        r = (uint16_t)(h / 2);
    }
    for (y = y0; y <= y1; y++) {
        uint16_t d = (uint16_t)(y - y0);
        uint16_t d2 = (uint16_t)(y1 - y);
        uint16_t dd = (d < d2) ? d : d2;
        uint8_t a = (uint8_t)(((uint32_t)(y - y0) * 255) / (uint32_t)(y1 - y0));
        uint16_t color = __xzd_blend565(c_top, c_bot, a);
        uint16_t inset = 0;
        uint16_t lx, rx;

        if (dd < r) {
            uint32_t t = (uint32_t)r * r - (uint32_t)(r - dd) * (r - dd);
            inset = (uint16_t)(r - __xzd_isqrt(t));
        }
        lx = (uint16_t)(x0 + inset);
        rx = (uint16_t)(x1 - inset);
        if (rx < lx) {
            continue;
        }
        xzd_ui_fill(fb, lx, y, rx, y, color, 255);
    }
}

void xzd_ui_draw_circle(TDL_DISP_FRAME_BUFF_T *fb, uint16_t cx, uint16_t cy,
                        uint16_t r, uint16_t color, uint8_t alpha)
{
    int16_t dy;

    if (NULL == fb || NULL == fb->frame) {
        return;
    }
    for (dy = -(int16_t)r; dy <= (int16_t)r; dy++) {
        int32_t rem = (int32_t)r * r - (int32_t)dy * dy;
        int32_t hw;
        int32_t x0, x1;

        if (rem < 0) {
            continue;
        }
        hw = (int32_t)__xzd_isqrt((uint32_t)rem);
        x0 = (int32_t)cx - hw;
        x1 = (int32_t)cx + hw;
        if (x0 < 0) {
            x0 = 0;
        }
        if (x1 >= XZD_UI_W) {
            x1 = XZD_UI_W - 1;
        }
        if (x1 < x0) {
            continue;
        }
        xzd_ui_fill(fb, (uint16_t)x0, (uint16_t)(cy + dy),
                    (uint16_t)x1, (uint16_t)(cy + dy), color, alpha);
    }
}

void xzd_ui_draw_dot(TDL_DISP_FRAME_BUFF_T *fb, uint16_t cx, uint16_t cy,
                     uint16_t r, uint16_t color)
{
    xzd_ui_draw_circle(fb, cx, cy, r, color, 255);
}

static const int8_t sg_spin_dots[12][2] = {
    {0, -23}, {12, -20}, {20, -12}, {23, 0}, {20, 12}, {12, 20},
    {0, 23}, {-12, 20}, {-20, 12}, {-23, 0}, {-20, -12}, {-12, -20}
};

void xzd_ui_draw_spinner(TDL_DISP_FRAME_BUFF_T *fb, uint16_t cx, uint16_t cy,
                         uint16_t r, uint16_t color, uint8_t ring_alpha,
                         int16_t head)
{
    int16_t i;
    int16_t hi = head % 12;

    if (hi < 0) {
        hi += 12;
    }
    for (i = 0; i < 12; i++) {
        int16_t px = (int16_t)cx + (sg_spin_dots[i][0] * (int16_t)r) / 23;
        int16_t py = (int16_t)cy + (sg_spin_dots[i][1] * (int16_t)r) / 23;

        if (i == hi) {
            xzd_ui_fill(fb, (uint16_t)(px - 2), (uint16_t)(py - 2),
                        (uint16_t)(px + 2), (uint16_t)(py + 2), color, 255);
        } else {
            xzd_ui_fill(fb, (uint16_t)(px - 1), (uint16_t)(py - 1),
                        (uint16_t)(px + 1), (uint16_t)(py + 1), color, ring_alpha);
        }
    }
}

void xzd_ui_draw_corners(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                         uint16_t x1, uint16_t y1, uint8_t len,
                         uint16_t color, uint8_t alpha)
{
    uint8_t t = 3;

    /* top-left */
    xzd_ui_fill(fb, x0, y0, (uint16_t)(x0 + len), (uint16_t)(y0 + t - 1), color, alpha);
    xzd_ui_fill(fb, x0, y0, (uint16_t)(x0 + t - 1), (uint16_t)(y0 + len), color, alpha);
    /* top-right */
    xzd_ui_fill(fb, (uint16_t)(x1 - len), y0, x1, (uint16_t)(y0 + t - 1), color, alpha);
    xzd_ui_fill(fb, (uint16_t)(x1 - t + 1), y0, x1, (uint16_t)(y0 + len), color, alpha);
    /* bottom-left */
    xzd_ui_fill(fb, x0, (uint16_t)(y1 - t + 1), (uint16_t)(x0 + len), y1, color, alpha);
    xzd_ui_fill(fb, x0, (uint16_t)(y1 - len), (uint16_t)(x0 + t - 1), y1, color, alpha);
    /* bottom-right */
    xzd_ui_fill(fb, (uint16_t)(x1 - len), (uint16_t)(y1 - t + 1), x1, y1, color, alpha);
    xzd_ui_fill(fb, (uint16_t)(x1 - t + 1), (uint16_t)(y1 - len), x1, y1, color, alpha);
}
