/**
 * @file    bsp_led.c
 * @brief   LED 驱动实现（静态分配）
 * @version 2.1
 */

#include "bsp_led.h"

typedef struct {
    bsp_led_hw_t hw;
    bsp_led_mode_t mode;
    uint32_t interval_ms;
    uint32_t elapsed;
    bool state;
} led_ctrl_t;

struct bsp_led_s {
    led_ctrl_t leds[BSP_LED_COUNT];
    bool initialized;
};

static struct bsp_led_s s_inst;
static bool s_inited = false;

static inline GPIO_PinState led_active_pin(uint8_t i, bool on) {
    return on ? s_inst.leds[i].hw.active_level :
                (s_inst.leds[i].hw.active_level == GPIO_PIN_SET ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void led_hw_write(uint8_t i, bool on) {
    HAL_GPIO_WritePin(s_inst.leds[i].hw.port, s_inst.leds[i].hw.pin, led_active_pin(i, on));
    s_inst.leds[i].state = on;
}

bsp_status_t bsp_led_init(bsp_led_t** handle, const bsp_led_config_t* config) {
    if (!handle || !config || !config->hw_table || config->count == 0) return BSP_ERR_PARAM;
    if (*handle || s_inited) return BSP_ERR_BUSY;

    for (uint8_t i = 0; i < config->count && i < BSP_LED_COUNT; i++) {
        s_inst.leds[i].hw = config->hw_table[i];
        s_inst.leds[i].mode = BSP_LED_MODE_OFF;
        s_inst.leds[i].interval_ms = 500;
        s_inst.leds[i].elapsed = 0;
        led_hw_write(i, false);
    }

    s_inst.initialized = true;
    s_inited = true;
    *handle = (bsp_led_t*)&s_inst;
    return BSP_OK;
}

void bsp_led_deinit(bsp_led_t** handle) {
    if (!handle || !*handle || !s_inited) return;
    for (uint8_t i = 0; i < BSP_LED_COUNT; i++) led_hw_write(i, false);
    s_inst.initialized = false;
    s_inited = false;
    *handle = NULL;
}

bsp_status_t bsp_led_set_mode(bsp_led_t* handle, uint8_t index, bsp_led_mode_t mode, uint32_t interval_ms) {
    if (!handle || !s_inited || index >= BSP_LED_COUNT) return BSP_ERR_NOTINIT;

    s_inst.leds[index].mode = mode;
    s_inst.leds[index].elapsed = 0;

    switch (mode) {
    case BSP_LED_MODE_ON: led_hw_write(index, true); break;
    case BSP_LED_MODE_BLINK:
        s_inst.leds[index].interval_ms = (interval_ms > 0) ? interval_ms : 500;
        led_hw_write(index, true);
        break;
    default: led_hw_write(index, false); break;
    }
    return BSP_OK;
}

bool bsp_led_is_on(bsp_led_t* handle, uint8_t index) {
    return (handle && s_inited && index < BSP_LED_COUNT) ? s_inst.leds[index].state : false;
}

void bsp_led_poll(bsp_led_t* handle, uint32_t elapsed_ms) {
    if (!handle || !s_inited || !elapsed_ms) return;

    for (uint8_t i = 0; i < BSP_LED_COUNT; i++) {
        if (s_inst.leds[i].mode != BSP_LED_MODE_BLINK) continue;
        s_inst.leds[i].elapsed += elapsed_ms;
        if (s_inst.leds[i].elapsed >= s_inst.leds[i].interval_ms) {
            s_inst.leds[i].elapsed -= s_inst.leds[i].interval_ms;
            led_hw_write(i, !s_inst.leds[i].state);
        }
    }
}
