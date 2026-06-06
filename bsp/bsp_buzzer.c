/**
 * @file    bsp_buzzer.c
 * @brief   蜂鸣器驱动实现（静态分配）
 * @version 2.1
 */

#include "bsp_buzzer.h"

struct bsp_buzzer_s {
    GPIO_TypeDef* port;
    uint16_t pin;
    GPIO_PinState active_level;
    bool is_on;
    bool initialized;
};

static struct bsp_buzzer_s s_inst;
static bool s_inited = false;

static inline GPIO_PinState buzzer_pin_state(bool on) {
    return on ? s_inst.active_level : (s_inst.active_level == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

bsp_status_t bsp_buzzer_init(bsp_buzzer_t** handle, const bsp_buzzer_config_t* config) {
    if (!handle || !config || !config->port) return BSP_ERR_PARAM;
    if (*handle || s_inited) return BSP_ERR_BUSY;

    s_inst.port = config->port;
    s_inst.pin = config->pin;
    s_inst.active_level = config->active_level;
    s_inst.is_on = false;
    s_inst.initialized = true;
    s_inited = true;

    HAL_GPIO_WritePin(s_inst.port, s_inst.pin, buzzer_pin_state(false));
    *handle = (bsp_buzzer_t*)&s_inst;
    return BSP_OK;
}

void bsp_buzzer_deinit(bsp_buzzer_t** handle) {
    if (!handle || !*handle || !s_inited) return;
    HAL_GPIO_WritePin(s_inst.port, s_inst.pin, buzzer_pin_state(false));
    s_inst.initialized = false;
    s_inited = false;
    *handle = NULL;
}

bsp_status_t bsp_buzzer_on(bsp_buzzer_t* handle) {
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    HAL_GPIO_WritePin(s_inst.port, s_inst.pin, buzzer_pin_state(true));
    s_inst.is_on = true;
    return BSP_OK;
}

bsp_status_t bsp_buzzer_off(bsp_buzzer_t* handle) {
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    HAL_GPIO_WritePin(s_inst.port, s_inst.pin, buzzer_pin_state(false));
    s_inst.is_on = false;
    return BSP_OK;
}

bsp_status_t bsp_buzzer_toggle(bsp_buzzer_t* handle) {
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    HAL_GPIO_TogglePin(s_inst.port, s_inst.pin);
    s_inst.is_on = !s_inst.is_on;
    return BSP_OK;
}

bool bsp_buzzer_is_on(bsp_buzzer_t* handle) {
    return (handle && s_inited) ? s_inst.is_on : false;
}
