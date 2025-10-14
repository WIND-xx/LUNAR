#include "heat.h"
#include "stm32f103xb.h"
#include "tim.h"

void heat_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    // 初始化加热控制硬件
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4);
}

void heat_deinit(void)
{
    // 反初始化加热控制硬件
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_4);
}

void heat_on(float power)
{
    // 设置加热功率，power 范围为 0.0 到 100.0（百分比）
    if (power < 0.0f) power = 0.0f;
    if (power > 100.0f) power = 100.0f;

    uint32_t pulse = (uint32_t) ((power / 100.0f) * (__HAL_TIM_GET_AUTORELOAD(&htim1) + 1));
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, pulse);
}
void heat_off(void)
{
    // 关闭加热
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, 0);
}
