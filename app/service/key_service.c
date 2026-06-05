/**
 * @file key_service.c
 * @brief 按键服务：扫描去抖 + 事件发布 + 业务处理
 * @version 3.0
 */

#include "key_service.h"
#include "app_config.h"
#include "event_bus.h"
#include "FreeRTOS.h"
#include "key.h"
#include "task.h"

/*============================================================================
 * 扫描状态
 *============================================================================*/
static uint8_t s_key_before = 0;    // 上一轮按键值
static uint8_t s_key_state = 0;     // 事件状态（0=等待, 1=长按已触发）
static uint16_t s_key_counter = 0;  // 按下持续计数

/*============================================================================
 * 按键扫描任务（20ms周期）
 *============================================================================*/
static void key_scan_task(void* arg)
{
    (void)arg;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        uint8_t now = get_key();

        if (s_key_before == 0 && now != 0) {
            /* 按键刚按下 */
            s_key_counter = 0;
            s_key_state = 0;
        } else if (s_key_before != 0 && now == 0) {
            /* 按键释放 */
            if (s_key_state == 0) {
                if (s_key_counter < KEY_LONG_PRESS_TICKS) {
                    /* 短按：持续时间 < 2秒 */
                    event_publish(EVENT_KEY_SHORT_PRESS, s_key_before);
                }
            }
            s_key_counter = 0;
        } else if (now != 0) {
            /* 按键持续按下 */
            s_key_counter++;
            if (s_key_counter >= KEY_LONG_PRESS_TICKS && s_key_state == 0) {
                s_key_state = 1;
                /* 长按：持续时间 >= 2秒 */
                event_publish(EVENT_KEY_LONG_PRESS, now);
            }
        }

        s_key_before = now;
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(KEY_SCAN_INTERVAL_MS));
    }
}

/*============================================================================
 * 初始化
 *============================================================================*/
void key_service_init(void)
{
    key_init();
    xTaskCreate(key_scan_task, TASK_KEY_NAME, TASK_KEY_STACK, NULL, TASK_KEY_PRIORITY, NULL);
}
