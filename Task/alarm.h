#ifndef __ALARM_H
#define __ALARM_H

#include "FreeRTOS.h"
#include "semphr.h"
#include <stdbool.h>
#include <stdint.h>

#include "protocal_task.h"
// 闹钟状态枚举（平台无关）
typedef enum
{
    ALARM_OK = 0,
    ALARM_ERR_INVALID_PARAM,
    ALARM_ERR_ID_OUT_OF_RANGE,
    ALARM_ERR_LOCK_FAILED,
    ALARM_ERR_TIME_INVALID
} AlarmResult;

// 闹钟重复模式
typedef enum
{
    ALARM_ONCE = 0,  // 仅一次
    ALARM_REPEAT = 1 // 重复
} AlarmRepeatMode;

// 星期掩码（对应低位寄存器2-8位）
typedef enum
{
    ALARM_WEEKDAY_SUN = (1 << 0), // 星期日 (bit2)
    ALARM_WEEKDAY_MON = (1 << 1), // 星期一 (bit3)
    ALARM_WEEKDAY_TUE = (1 << 2), // 星期二 (bit4)
    ALARM_WEEKDAY_WED = (1 << 3), // 星期三 (bit5)
    ALARM_WEEKDAY_THU = (1 << 4), // 星期四 (bit6)
    ALARM_WEEKDAY_FRI = (1 << 5), // 星期五 (bit7)
    ALARM_WEEKDAY_SAT = (1 << 6)  // 星期六 (bit8)
} AlarmWeekdayMask;

// 闹钟结构体
typedef struct
{
    uint8_t         id;           // 闹钟ID (0-31)
    bool            enabled;      // 是否启用
    uint8_t         hour;         // 小时 (0-23)
    uint8_t         minute;       // 分钟 (0-59)
    AlarmRepeatMode repeat_mode;  // 重复模式
    uint8_t         weekday_mask; // 星期掩码 (ALARM_WEEKDAY_xxx组合)
    uint8_t         ringtone_id;  // 铃声ID (0-127)
    bool            triggered;    // 是否已触发（单次闹钟用）
} Alarm;

// 闹钟管理器
typedef struct
{
    Alarm             alarms[32]; // 支持32个闹钟 (ID 0-31)
    SemaphoreHandle_t mutex;      // 保护闹钟数据的互斥锁
} AlarmManager;

// 外部接口声明
void        alarm_manager_init(AlarmManager *manager);
AlarmResult alarm_parse_and_save(AlarmManager *manager, uint16_t high_reg, uint16_t low_reg);
AlarmResult alarm_delete(AlarmManager *manager, uint8_t alarm_id);
void        alarm_check_task(void *params);
bool        alarm_is_triggered(Alarm *alarm, uint8_t current_hour, uint8_t current_minute, uint8_t current_weekday);
AlarmResult alarm_handle_modbus_write(RegisterID reg, uint16_t value);

#endif /* __ALARM_H */
