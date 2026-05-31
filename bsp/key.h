
#ifndef __KEY_H
#define __KEY_H

#ifdef __cplusplus
extern "C"
{
#endif

/*----------------------------------include-----------------------------------*/
#include "gpio.h"
#include "main.h"
/*-----------------------------------macro------------------------------------*/

/*----------------------------------typedef-----------------------------------*/
typedef enum
{
    KEY_NULL = 0,
    KEY_MUSIC = 1,
    KEY_BLUETOOTH = 2,
    KEY_PLAY_PAUSE = 3,
    KEY_MIN10 = 4,
    KEY_MIN60 = 5,
    KEY_PREV = 6,
    KEY_NEXT = 7,
    KEY_VOL_DOWN = 8,
    KEY_VOL_UP = 9,
    KEY_HEAT_PLUS = 10,
    KEY_HEAT_MINUS = 11,
    KEY_SHORTCUT_1 = 12,
    KEY_SHORTCUT_2 = 13,
    KEY_MIN30 = 14,
    KEY_HEAT = 15,
    // 16, 17 保留
    KEY_POWER = 18
} KeyMode;
/*----------------------------------variable----------------------------------*/

/*-------------------------------------os-------------------------------------*/

/*----------------------------------function----------------------------------*/
unsigned char get_key(void);
void key_init(void);      // GPIO初始化（仅调用一次）
/*------------------------------------test------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H */
