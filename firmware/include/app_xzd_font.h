/**
 * @file app_xzd_font.h
 * @brief Generated bitmap font for the XianZhidao firmware
 *        (source: tools/gen_font.py + tools/fonts/HZK16,ASC16 — do not edit by hand).
 *
 * Pure 1-bit dot-matrix: CJK 16x16 (HZK16), ASCII 8x16 (ASC16),
 * big digits 24x24 (integer-scaled by the UI with nearest-neighbour).
 */
#ifndef __APP_XZD_FONT_H__
#define __APP_XZD_FONT_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XZD_FONT_CJK_W      16
#define XZD_FONT_CJK_H      16
#define XZD_FONT_ASCII_W    8
#define XZD_FONT_ASCII_H    16
#define XZD_FONT_BIG_W      24
#define XZD_FONT_BIG_H      24

typedef struct {
    uint8_t  w;                     /* glyph width in pixels */
    uint8_t  h;                     /* glyph height in pixels */
    const uint8_t *bits;            /* row-major, ceil(w/8) bytes per row, MSB first */
} XZD_GLYPH_T;

uint16_t xzd_font_cjk_count(void);
uint16_t xzd_font_cjk_cp_at(uint16_t idx);
bool xzd_font_get_cjk(uint16_t cp, XZD_GLYPH_T *out);
bool xzd_font_get_ascii(char c, XZD_GLYPH_T *out);
bool xzd_font_get_big_digit(char c, XZD_GLYPH_T *out);

#ifdef __cplusplus
}
#endif

#endif /* __APP_XZD_FONT_H__ */
