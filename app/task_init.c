/**
 * @file task_init.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief
 * @version 0.1
 * @date 2025-11-06
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "BufferProcess.h"
#include "buzzer.h"
#include "heat_task.h"
#include "key_task.h"
#include "led.h"
#include "protocal.h"

void task_init(void)
{
    led_init();              // 初始化LED控制器
    buzzer_init();           // 初始化蜂鸣器
    key_task_init();         // 初始化按键任务
    heat_task_init();        // 初始化加热任务
    buffer_process_init();   // 初始化缓冲处理任务
}

void do_reg_change_actions(RegisterID reg, uint16_t value)
{
    switch (reg)
    {
        case REG_HEATING_STATUS:
            if (value == 0)
            {
                heat_status_set(HEAT_STOP);
            } else
            {
                heat_status_set(HEAT_RUNNING);
            }
            break;
        case REG_HEATING_LEVEL: heat_level_set((HeatLevel)value); break;
        case REG_HEATING_TIMER:
            heat_timer_set(value);   // 设置定时（分钟）
            break;
        case REG_ALARM_SET_HIGH:
        case REG_ALARM_SET_LOW:
        case REG_DELETE_ALARM:
            // 处理闹钟相关寄存器写入
            // alarm_handle_modbus_write(reg, value);
            break;
        default:
            // 其他寄存器无需特殊处理
            break;
    }
}
