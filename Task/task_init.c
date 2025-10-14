#include "task_init.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "heat_task.h"
#include "key_task.h"
#include "protocal_task.h"

void task_init(void)
{
    // 创建按键扫描任务
    xTaskCreate(key_scan, "KeyScan", 128 * 2, NULL, 2, NULL);

    // 启动调度器
    vTaskStartScheduler();
}

void do_reg_change_actions(RegisterID reg, uint16_t value)
{
    switch (reg)
    {
        case REG_HEATING_STATUS:
            if (value == 0) { heat_set_status(HEAT_STOP); }
            else { heat_set_status(HEAT_RUNNING); }
            break;
        case REG_HEATING_LEVEL:
            heat_set_level((HeatLevel) (value - 1)); // 假设寄存器值1-5对应档位0-4
            break;
        case REG_HEATING_TIMER:
            heat_set_timer(value); // 设置定时（分钟）
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
