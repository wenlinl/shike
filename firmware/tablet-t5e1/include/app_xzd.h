/**
 * @file app_xzd.h
 * @brief XianZhidao (鲜知道) main application.
 *
 * Standby page (放入/取出) -> 3 s semi-transparent countdown over the live
 * camera preview -> auto capture (JPEG) + shutter click -> HTTPS upload to
 * /api/scan -> result page / error / offline fallback.
 */
#ifndef __APP_XZD_H__
#define __APP_XZD_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise display, touch, button, camera, audio, network and UI.
 * @note Call after board_register_hardware().
 */
OPERATE_RET app_xzd_init(void);

/**
 * @brief Periodic task: state machine, touch/button handling, UI updates.
 *        Call from the main loop (e.g. every 20 ms).
 */
void app_xzd_loop(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_XZD_H__ */
