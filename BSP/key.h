
#ifndef KEY_H
#define KEY_H

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
    KEY_ZHUMIAN = 1,
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
    KEY_POWER = 18
} KeyMode;
/*----------------------------------variable----------------------------------*/

/*-------------------------------------os-------------------------------------*/

/*----------------------------------function----------------------------------*/
unsigned char get_key(void);
/*------------------------------------test------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* KEY_H */
