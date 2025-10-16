
#ifndef AT_CTRL_H
#define AT_CTRL_H

#ifdef __cplusplus
extern "C"
{
#endif

/*----------------------------------include-----------------------------------*/
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*-----------------------------------macro------------------------------------*/

/*----------------------------------typedef-----------------------------------*/
typedef enum
{
    VOLUME_UP,
    VOLUME_DOWN,
} VOLUME_ENUM;

typedef enum
{
    BTMODE_OFF,
    BTMODE_BT,
    BTMODE_MUSIC
} BTMODE_ENUM;
/*----------------------------------variable----------------------------------*/

/*-------------------------------------os-------------------------------------*/

/*----------------------------------function----------------------------------*/
void bt_start(void);
void music_switch(void);
void music_next(void);
void music_prev(void);
void music_volume_control(VOLUME_ENUM ctrl);
void bt_mode(BTMODE_ENUM mode);
void music_volume_set(uint8_t volume);
/*------------------------------------test------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* AT_CTRL_H */
