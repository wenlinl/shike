/**
 * @file app_xzd_audio.c
 * @brief Synthesised shutter click + countdown tick through the on-board codec.
 *
 * The board codec runs 16 kHz / mono / 16-bit. The shutter "咔嚓" is two short
 * noise bursts; the countdown "叮" is a 1.2 kHz sine with exponential decay.
 * All waveforms are generated at init with integer math (no libm dependency).
 */
#include "tuya_cloud_types.h"
#include "tal_api.h"

#include "tdl_audio_manage.h"

#include "app_xzd_audio.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define XZD_CLICK_LEN_MS       110
#define XZD_CLICK_AMP          12000
#define XZD_TICK_LEN_MS        180
#define XZD_TICK_AMP           9500
#define XZD_TICK_FREQ_HZ       1568
#define XZD_AUDIO_RATE         16000

/***********************************************************
***********************variable define**********************
***********************************************************/
static TDL_AUDIO_HANDLE_T sg_audio_hdl = NULL;
static int16_t *sg_click_pcm = NULL;
static uint32_t sg_click_len = 0;
static int16_t *sg_tick_pcm = NULL;
static uint32_t sg_tick_len = 0;

/***********************************************************
***********************function define**********************
***********************************************************/
static uint32_t __xzd_rng(void)
{
    static uint32_t s = 0x9E3779B9U;

    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* Quarter-sine table: sin(pi*i/32) * 32767, i = 0..15. */
static const int16_t sg_sin_tab[16] = {
        0,  3212,  6393,  9512, 12539, 15447, 18205, 20788,
    23170, 25330, 27246, 28899, 30274, 31357, 32138, 32610,
};

static int32_t __xzd_sin_wave(uint32_t idx)
{
    uint32_t q = idx & 63U;
    int32_t v;

    if (q < 32U) {
        v = (q < 16U) ? sg_sin_tab[q] : sg_sin_tab[31U - q];
    } else {
        q -= 32U;
        v = -((q < 16U) ? sg_sin_tab[q] : sg_sin_tab[31U - q]);
    }
    return v;
}

static void __xzd_synth_click(void)
{
    uint32_t total = (XZD_CLICK_LEN_MS * XZD_AUDIO_RATE) / 1000;
    float env1[5];
    float env2[5];
    uint32_t i;
    int k;

    /* Sharp 4 ms decay per burst — crisp mechanical "咔嚓". */
    env1[0] = 1.0f;
    env2[0] = 1.0f;
    for (k = 1; k <= 4; k++) {
        env1[k] = env1[k - 1] * 0.55f;
        env2[k] = env2[k - 1] * 0.55f;
    }

    sg_click_pcm = (int16_t *)tal_psram_malloc(total * sizeof(int16_t));
    if (NULL == sg_click_pcm) {
        PR_ERR("xzd audio: click pcm alloc failed");
        return;
    }
    sg_click_len = total;

    for (i = 0; i < total; i++) {
        float env = 0.0f;
        int32_t sample;
        uint32_t t_ms = (i * 1000) / XZD_AUDIO_RATE;

        /* First click: t=0, 4 ms decay */
        if (t_ms < 4) {
            env += env1[t_ms];
        }
        /* Second click: t=70 ms, 4 ms decay */
        if (t_ms >= 70 && t_ms < 74) {
            env += env2[t_ms - 70];
        }
        sample = (int32_t)(((int32_t)__xzd_rng() - 0x80000000) / 131072 * env);
        if (sample > XZD_CLICK_AMP) {
            sample = XZD_CLICK_AMP;
        }
        if (sample < -XZD_CLICK_AMP) {
            sample = -XZD_CLICK_AMP;
        }
        sg_click_pcm[i] = (int16_t)sample;
    }
}

static void __xzd_synth_tick(void)
{
    uint32_t total = (XZD_TICK_LEN_MS * XZD_AUDIO_RATE) / 1000;
    uint32_t step = (uint32_t)(((uint64_t)XZD_TICK_FREQ_HZ << 16) / XZD_AUDIO_RATE);
    uint32_t step2 = (uint32_t)(((uint64_t)(XZD_TICK_FREQ_HZ * 2) << 16) / XZD_AUDIO_RATE);
    uint32_t phase = 0;
    uint32_t phase2 = 0;
    float env_tab[XZD_TICK_LEN_MS];
    uint32_t i;
    int k;

    /* Slow bell-like decay: ~0.82 per 10 ms. */
    env_tab[0] = 1.0f;
    for (k = 1; k < XZD_TICK_LEN_MS; k++) {
        env_tab[k] = env_tab[k - 1] * 0.982f;
    }

    sg_tick_pcm = (int16_t *)tal_psram_malloc(total * sizeof(int16_t));
    if (NULL == sg_tick_pcm) {
        PR_ERR("xzd audio: tick pcm alloc failed");
        return;
    }
    sg_tick_len = total;

    for (i = 0; i < total; i++) {
        uint32_t t_ms = (i * 1000) / XZD_AUDIO_RATE;
        int32_t s1 = __xzd_sin_wave(phase >> 10);
        int32_t s2 = __xzd_sin_wave(phase2 >> 10);
        int32_t sample = (int32_t)((s1 + s2 / 4) * XZD_TICK_AMP * env_tab[t_ms] / 32767);

        sg_tick_pcm[i] = (int16_t)sample;
        phase += step;
        phase2 += step2;
    }
}

OPERATE_RET xzd_audio_init(void)
{
    OPERATE_RET rt = OPRT_OK;
    TDL_AUDIO_INFO_T info = {0};

    rt = tdl_audio_find(AUDIO_CODEC_NAME, &sg_audio_hdl);
    if (rt != OPRT_OK || NULL == sg_audio_hdl) {
        PR_ERR("xzd audio: find '%s' failed %d", AUDIO_CODEC_NAME, rt);
        return (rt != OPRT_OK) ? rt : OPRT_COM_ERROR;
    }

    rt = tdl_audio_open(sg_audio_hdl, NULL); /* playback only */
    if (rt != OPRT_OK) {
        PR_ERR("xzd audio: open failed %d", rt);
        return rt;
    }

    tdl_audio_get_info(sg_audio_hdl, &info);
    PR_NOTICE("xzd audio: rate=%u ch=%u bits=%u tm=%ums",
              info.sample_rate, info.sample_ch_num, info.sample_bits, info.sample_tm_ms);

    tdl_audio_volume_set(sg_audio_hdl, 75);
    __xzd_synth_click();
    __xzd_synth_tick();

    return OPRT_OK;
}

void xzd_audio_play_click(void)
{
    if (NULL == sg_audio_hdl || NULL == sg_click_pcm || sg_click_len == 0) {
        return;
    }
    tdl_audio_play(sg_audio_hdl, (uint8_t *)sg_click_pcm, sg_click_len * sizeof(int16_t));
}

void xzd_audio_play_tick(void)
{
    if (NULL == sg_audio_hdl || NULL == sg_tick_pcm || sg_tick_len == 0) {
        return;
    }
    tdl_audio_play(sg_audio_hdl, (uint8_t *)sg_tick_pcm, sg_tick_len * sizeof(int16_t));
}
