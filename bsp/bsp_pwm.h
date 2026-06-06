/**
 * @file    bsp_pwm.h
 * @brief   PWM 驱动（统一 BSP 接口，不透明句柄模式）
 * @version 2.0
 * @date    2026-06-06
 */

#ifndef BSP_PWM_H
#define BSP_PWM_H

#include "bsp_common.h"
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 类型定义
 *============================================================================*/

/** 不透明 PWM 句柄 */
BSP_HANDLE_DECLARE(pwm);

/** PWM 通道 */
typedef uint32_t bsp_pwm_channel_t;

/** PWM 初始化配置 */
typedef struct {
    TIM_HandleTypeDef* htim;   /**< HAL 定时器句柄（必须已在 CubeMX 中配置） */
    bsp_pwm_channel_t channel; /**< TIM 通道（如 TIM_CHANNEL_1） */
    uint32_t freq_hz;          /**< PWM 频率 (Hz) */
    uint32_t resolution;       /**< 占空比分辨率（如 1000 表示 0~999） */
} bsp_pwm_config_t;

/*==============================================================================
 * API 函数
 *============================================================================*/

/**
 * @brief 初始化 PWM 实例
 * @param[out] handle  返回的 PWM 句柄指针
 * @param[in]  config  配置参数
 * @return BSP_OK 成功，其他错误码见 bsp_common.h
 */
bsp_status_t bsp_pwm_init(bsp_pwm_t** handle, const bsp_pwm_config_t* config);

/**
 * @brief 反初始化 PWM 实例，释放资源
 * @param handle PWM 句柄指针的指针（调用后 *handle = NULL）
 */
void bsp_pwm_deinit(bsp_pwm_t** handle);

/**
 * @brief 设置占空比
 * @param handle PWM 句柄
 * @param duty   占空比值 [0, resolution)
 * @return BSP_OK 成功
 */
bsp_status_t bsp_pwm_set_duty(bsp_pwm_t* handle, uint32_t duty);

/**
 * @brief 启动 PWM 输出
 * @param handle PWM 句柄
 * @return BSP_OK 成功
 */
bsp_status_t bsp_pwm_start(bsp_pwm_t* handle);

/**
 * @brief 停止 PWM 输出
 * @param handle PWM 句柄
 * @return BSP_OK 成功
 */
bsp_status_t bsp_pwm_stop(bsp_pwm_t* handle);

/**
 * @brief 获取当前分辨率（用于占空比计算）
 * @param handle PWM 句柄
 * @return 分辨率值，失败返回 0
 */
uint32_t bsp_pwm_get_resolution(bsp_pwm_t* handle);

#ifdef __cplusplus
}
#endif

#endif /* BSP_PWM_H */
