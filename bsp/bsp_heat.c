/**
 * @file    bsp_heat.c
 * @brief   加热模块驱动实现（静态分配）
 * @version 2.1
 */

#include "bsp_heat.h"
#include "bsp_pwm.h"

struct bsp_heat_s {
    bsp_pwm_t* pwm;
    uint8_t current_power;
    bool initialized;
};

static struct bsp_heat_s s_inst;
static bool s_inited = false;

bsp_status_t bsp_heat_init(bsp_heat_t** handle, const bsp_heat_config_t* config) {
    if (!handle || !config || !config->htim) return BSP_ERR_PARAM;
    if (*handle || s_inited) return BSP_ERR_BUSY;

    bsp_pwm_config_t pwm_cfg = {
        .htim = config->htim,
        .channel = config->channel,
        .freq_hz = config->freq_hz,
        .resolution = config->resolution,
    };
    bsp_status_t ret = bsp_pwm_init(&s_inst.pwm, &pwm_cfg);
    if (ret != BSP_OK) return ret;

    s_inst.current_power = 0;
    s_inst.initialized = true;
    s_inited = true;
    *handle = (bsp_heat_t*)&s_inst;
    return BSP_OK;
}

void bsp_heat_deinit(bsp_heat_t** handle) {
    if (!handle || !*handle || !s_inited) return;
    bsp_pwm_set_duty(s_inst.pwm, 0);
    bsp_pwm_stop(s_inst.pwm);
    bsp_pwm_deinit(&s_inst.pwm);
    s_inst.initialized = false;
    s_inited = false;
    *handle = NULL;
}

bsp_status_t bsp_heat_start(bsp_heat_t* handle) {
    return (handle && s_inited) ? bsp_pwm_start(s_inst.pwm) : BSP_ERR_NOTINIT;
}

bsp_status_t bsp_heat_stop(bsp_heat_t* handle) {
    return (handle && s_inited) ? bsp_pwm_stop(s_inst.pwm) : BSP_ERR_NOTINIT;
}

bsp_status_t bsp_heat_set_power(bsp_heat_t* handle, uint8_t power_percent) {
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    uint8_t power = (power_percent > BSP_HEAT_MAX_POWER) ? BSP_HEAT_MAX_POWER : power_percent;
    uint32_t res = bsp_pwm_get_resolution(s_inst.pwm);
    if (!res) return BSP_ERROR;
    uint32_t duty = ((uint32_t)power * res) / BSP_HEAT_MAX_POWER;
    if (duty >= res) duty = res - 1;
    bsp_status_t ret = bsp_pwm_set_duty(s_inst.pwm, duty);
    if (ret == BSP_OK) s_inst.current_power = power;
    return ret;
}

uint8_t bsp_heat_get_power(bsp_heat_t* handle) {
    return (handle && s_inited) ? s_inst.current_power : 0;
}
