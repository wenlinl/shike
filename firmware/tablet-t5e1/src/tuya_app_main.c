#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_output.h"
#include "tal_cli.h"

#include "board_com_api.h"

#include "app_xzd.h"

static void user_main(void)
{
    OPERATE_RET rt = OPRT_OK;

    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);
    PR_DEBUG("initializing board hardware...\r\n");

    /* Required by netmgr / WiFi stack (see SDK http_client example). */
    tal_sw_timer_init();
    tal_workq_init();

    rt = board_register_hardware();
    if (rt != OPRT_OK) {
        PR_ERR("board_register_hardware failed: %d", rt);
        return;
    }

    rt = app_xzd_init();
    if (rt != OPRT_OK) {
        PR_ERR("xzd app init failed: %d", rt);
        return;
    }

    while (1) {
        app_xzd_loop();
        tal_system_sleep(20);
    }
}

/**
 * @brief main
 *
 * @param argc
 * @param argv
 * @return void
 */
#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    user_main();
}
#else

/* Tuya thread handle */
static THREAD_HANDLE ty_app_thread = NULL;

/**
 * @brief  task thread
 *
 * @param[in] arg:Parameters when creating a task
 * @return none
 */
static void tuya_app_thread(void *arg)
{
    user_main();

    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = 1024 * 4;
    thrd_param.priority = THREAD_PRIO_1;
    thrd_param.thrdname = "tuya_app_main";

    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
