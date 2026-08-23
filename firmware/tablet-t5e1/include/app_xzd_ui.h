/**
 * @file app_xzd_ui.h
 * @brief Framebuffer UI drawing for the firmware (RGB565 + alpha).
 *
 * All drawing targets a TDL display frame buffer (RGB565) in the native
 * portrait 240x320 coordinate space (matching the ST7789 panel and the
 * reference UI designs). Alpha blending is done per pixel so semi-transparent
 * overlays (countdown digits, dialogs) can be layered on top of the live
 * camera preview.
 */
#ifndef __APP_XZD_UI_H__
#define __APP_XZD_UI_H__

#include "tuya_cloud_types.h"
#include "tdl_display_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native portrait viewport (240x320), 1:1 with the panel. */
#define XZD_UI_W            240
#define XZD_UI_H            320

/* RGB565 colors */
#define XZD_COLOR_BLACK      0x0000
#define XZD_COLOR_WHITE      0xFFFF
#define XZD_COLOR_GRAY       0x8410
#define XZD_COLOR_DARK       0x18E3
#define XZD_COLOR_RED        0xF800
#define XZD_COLOR_GREEN      0x07E0
#define XZD_COLOR_BLUE       0x001F
#define XZD_COLOR_ORANGE     0xFD20
#define XZD_COLOR_YELLOW     0xFFE0
#define XZD_COLOR_BTN_IN     0x1B60   /* 放入: fresh green */
#define XZD_COLOR_BTN_OUT    0xFD20   /* 取出: warm orange */
#define XZD_COLOR_BTN_GRAY   0x39E7

/* ---- 食刻 design tokens (from the reference UI) ---- */
#define XZD_COLOR_BG         0xFF9D   /* #f8f1e9 cream background        */
#define XZD_COLOR_CARD       0xFFFF   /* #fffdf9 card white              */
#define XZD_COLOR_INK        0x51C5   /* #50382b dark brown text         */
#define XZD_COLOR_INK2       0xA46F   /* #a08c7c secondary text          */
#define XZD_COLOR_LINE       0xEF1A   /* #efe3d5 divider                 */
#define XZD_COLOR_RED2       0xE1E5   /* #e23c2b brand red               */
#define XZD_COLOR_RED_D      0xC984   /* #c93122 brand red dark          */
#define XZD_COLOR_RED_TOP    0xEAC8   /* #ef5a41 gradient top            */
#define XZD_COLOR_GREEN_OK   0x4CCC   /* #4d9a62 success green           */
#define XZD_COLOR_GREEN_D    0x3BEA   /* #3d7f50 deep green text         */
#define XZD_COLOR_GREEN_SOFT 0xEF9D   /* #e8f1e9 green soft pill         */
#define XZD_COLOR_AMBER      0xEC87   /* #e8913c reminder orange         */
#define XZD_COLOR_AMBER_SOFT 0xFF99   /* #fdf0cd amber soft pill         */
#define XZD_COLOR_AMBER_INK  0xA343   /* #a06b1c amber text              */
#define XZD_COLOR_BLUE2      0x3BB5   /* #3f76ad out-mode blue           */
#define XZD_COLOR_BLUE_SOFT  0xDF7F   /* #deeefc blue soft pill          */
#define XZD_COLOR_BROWN_BAR  0x49C6   /* #4a3a32 offline bar             */
#define XZD_COLOR_ERR_SOFT   0xFF1B   /* #fde3dc low-conf icon bg        */
#define XZD_COLOR_WARM2      0xF73B   /* #f6e6d8 boot gradient bottom    */
#define XZD_COLOR_CORNER     0x8E8D   /* #8fd06f scan corner brackets    */

/**
 * @brief Configure RGB565 byte-swap behaviour (call with display info is_swap).
 */
void xzd_ui_set_swap(bool is_swap);

/**
 * @brief Enable horizontal/vertical mirroring for all drawing. Mirror both
 *        axes when the panel is mounted upside-down (180 degrees). Both
 *        false = native portrait, matching the reference UI.
 */
void xzd_ui_set_flip(bool flip_x, bool flip_y);

/**
 * @brief Map a logical portrait coordinate (240x320) to the panel frame
 *        buffer (240x320), including optional axis mirrors.
 */
void xzd_ui_logical_to_fb(TDL_DISP_FRAME_BUFF_T *fb, uint16_t lx, uint16_t ly,
                          uint16_t *fx, uint16_t *fy);

/**
 * @brief Inverse map: panel/touch coordinates -> logical portrait coordinates.
 */
void xzd_ui_fb_to_logical(TDL_DISP_FRAME_BUFF_T *fb, uint16_t fx, uint16_t fy,
                          uint16_t *lx, uint16_t *ly);

