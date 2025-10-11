#include "led.h"
#include "FreeRTOS.h"
#include "gpio.h"
#include "main.h"
#include "semphr.h"
#include "timers.h"

#include <stdbool.h>
#include <string.h>

/**
 * @brief LED硬件配置结构体
 */
typedef struct
{
    GPIO_TypeDef *port;         // GPIO端口
    uint16_t      pin;          // GPIO引脚
    GPIO_PinState active_level; // 有效点亮电平
} led_hw_config_t;

/**
 * @brief 单色LED配置表
 * @note 对应LED_MUSIC到LED_RF的配置
 */
static const led_hw_config_t monochrome_led_config[LED_RF + 1] = {
    [LED_MUSIC] = {LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET}, [LED_BT] = {LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET},
    [LED_10MIN] = {LED6_GPIO_Port, LED6_Pin, GPIO_PIN_RESET}, [LED_30MIN] = {LED5_GPIO_Port, LED5_Pin, GPIO_PIN_RESET},
    [LED_60MIN] = {LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET}, [LED_RF] = {LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET},
};

/**
 * @brief 彩色LED通道配置
 */
static const led_hw_config_t color_led_config[3] = {
    // [0] = {LED_G_GPIO_Port, LED_G_Pin, GPIO_PIN_RESET}, // 绿色LED
    // [1] = {LED_R_GPIO_Port, LED_R_Pin, GPIO_PIN_RESET}, // 红色LED
    [2] = {LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_RESET}, // 蓝色LED
};

/**
 * @brief LED控制状态结构体
 */
typedef struct
{
    led_mode_t mode;        // 工作模式
    uint32_t   interval_ms; // 闪烁间隔(ms)
    TickType_t next_toggle; // 下一次翻转的时间戳
} led_control_t;

/**
 * @brief 所有LED的控制状态数组
 */
static led_control_t led_controls[LED_COUNT];

/**
 * @brief 线程安全互斥锁
 */
static SemaphoreHandle_t led_mutex = NULL;

/**
 * @brief LED闪烁定时器
 */
static TimerHandle_t blink_timer = NULL;

/**
 * @brief 获取彩色LED对应的通道索引
 * @param idx LED索引
 * @return 通道索引(0-2)，无效时返回-1
 */
static int led_get_color_channel(LED_Index idx)
{
    switch (idx)
    {
        case LED_G:
            return 0;
        case LED_R:
            return 1;
        case LED_B:
            return 2;
        default:
            return -1;
    }
}

/**
 * @brief 直接控制LED硬件状态
 * @param idx LED索引
 * @param on true: 点亮，false: 熄灭
 */
static void led_hw_set(LED_Index idx, bool on)
{
    if (idx >= LED_COUNT) return;

    if (idx <= LED_RF)
    {
        // 单色LED控制
        const led_hw_config_t *config = &monochrome_led_config[idx];
        GPIO_PinState          state =
            on ? config->active_level : (config->active_level == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET);
        HAL_GPIO_WritePin(config->port, config->pin, state);
    }
    else
    {
        // 彩色LED控制
        int channel = led_get_color_channel(idx);
        if (channel >= 0)
        {
            // 先关闭所有彩色LED通道
            for (int i = 0; i < 3; i++)
            {
                HAL_GPIO_WritePin(color_led_config[i].port, color_led_config[i].pin, GPIO_PIN_SET);
            }

            // 只点亮指定通道
            if (on)
            {
                HAL_GPIO_WritePin(color_led_config[channel].port, color_led_config[channel].pin,
                                  color_led_config[channel].active_level);
            }
        }
    }
}

/**
 * @brief 读取LED当前硬件状态
 * @param idx LED索引
 * @return true: 点亮状态，false: 熄灭状态
 */
static bool led_hw_get(LED_Index idx)
{
    if (idx >= LED_COUNT) return false;

    if (idx <= LED_RF)
    {
        // 读取单色LED状态
        const led_hw_config_t *config = &monochrome_led_config[idx];
        return (HAL_GPIO_ReadPin(config->port, config->pin) == config->active_level);
    }
    else
    {
        // 读取彩色LED状态
        int channel = led_get_color_channel(idx);
        if (channel >= 0)
        {
            return (HAL_GPIO_ReadPin(color_led_config[channel].port, color_led_config[channel].pin) ==
                    color_led_config[channel].active_level);
        }
    }

    return false;
}

/**
 * @brief 内部设置LED模式（需已获取互斥锁）
 * @param idx LED索引
 * @param mode 工作模式
 * @param interval_ms 闪烁间隔(ms)
 */
static void led_set_mode_internal(LED_Index idx, led_mode_t mode, uint32_t interval_ms)
{
    if (idx >= LED_COUNT) return;

    led_controls[idx].mode = mode;

    switch (mode)
    {
        case LED_MODE_BLINK:
            // 设置闪烁参数
            led_controls[idx].interval_ms = (interval_ms > 0) ? interval_ms : 500;
            led_controls[idx].next_toggle = xTaskGetTickCount() + pdMS_TO_TICKS(led_controls[idx].interval_ms);
            led_hw_set(idx, true); // 初始状态为点亮
            break;

        case LED_MODE_ON:
            led_hw_set(idx, true);
            break;

        case LED_MODE_OFF:
        default:
            led_hw_set(idx, false);
            break;
    }
}

