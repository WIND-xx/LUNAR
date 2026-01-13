#ifndef __PWM_DRIVER_H
#define __PWM_DRIVER_H

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

// PWM 配置参数（初始化用）
typedef struct
{
    uint32_t freq_hz;      // PWM 频率，单位 Hz
    uint32_t resolution;   // 分辨率，如 1000 表示占空比范围 0~999
    uint32_t init_duty;    // 初始占空比（0 ~ resolution-1）
} pwm_config_t;

// PWM 驱动实例
typedef struct
{
    TIM_HandleTypeDef* htim;
    uint32_t channel;
    uint32_t resolution;
} pwm_driver_t;

// 初始化 PWM（自动计算 Prescaler 和 Period）
HAL_StatusTypeDef pwm_driver_init(pwm_driver_t* self, TIM_HandleTypeDef* htim, uint32_t channel,
                                  const pwm_config_t* config);

// 设置占空比（整数，范围 [0, resolution)）
void pwm_driver_set_duty(pwm_driver_t* self, uint32_t duty);

// 启动 PWM 输出
static inline void pwm_driver_start(pwm_driver_t* self)
{
    if (self) HAL_TIM_PWM_Start(self->htim, self->channel);
}

// 停止 PWM 输出
static inline void pwm_driver_stop(pwm_driver_t* self)
{
    if (self) HAL_TIM_PWM_Stop(self->htim, self->channel);
}

#ifdef __cplusplus
}
#endif

#endif /* __PWM_DRIVER_H */
