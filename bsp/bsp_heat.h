/**
 * @file    bsp_heat.h
 * @brief   加热模块驱动（基于 PWM，无 OS 依赖）
 * @version 2.0
 * @date    2026-06-06
 */

#ifndef BSP_HEAT_H
#define BSP_HEAT_H

#include "bsp_common.h"
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 类型定义
 *============================================================================*/

/** 不透明加热模块句柄 */
BSP_HANDLE_DECLARE(heat);

/** 加热模块配置 */
typedef struct {
    TIM_HandleTypeDef* htim; /**< HAL 定时器句柄 */
    uint32_t channel;        /**< PWM 通道 */
    uint32_t freq_hz;        /**< PWM 频率 */
    uint32_t resolution;     /**< 占空比分辨率 */
} bsp_heat_config_t;

/*==============================================================================
 * API 函数
 *============================================================================*/

#define BSP_HEAT_MAX_POWER 100U /**< 最大功率百分比 */

/**
 * @brief 初始化加热模块
 * @param[out] handle 返回的句柄
 * @param[in]  config 硬件配置
 * @return BSP_OK 成功
 */
bsp_status_t bsp_heat_init(bsp_heat_t** handle, const bsp_heat_config_t* config);

/**
 * @brief 反初始化（停止加热 + 释放资源）
 * @param handle 句柄指针的指针
 */
void bsp_heat_deinit(bsp_heat_t** handle);

/**
 * @brief 启动加热（使能 PWM 输出）
 */
bsp_status_t bsp_heat_start(bsp_heat_t* handle);

/**
 * @brief 停止加热（禁用 PWM 输出）
 */
bsp_status_t bsp_heat_stop(bsp_heat_t* handle);

/**
 * @brief 设置加热功率
 * @param handle        加热句柄
 * @param power_percent 功率百分比 [0, 100]
 * @return BSP_OK 成功
 */
bsp_status_t bsp_heat_set_power(bsp_heat_t* handle, uint8_t power_percent);

/**
 * @brief 获取当前功率
 * @return 功率百分比 [0, 100]，失败返回 0
 */
uint8_t bsp_heat_get_power(bsp_heat_t* handle);

#ifdef __cplusplus
}
#endif

#endif /* BSP_HEAT_H */
