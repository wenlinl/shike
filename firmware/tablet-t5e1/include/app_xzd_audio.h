/**
 * @file app_xzd_audio.h
 * @brief Shutter click audio for XianZhidao.
 */
#ifndef __APP_XZD_AUDIO_H__
#define __APP_XZD_AUDIO_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the on-board codec (playback only) and synthesise the click.
 */
OPERATE_RET xzd_audio_init(void);

/** @brief Play the camera "咔嚓" click (non-blocking fast write to codec). */
void xzd_audio_play_click(void);

/** @brief Play the countdown "叮" tick (short beep for each digit). */
void xzd_audio_play_tick(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_XZD_AUDIO_H__ */
