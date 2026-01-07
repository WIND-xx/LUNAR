#include "heat.h"
#include "stm32f103xb.h"
#include "tim.h"

void heat_init(void)
{
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, 0);
}

void heat_deinit(void)
{
    // 反初始化加热控制硬件
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_4);
}

void heat_on(uint16_t power)
{
    // 设置加热功率，power 范围为 0 到 100（百分比）
    if (power > 100) power = 100;

    // 修正：计算CCR值时直接乘以ARR（而非ARR+1），避免超过ARR
    uint32_t arr_val = __HAL_TIM_GET_AUTORELOAD(&htim1);
    uint32_t pulse   = (uint32_t)((power / 100.0f) * arr_val);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, pulse);
}

void heat_off(void)
{
    // 关闭加热：占空比0%
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, 0);
}