/**
 * @brief In-place vertical flip of the raw frame buffer rows (used once for
 *        pixel sources that bypass the draw accessors, e.g. camera frames).
 */
void xzd_ui_flip_vertical(TDL_DISP_FRAME_BUFF_T *fb);

/**
 * @brief In-place horizontal flip of the raw frame buffer (used for pixel
 *        sources that bypass the draw accessors, e.g. camera frames).
 */
void xzd_ui_flip_horizontal(TDL_DISP_FRAME_BUFF_T *fb);

/**
 * @brief Fill a rectangle with alpha blending over the current pixels.
 */
void xzd_ui_fill(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                 uint16_t x1, uint16_t y1, uint16_t color, uint8_t alpha);

/**
 * @brief Fill the whole frame buffer (no blending, alpha=255 fast path).
 */
void xzd_ui_fill_full(TDL_DISP_FRAME_BUFF_T *fb, uint16_t color);

/**
 * @brief Measure the rendered width (pixels) of a UTF-8 string.
 */
uint16_t xzd_ui_text_width(const char *s);

/**
 * @brief Draw a UTF-8 string at (x, y) (top-left), 16 px tall.
 */
void xzd_ui_draw_text(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x, uint16_t y,
                      const char *s, uint16_t color, uint8_t alpha);

/**
 * @brief Draw a UTF-8 string horizontally centered on cx.
 */
void xzd_ui_draw_text_center(TDL_DISP_FRAME_BUFF_T *fb, uint16_t cx, uint16_t y,
                             const char *s, uint16_t color, uint8_t alpha);

/**
 * @brief Draw one of the big countdown digits '0'..'9', centered at (cx, cy).
 */
void xzd_ui_draw_big_digit(TDL_DISP_FRAME_BUFF_T *fb, char digit,
                           uint16_t cx, uint16_t cy, uint16_t color, uint8_t alpha);

/**
 * @brief Draw a labeled button (filled bg + centered label + border).
 */
void xzd_ui_draw_button(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                        uint16_t x1, uint16_t y1, uint16_t bg, uint16_t fg,
                        const char *label);

/**
 * @brief Draw a thin rectangle border.
 */
void xzd_ui_draw_rect(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                      uint16_t x1, uint16_t y1, uint16_t color, uint8_t alpha);

/**
 * @brief Fill a rounded rectangle (radius r) with alpha blending.
 */
void xzd_ui_fill_round_rect(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                            uint16_t x1, uint16_t y1, uint16_t r,
                            uint16_t color, uint8_t alpha);

/**
 * @brief Fill a rounded rectangle with a rounded border: outer rounded rect
 *        in `color`, inner (inset by bw) rounded rect in `fill`.
 */
void xzd_ui_fill_round_rect_bordered(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0,
                                     uint16_t y0, uint16_t x1, uint16_t y1,
                                     uint16_t r, uint8_t bw,
                                     uint16_t color, uint16_t fill);

/**
 * @brief Fill a rounded rectangle with a vertical gradient (top -> bottom).
 */
void xzd_ui_fill_round_rect_grad(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                                 uint16_t x1, uint16_t y1, uint16_t r,
                                 uint16_t c_top, uint16_t c_bot);

/**
 * @brief Fill a solid disc (filled circle) with alpha blending.
 */
void xzd_ui_draw_circle(TDL_DISP_FRAME_BUFF_T *fb, uint16_t cx, uint16_t cy,
                        uint16_t r, uint16_t color, uint8_t alpha);

/**
 * @brief Draw a small solid status dot.
 */
void xzd_ui_draw_dot(TDL_DISP_FRAME_BUFF_T *fb, uint16_t cx, uint16_t cy,
                     uint16_t r, uint16_t color);

/**
 * @brief Draw a spinner: a faint ring of 12 dots with one bright head that
 *        advances as `head` (0..11) increases.
 */
void xzd_ui_draw_spinner(TDL_DISP_FRAME_BUFF_T *fb, uint16_t cx, uint16_t cy,
                         uint16_t r, uint16_t color, uint8_t ring_alpha,
                         int16_t head);

/**
 * @brief Draw the four L-shaped scan corner brackets inside (x0,y0)-(x1,y1).
 */
void xzd_ui_draw_corners(TDL_DISP_FRAME_BUFF_T *fb, uint16_t x0, uint16_t y0,
                         uint16_t x1, uint16_t y1, uint8_t len,
                         uint16_t color, uint8_t alpha);

/**
 * @brief Hit test: is (x, y) inside the inclusive rect.
 */
bool xzd_ui_hit(uint16_t x, uint16_t y, uint16_t x0, uint16_t y0,
                uint16_t x1, uint16_t y1);

#ifdef __cplusplus
}
#endif

#endif /* __APP_XZD_UI_H__ */
