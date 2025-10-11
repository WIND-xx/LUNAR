
#ifndef KEY_TASK_H
#define KEY_TASK_H

#ifdef __cplusplus
extern "C"
{
#endif

/*----------------------------------include-----------------------------------*/
#include "main.h"
/*-----------------------------------macro------------------------------------*/

/*----------------------------------typedef-----------------------------------*/
typedef enum
{
    KEY_NONE = 0,
    KEY_SHORT_PRESS,
    KEY_LONG_PRESS,
} key_event_t;
/*----------------------------------variable----------------------------------*/

/*-------------------------------------os-------------------------------------*/

/*----------------------------------function----------------------------------*/
void key_scan(void *arg);
void key_event_handler(uint8_t key_id, key_event_t event);
/*------------------------------------test------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* KEY_TASK_H */
