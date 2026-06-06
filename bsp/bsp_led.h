/**
 * @file    bsp_led.h
 * @brief   LED 驱动（纯硬件操作，无 OS 依赖）
 * @version 2.0
 * @date    2026-06-06
 *
 * @note    BSP 层管理所有 LED，提供模式控制（开/关/闪烁）。
 *          闪烁由上层周期性调用 bsp_led_poll() 驱动，
 *          无需 FreeRTOS 定时器。
 */

#ifndef BSP_LED_H
#define BSP_LED_H

#include "bsp_common.h"
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 类型定义
 *============================================================================*/

/** 不透明 LED 管理句柄 */
BSP_HANDLE_DECLARE(led);

/** LED 工作模式 */
typedef enum {
    BSP_LED_MODE_OFF = 0,   /**< 常灭 */
    BSP_LED_MODE_ON = 1,    /**< 常亮 */
    BSP_LED_MODE_BLINK = 2, /**< 闪烁 */
} bsp_led_mode_t;

/** 单个 LED 硬件配置 */
typedef struct {
    GPIO_TypeDef* port;         /**< GPIO 端口 */
    uint16_t pin;               /**< GPIO 引脚 */
    GPIO_PinState active_level; /**< 有效点亮电平 */
} bsp_led_hw_t;

/** LED 索引枚举（应用层使用，与硬件配置表顺序对应） */
typedef enum {
    BSP_LED_MUSIC = 0,
    BSP_LED_BT = 1,
    BSP_LED_10MIN = 2,
    BSP_LED_30MIN = 3,
    BSP_LED_60MIN = 4,
    BSP_LED_RF = 5,
    BSP_LED_B = 6,
    BSP_LED_COUNT = 7
} bsp_led_index_t;

/** LED 模块配置（所有 LED 的硬件表 + 数量） */
typedef struct {
    const bsp_led_hw_t* hw_table; /**< LED 硬件配置表 */
    uint8_t count;                /**< LED 数量 */
} bsp_led_config_t;

/*==============================================================================
 * API 函数
 *============================================================================*/

/**
 * @brief 初始化 LED 模块
 * @param[out] handle  返回的 LED 句柄
 * @param[in]  config  硬件配置表
 * @return BSP_OK 成功
 */
bsp_status_t bsp_led_init(bsp_led_t** handle, const bsp_led_config_t* config);

/**
 * @brief 反初始化 LED 模块（全灭 + 释放资源）
 * @param handle 句柄指针的指针
 */
void bsp_led_deinit(bsp_led_t** handle);

/**
 * @brief 设置指定 LED 的工作模式
 * @param handle       LED 句柄
 * @param index        LED 索引 [0, count)
 * @param mode         工作模式
 * @param interval_ms  闪烁间隔（仅 BSP_LED_MODE_BLINK 时有效，单位 ms）
 * @return BSP_OK 成功
 */
bsp_status_t bsp_led_set_mode(bsp_led_t* handle, uint8_t index, bsp_led_mode_t mode, uint32_t interval_ms);

/**
 * @brief 查询指定 LED 是否处于点亮状态
 * @param handle  LED 句柄
 * @param index   LED 索引
 * @return true=亮, false=灭
 */
bool bsp_led_is_on(bsp_led_t* handle, uint8_t index);

/**
 * @brief 周期性轮询（驱动闪烁逻辑）
 * @param handle      LED 句柄
 * @param elapsed_ms  距上次调用经过的毫秒数
 *
 * @note  上层（service/任务）应周期性调用此函数，
 *         典型周期 20~50ms。无 OS 依赖。
 */
void bsp_led_poll(bsp_led_t* handle, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif /* BSP_LED_H */
