// buzzer.c（示例）
#include "buzzer.h"
#include "gpio.h" // 假设用 GPIO 控制

void buzzer_init(void)
{
    buzzer_set(false); // 初始化时关闭蜂鸣器
}

void buzzer_set(bool on)
{
    HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**********************************************************************************/
#include "FreeRTOS.h"
#include "timers.h"
// 静态定时器句柄（可复用）
static TimerHandle_t xBeepTimer = NULL;

// 定时器回调：关闭蜂鸣器
static void vBeepTimerCallback(TimerHandle_t xTimer)
{
    (void) xTimer;
    buzzer_set(false); // 关闭蜂鸣器
}
bool buzzer_beep(uint32_t duration_ms)
{
    // 首次调用时创建一次性定时器（自动删除）
    if (xBeepTimer == NULL)
    {
        xBeepTimer = xTimerCreate("BeepTimer", pdMS_TO_TICKS(duration_ms),
                                  pdFALSE, // one-shot（一次性）
                                  (void *) 0, vBeepTimerCallback);
        if (xBeepTimer == NULL) return false;
    }
    else
    {
        // 重置定时器周期（如果正在运行，先停止）
        if (xTimerIsTimerActive(xBeepTimer) != pdFALSE)
        {
            xTimerStop(xBeepTimer, 0);
        }
        // 重新设置周期
        if (xTimerChangePeriod(xBeepTimer, pdMS_TO_TICKS(duration_ms), 0) != pdPASS)
        {
            return false;
        }
    }

    // 开启蜂鸣器
    buzzer_set(true);

    // 启动定时器
    if (xTimerStart(xBeepTimer, 0) != pdPASS)
    {
        buzzer_set(false);
        return false;
    }

    return true;
}
