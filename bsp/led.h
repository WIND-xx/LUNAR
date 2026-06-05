#ifndef __LED_H
#define __LED_H

#include "gpio.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_MUSIC,
    LED_BT,
    LED_10MIN,
    LED_30MIN,
    LED_60MIN,
    LED_RF,
    LED_B,
    LED_COUNT
} LED_Index;

typedef enum {
    LED_MODE_OFF,
    LED_MODE_ON,
    LED_MODE_BLINK
} led_mode_t;

void led_init(void);
void led_set_mode(LED_Index idx, led_mode_t mode, uint32_t interval_ms);
bool led_get(LED_Index idx);
void led_time_select(uint16_t seconds);

#ifdef __cplusplus
}
#endif

#endif
