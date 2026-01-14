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

#include "buzzer.h"
#include "frame_process.h"
#include "heat_task.h"
#include "key_task.h"
#include "led.h"
#include "protocol.h"

void task_init(void)
{
    led_init();                    // 初始化LED控制器
    buzzer_init();                 // 初始化蜂鸣器
    key_task_init();               // 初始化按键任务
    heat_task_init();              // 初始化加热任务
    ble_data_process_task_start(); // 初始化缓冲处理任务
    protocol_init();               // 初始化协议回调
}
static void my_register_write_callback(RegisterID reg, uint16_t value)
{
    switch (reg) {
    case REG_HEATING_STATUS:
        // 控制加热开关
        if (value == 0) {
            heat_status_set(HEAT_STOP);
        } else {
            heat_status_set(HEAT_RUNNING);
        }
        break;

    case REG_HEATING_LEVEL:
        // 设置加热档位
        heat_level_set((HeatLevel)value);
        break;

    case REG_HEATING_TIMER:
        // 设置加热定时
        heat_timer_set(value);
        break;

    case REG_POWER_SWITCH:
        // 关机命令
        if (value == 1) {
            // system_power_off();
        }
        break;

    case REG_EXECUTE_SHORTCUT:
        // 执行快捷键
        // execute_shortcut(value);
        break;

    // ... 处理其他寄存器
    default:
        break;
    }
}

// 2. 定义读回调函数（如果需要从硬件读取实时数据）
static uint16_t my_register_read_callback(RegisterID reg)
{
    uint16_t value = 0xFFFF;

    switch (reg) {
    case REG_HEATING_STATUS:
        // 从硬件读取加热状态
        value = heat_status_get();
        break;

    case REG_HEATING_LEVEL:
        // 读取当前加热档位
        value = heat_level_get();
        break;

    case REG_HEATING_TIMER:
        // 读取剩余加热时间
        value = heat_timer_get();
        break;

    default:
        // 对于其他寄存器，返回寄存器存储的值
        value = register_read(reg);
        break;
    }

    return value;
}

void protocol_init(void)
{
    // 注册写回调（必须）
    protocol_register_write_callback(my_register_write_callback);

    // 注册读回调（可选，如果不需要实时读取硬件数据可以不注册）
    protocol_register_read_callback(my_register_read_callback);
}
