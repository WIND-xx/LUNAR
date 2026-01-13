#include "pwm_driver.h"
#include "stm32f1xx_hal.h"   // 根据你的芯片修改，如 stm32f4xx_hal.h

// 获取定时器时钟（自动区分 APB1/APB2）
static uint32_t get_timer_clk(TIM_TypeDef* tim)
{
    if (tim == TIM1
#if defined(TIM9)
        || tim == TIM8 || tim == TIM9 || tim == TIM10 || tim == TIM11
#endif
    )
    {
        // APB2 定时器
        uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
        return (RCC->CFGR & RCC_CFGR_PPRE2) ? pclk2 * 2 : pclk2;
    } else
    {
        // APB1 定时器
        uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();
        return (RCC->CFGR & RCC_CFGR_PPRE1) ? pclk1 * 2 : pclk1;
    }
}

HAL_StatusTypeDef pwm_driver_init(pwm_driver_t* self, TIM_HandleTypeDef* htim, uint32_t channel,
                                  const pwm_config_t* config)
{
    if (!self || !htim || !config) return HAL_ERROR;
    if (config->resolution == 0 || config->freq_hz == 0) return HAL_ERROR;

    uint32_t tim_clk = get_timer_clk(htim->Instance);
    uint32_t ticks   = tim_clk / config->freq_hz;   // 一个周期的总 tick 数

    if (ticks < config->resolution)
    {
        return HAL_ERROR;   // 分辨率太高，无法达到目标频率
    }

    uint32_t prescaler = (ticks / config->resolution) - 1;
    uint32_t period    = config->resolution - 1;
    uint32_t pulse     = config->init_duty > period ? period : config->init_duty;

    // 配置定时器
    htim->Instance               = (TIM_TypeDef*)htim->Instance;   // 保留原指针
    htim->Init.Prescaler         = prescaler;
    htim->Init.Period            = period;
    htim->Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim->Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    HAL_StatusTypeDef ret = HAL_TIM_PWM_Init(htim);
    if (ret != HAL_OK) return ret;

    // 配置通道
    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode             = TIM_OCMODE_PWM1;
    oc.Pulse              = pulse;
    oc.OCPolarity         = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode         = TIM_OCFAST_DISABLE;
    oc.OCIdleState        = TIM_OCIDLESTATE_RESET;
    oc.OCNIdleState       = TIM_OCNIDLESTATE_RESET;

    ret = HAL_TIM_PWM_ConfigChannel(htim, &oc, channel);
    if (ret != HAL_OK) return ret;

    // HAL_TIM_MspPostInit(htim);   // 配置 GPIO 复用

    // 保存状态
    self->htim       = htim;
    self->channel    = channel;
    self->resolution = config->resolution;

    return HAL_OK;
}

void pwm_driver_set_duty(pwm_driver_t* self, uint32_t duty)
{
    if (!self) return;
    uint32_t pulse = duty > (self->resolution - 1) ? (self->resolution - 1) : duty;
    __HAL_TIM_SET_COMPARE(self->htim, self->channel, pulse);
}
