// heat.c
#include "heat.h"
#include "pwm_driver.h"
#include "tim.h"

typedef struct {
    pwm_driver_t pwm;       // PWM驱动句柄
    uint8_t current_power;  // 当前功率百分比
    bool is_initialized;    // 初始化状态标记
} heat_dev_t;

static heat_dev_t s_heat_dev = {.current_power = 0, .is_initialized = false};

static bool heat_is_initialized(void)
{
    return s_heat_dev.is_initialized;
}

bool heat_init(void)
{
    // 防止重复初始化
    if (heat_is_initialized()) {
        return true;
    }

    // 配置PWM驱动参数
    s_heat_dev.pwm.htim = &htim1;
    s_heat_dev.pwm.channel = TIM_CHANNEL_4;
    s_heat_dev.pwm.resolution = (uint32_t)htim1.Init.Period + 1;  // 100（因为 Period=99）

    s_heat_dev.is_initialized = true;
    return true;
}

void heat_deinit(void)
{
    if (!heat_is_initialized()) {
        return;
    }

    // 先关闭加热再停止PWM
    heat_set_power(0);
    pwm_driver_stop(&s_heat_dev.pwm);

    // 重置设备状态
    s_heat_dev.is_initialized = false;
}

bool heat_start(void)
{
    return pwm_driver_start(&s_heat_dev.pwm) == HAL_OK;
}
bool heat_stop(void)
{
    return pwm_driver_stop(&s_heat_dev.pwm) == HAL_OK;
}

bool heat_set_power(uint8_t power_percent)
{
    // 检查初始化状态
    if (!heat_is_initialized()) {
        return false;
    }

    // 参数范围校验
    uint8_t power = (power_percent > HEAT_MAX_POWER_PERCENT) ? HEAT_MAX_POWER_PERCENT : power_percent;

    // 计算PWM占空比（使用uint32_t防止溢出）
    uint32_t duty = ((uint32_t)power * s_heat_dev.pwm.resolution) / HEAT_MAX_POWER_PERCENT;

    // 确保占空比不超过最大分辨率
    if (duty >= s_heat_dev.pwm.resolution) {
        duty = s_heat_dev.pwm.resolution - 1;
    }

    // 设置PWM占空比并更新当前功率
    if (pwm_driver_set_duty(&s_heat_dev.pwm, duty) == HAL_OK) {
        s_heat_dev.current_power = power;
        return true;
    }

    return false;
}

uint8_t heat_get_current_power(void)
{
    return heat_is_initialized() ? s_heat_dev.current_power : 0;
}
