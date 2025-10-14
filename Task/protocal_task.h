
#ifndef PROTOCAL_TASK_H
#define PROTOCAL_TASK_H

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
// 寄存器定义（明确读写属性）
typedef enum
{
    REG_POWER_SWITCH = 0,   // 关机（只写）
    REG_UTC_TIMESTAMP_HIGH, // UTC时间戳（高位，只写）
    REG_UTC_TIMESTAMP_LOW,  // UTC时间戳（低位，只写）
    REG_ALARM_SET_HIGH,     // 闹钟（高位，只写）
    REG_ALARM_SET_LOW,      // 闹钟（低位，只写）
    REG_DELETE_ALARM,       // 删除闹钟（只写）
    REG_EXECUTE_SHORTCUT,   // 执行快捷键（只写）
    REG_HEATING_STATUS,     // 热敷工作状态（读写）
    REG_HEATING_LEVEL,      // 热敷档位（读写，1-5档）
    REG_HEATING_TIMER,      // 热敷定时（读写，0-120分钟）
    REG_SHORTCUT_KEY1,      // 快捷键1配置（读写）
    REG_SHORTCUT_KEY2,      // 快捷键2配置（读写）
    REG_COUNT,
} RegisterID;
/*----------------------------------variable----------------------------------*/

/*-------------------------------------os-------------------------------------*/

/*----------------------------------function----------------------------------*/

/*------------------------------------test------------------------------------*/

#ifdef __cplusplus
}
#endif

#endif /* PROTOCAL_TASK_H */
