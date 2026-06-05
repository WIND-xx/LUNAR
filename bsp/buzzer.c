/**
 * @file buzzer.c
 * @brief 蜂鸣器驱动（FreeRTOS 定时器实现）
 * @version 1.1
 */

#include "buzzer.h"
#include "FreeRTOS.h"
#include "gpio.h"
#include "timers.h"

static TimerHandle_t xBeepTimer = NULL;

static void vBeepTimerCallback(TimerHandle_t xTimer)
{
    (void)xTimer;
    buzzer_set(false);
}

void buzzer_init(void)
{
    buzzer_set(false);
}

void buzzer_set(bool on)
{
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool buzzer_beep(uint32_t duration_ms)
{
    // 首次调用时创建一次性定时器
    if (xBeepTimer == NULL) {
        xBeepTimer = xTimerCreate("BeepTimer", pdMS_TO_TICKS(duration_ms), pdFALSE, (void*)0, vBeepTimerCallback);
        if (xBeepTimer == NULL)
            return false;
    } else {
        // 若正在运行，先停止
        if (xTimerIsTimerActive(xBeepTimer) != pdFALSE) {
            xTimerStop(xBeepTimer, 0);
        }
        // 更新定时器周期
        if (xTimerChangePeriod(xBeepTimer, pdMS_TO_TICKS(duration_ms), 0) != pdPASS) {
            return false;
        }
    }

    // 开启蜂鸣器
    buzzer_set(true);

    // 启动定时器（失败则回滚）
    if (xTimerStart(xBeepTimer, 0) != pdPASS) {
        buzzer_set(false);
        return false;
    }

    return true;
}
