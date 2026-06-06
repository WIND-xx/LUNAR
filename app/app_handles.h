/**
 * @file    app_handles.h
 * @brief   全局 BSP 句柄声明（各 service 共享）
 * @version 1.0
 */

#ifndef APP_HANDLES_H
#define APP_HANDLES_H

#include "bsp_bt401.h"
#include "bsp_buzzer.h"
#include "bsp_heat.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_ntc.h"
#include "bsp_rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 按键值定义（矩阵键盘返回的原始键值）
 *============================================================================*/
typedef enum {
    KEY_NULL       = 0,
    KEY_MUSIC      = 1,
    KEY_BLUETOOTH  = 2,
    KEY_PLAY_PAUSE = 3,
    KEY_MIN10      = 4,
    KEY_MIN60      = 5,
    KEY_PREV       = 6,
    KEY_NEXT       = 7,
    KEY_VOL_DOWN   = 8,
    KEY_VOL_UP     = 9,
    KEY_HEAT_PLUS  = 10,
    KEY_HEAT_MINUS = 11,
    KEY_SHORTCUT_1 = 12,
    KEY_SHORTCUT_2 = 13,
    KEY_MIN30      = 14,
    KEY_HEAT       = 15,
    KEY_POWER      = 18
} app_key_t;

/*==============================================================================
 * 全局 BSP 句柄（在 app_main.c 中定义并初始化）
 *============================================================================*/
extern bsp_bt401_t* g_bt401;
extern bsp_buzzer_t* g_buzzer;
extern bsp_heat_t* g_heat;
extern bsp_key_t* g_key;
extern bsp_led_t* g_led;
extern bsp_ntc_t* g_ntc;
extern bsp_rtc_t* g_rtc;

#ifdef __cplusplus
}
#endif

#endif /* APP_HANDLES_H */
