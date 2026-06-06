/**
 * @file key_service.c
 * @brief 按键服务：扫描去抖 + 事件发布 + 业务处理
 * @version 4.0
 */

#include "key_service.h"
#include "app_config.h"
#include "bsp_key.h"
#include "event_bus.h"
#include "gpio.h"

#include "FreeRTOS.h"
#include "task.h"

/*============================================================================
 * BSP 句柄
 *============================================================================*/
static bsp_key_t* s_key_handle = NULL;

/*============================================================================
 * 键盘硬件配置（编译期常量）
 *============================================================================*/
static const bsp_key_row_t key_rows[] = {
    {GPIO_PIN_9, {GPIO_PIN_8, GPIO_PIN_3, GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_15}, {1, 2, 3, 4, 5}},
    {GPIO_PIN_8, {GPIO_PIN_3, GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_15, 0}, {6, 7, 8, 9, 0}},
    {GPIO_PIN_3, {GPIO_PIN_5, GPIO_PIN_4, GPIO_PIN_15, 0, 0}, {12, 10, 11, 0, 0}},
    {GPIO_PIN_5, {GPIO_PIN_4, GPIO_PIN_15, 0, 0, 0}, {14, 13, 0, 0, 0}},
    {GPIO_PIN_4, {GPIO_PIN_15, 0, 0, 0, 0}, {15, 0, 0, 0, 0}},
};

static const bsp_key_power_pin_t power_pin = {
    .port      = POWER_DC_GPIO_Port,
    .pin       = POWER_DC_Pin,
    .key_value = 18, /* KEY_POWER */
};

/*============================================================================
 * 扫描状态
 *============================================================================*/
static uint8_t  s_key_before  = 0;
static uint8_t  s_key_state   = 0;
static uint16_t s_key_counter = 0;

/*============================================================================
 * 按键扫描任务（20ms 周期）
 *============================================================================*/
static void key_scan_task(void* arg)
{
    (void)arg;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        /* 先检查电源键 */
        uint8_t now = 0;
        if (bsp_key_is_power_pressed(s_key_handle)) {
            now = 18; /* KEY_POWER */
        } else {
            bsp_key_scan(s_key_handle, &now);
        }

        if (s_key_before == 0 && now != 0) {
            /* 按键刚按下 */
            s_key_counter = 0;
            s_key_state   = 0;
        } else if (s_key_before != 0 && now == 0) {
            /* 按键释放 */
            if (s_key_state == 0) {
                if (s_key_counter < KEY_LONG_PRESS_TICKS) {
                    /* 短按 */
                    event_publish(EVENT_KEY_SHORT_PRESS, s_key_before);
                }
            }
            s_key_counter = 0;
        } else if (now != 0) {
            /* 按键持续按下 */
            s_key_counter++;
            if (s_key_counter >= KEY_LONG_PRESS_TICKS && s_key_state == 0) {
                s_key_state = 1;
                /* 长按 */
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
    bsp_key_config_t cfg = {
        .rows      = key_rows,
        .row_count = BSP_ARRAY_SIZE(key_rows),
        .power_pin = &power_pin,
        .row_port  = GPIOB,
    };
    bsp_key_init(&s_key_handle, &cfg);

    xTaskCreate(key_scan_task, TASK_KEY_NAME, TASK_KEY_STACK, NULL, TASK_KEY_PRIORITY, NULL);
}