/**
 * @brief LED闪烁定时器回调函数
 * @param timer 定时器句柄
 */
static void blink_timer_callback(TimerHandle_t timer)
{
    (void) timer; // 未使用参数

    if (led_mutex == NULL) return;

    // 尝试获取锁，立即返回如果获取失败
    if (xSemaphoreTake(led_mutex, 0) == pdTRUE)
    {
        TickType_t now = xTaskGetTickCount();

        // 更新所有闪烁模式的LED
        for (LED_Index idx = 0; idx < LED_COUNT; idx++)
        {
            if (led_controls[idx].mode == LED_MODE_BLINK)
            {
                // 检查是否到达翻转时间
                if ((int32_t) (now - led_controls[idx].next_toggle) >= 0)
                {
                    // 翻转LED状态
                    bool current_state = led_hw_get(idx);
                    led_hw_set(idx, !current_state);

                    // 更新下一次翻转时间
                    led_controls[idx].next_toggle = now + pdMS_TO_TICKS(led_controls[idx].interval_ms);
                }
            }
        }

        xSemaphoreGive(led_mutex);
    }
}

void led_init(void)
{
    // 创建互斥锁（确保只创建一次）
    if (led_mutex == NULL)
    {
        led_mutex = xSemaphoreCreateMutex();
        configASSERT(led_mutex != NULL);
    }

    // 初始化所有LED状态
    if (xSemaphoreTake(led_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        for (LED_Index idx = 0; idx < LED_COUNT; idx++)
        {
            led_controls[idx].mode = LED_MODE_OFF;
            led_controls[idx].interval_ms = 500;
            led_controls[idx].next_toggle = 0;
            led_hw_set(idx, false); // 初始状态为关闭
        }

        // 默认点亮蓝色LED（可根据需求调整）
        led_set_mode_internal(LED_B, LED_MODE_ON, 0);

        xSemaphoreGive(led_mutex);
    }

    // 创建并启动闪烁定时器（周期50ms）
    if (blink_timer == NULL)
    {
        blink_timer = xTimerCreate("LED_Blink",
                                   pdMS_TO_TICKS(50), // 50ms周期
                                   pdTRUE,            // 自动重载
                                   NULL, blink_timer_callback);

        configASSERT(blink_timer != NULL);
        xTimerStart(blink_timer, 0);
    }
}

void led_set_mode(LED_Index idx, led_mode_t mode, uint32_t interval_ms)
{
    if (led_mutex == NULL || idx >= LED_COUNT) return;

    // 尝试获取锁，超时时间10ms
    if (xSemaphoreTake(led_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        led_set_mode_internal(idx, mode, interval_ms);
        xSemaphoreGive(led_mutex);
    }
}

bool led_get(LED_Index idx)
{
    if (idx >= LED_COUNT || led_mutex == NULL) return false;

    bool state = false;

    // 尝试获取锁，超时时间10ms
    if (xSemaphoreTake(led_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        state = led_hw_get(idx);
        xSemaphoreGive(led_mutex);
    }

    return state;
}

void led_time_select(uint16_t minutes)
{
    if (led_mutex == NULL) return;

    // 尝试获取锁，超时时间10ms
    if (xSemaphoreTake(led_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        // 先关闭所有时间指示灯
        led_hw_set(LED_10MIN, false);
        led_hw_set(LED_30MIN, false);
        led_hw_set(LED_60MIN, false);

        // 根据输入设置相应的指示灯
        if (minutes == 0xFF)
        {
            // 全亮模式
            led_hw_set(LED_10MIN, true);
            led_hw_set(LED_30MIN, true);
            led_hw_set(LED_60MIN, true);

            // 更新控制状态
            led_controls[LED_10MIN].mode = LED_MODE_ON;
            led_controls[LED_30MIN].mode = LED_MODE_ON;
            led_controls[LED_60MIN].mode = LED_MODE_ON;
        }
        else if (minutes >= 60)
        {
            // 60分钟模式
            led_hw_set(LED_60MIN, true);

            // 更新控制状态
            led_controls[LED_10MIN].mode = LED_MODE_OFF;
            led_controls[LED_30MIN].mode = LED_MODE_OFF;
            led_controls[LED_60MIN].mode = LED_MODE_ON;
        }
        else if (minutes >= 30)
        {
            // 30分钟模式
            led_hw_set(LED_30MIN, true);

            // 更新控制状态
            led_controls[LED_10MIN].mode = LED_MODE_OFF;
            led_controls[LED_30MIN].mode = LED_MODE_ON;
            led_controls[LED_60MIN].mode = LED_MODE_OFF;
        }
        else if (minutes >= 10)
        {
            // 10分钟模式
            led_hw_set(LED_10MIN, true);

            // 更新控制状态
            led_controls[LED_10MIN].mode = LED_MODE_ON;
            led_controls[LED_30MIN].mode = LED_MODE_OFF;
            led_controls[LED_60MIN].mode = LED_MODE_OFF;
        }
        else
        {
            // 全灭模式
            led_controls[LED_10MIN].mode = LED_MODE_OFF;
            led_controls[LED_30MIN].mode = LED_MODE_OFF;
            led_controls[LED_60MIN].mode = LED_MODE_OFF;
        }

        xSemaphoreGive(led_mutex);
    }
}
