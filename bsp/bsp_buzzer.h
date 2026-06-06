/**
 * @file    bsp_buzzer.h
 * @brief   蜂鸣器驱动（纯硬件操作，无 OS 依赖）
 * @version 2.0
 * @date    2026-06-06
 *
 * @note    BSP 层仅提供硬件控制接口（开/关/翻转）。
 *          需要定时关闭等逻辑请在上层 service 中实现。
 */

#ifndef BSP_BUZZER_H
#define BSP_BUZZER_H

#include "bsp_common.h"
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 类型定义
 *============================================================================*/

/** 不透明蜂鸣器句柄 */
BSP_HANDLE_DECLARE(buzzer);

/** 蜂鸣器硬件配置 */
typedef struct {
    GPIO_TypeDef* port;          /**< GPIO 端口 */
    uint16_t      pin;           /**< GPIO 引脚 */
    GPIO_PinState active_level;  /**< 有效电平（高/低） */
} bsp_buzzer_config_t;

/*==============================================================================
 * API 函数
 *============================================================================*/

/**
 * @brief 初始化蜂鸣器
 * @param[out] handle  返回的蜂鸣器句柄
 * @param[in]  config  硬件配置
 * @return BSP_OK 成功
 */
bsp_status_t bsp_buzzer_init(bsp_buzzer_t** handle, const bsp_buzzer_config_t* config);

/**
 * @brief 反初始化蜂鸣器（关闭并释放资源）
 * @param handle 句柄指针的指针
 */
void bsp_buzzer_deinit(bsp_buzzer_t** handle);

/**
 * @brief 开启蜂鸣器
 */
bsp_status_t bsp_buzzer_on(bsp_buzzer_t* handle);

/**
 * @brief 关闭蜂鸣器
 */
bsp_status_t bsp_buzzer_off(bsp_buzzer_t* handle);

/**
 * @brief 翻转蜂鸣器状态
 */
bsp_status_t bsp_buzzer_toggle(bsp_buzzer_t* handle);

/**
 * @brief 获取蜂鸣器当前状态
 * @return true=响, false=不响
 */
bool bsp_buzzer_is_on(bsp_buzzer_t* handle);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BUZZER_H */
