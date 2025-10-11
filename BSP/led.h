#ifndef __LED_H
#define __LED_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "gpio.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief LED索引枚举，定义所有可用LED
 */
typedef enum
{
    LED_MUSIC, // 音乐指示灯
    LED_BT,    // 蓝牙指示灯
    LED_10MIN, // 10分钟指示灯
    LED_30MIN, // 30分钟指示灯
    LED_60MIN, // 60分钟指示灯
    LED_RF,    // 射频指示灯
    LED_G,     // 绿色LED
    LED_R,     // 红色LED
    LED_B,     // 蓝色LED
    LED_COUNT  // LED总数，用于边界检查
} LED_Index;

/**
 * @brief LED工作模式枚举
 */
typedef enum
{
    LED_MODE_OFF,  // 关闭模式
    LED_MODE_ON,   // 常亮模式
    LED_MODE_BLINK // 闪烁模式
} led_mode_t;

/**
 * @brief 初始化所有LED控制器
 * @note 需在系统启动时调用（FreeRTOS初始化之后）
 */
void led_init(void);

/**
 * @brief 设置指定LED的工作模式（线程安全）
 * @param idx LED索引（LED_Index）
 * @param mode 工作模式（LED_MODE_OFF/ON/BLINK）
 * @param interval_ms 闪烁间隔（毫秒），仅BLINK模式有效
 *                    若为0则使用默认值500ms
 */
void led_set_mode(LED_Index idx, led_mode_t mode, uint32_t interval_ms);

/**
 * @brief 读取指定LED的当前状态
 * @param idx LED索引
 * @return true: 点亮状态，false: 熄灭状态
 */
bool led_get(LED_Index idx);

/**
 * @brief 时间指示灯控制：10/30/60分钟互斥显示
 * @param minutes 可选值：10/30/60（对应时间）、0xFF（全亮）、0（全灭）
 */
void led_time_select(uint16_t minutes);

/**
 * @brief 保留的状态更新函数，兼容旧接口
 * @note FreeRTOS下由定时器自动处理，无需手动调用
 */
void led_update_states(void);

#ifdef __cplusplus
}
#endif

#endif /* __LED_H */
