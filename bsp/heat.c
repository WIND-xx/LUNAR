// heat.c
#include "heat.h"
#include "pwm_driver.h"
#include "tim.h"


static pwm_driver_t s_heat_pwm;

void heat_init(void)
{
    s_heat_pwm.htim       = &htim1;
    s_heat_pwm.channel    = TIM_CHANNEL_4;
    s_heat_pwm.resolution = htim1.Init.Period + 1;   // 100（因为 Period=99）

    pwm_driver_start(&s_heat_pwm);
    heat_off();   // 初始关闭
}

void heat_deinit(void)
{
    pwm_driver_stop(&s_heat_pwm);
}

void heat_on(uint16_t power)
{
    if (power > 100) power = 100;

    // 整数运算：duty = (power * resolution) / 100
    uint32_t duty = (uint32_t)power * s_heat_pwm.resolution / 100U;
    if (duty >= s_heat_pwm.resolution)
    {
        duty = s_heat_pwm.resolution - 1;
    }

    pwm_driver_set_duty(&s_heat_pwm, duty);
}

void heat_off(void)
{
    pwm_driver_set_duty(&s_heat_pwm, 0);
}
