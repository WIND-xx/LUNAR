#include "alarm.h"
#include "heat_task.h" // 用于触发加热动作
#include "rtc.h"
#include "task.h"

// 全局闹钟管理器实例
static AlarmManager alarm_manager;

// 初始化闹钟管理器
void alarm_manager_init(AlarmManager *manager)
{
    if (manager == NULL) return;

    // 创建互斥锁
    manager->mutex = xSemaphoreCreateMutex();

    // 初始化所有闹钟
    for (uint8_t i = 0; i < 32; i++)
    {
        manager->alarms[i].id = i;
        manager->alarms[i].enabled = false;
        manager->alarms[i].triggered = false;
    }
}

// 解析Modbus寄存器数据并保存闹钟
AlarmResult alarm_parse_and_save(AlarmManager *manager, uint16_t high_reg, uint16_t low_reg)
{
    if (manager == NULL) return ALARM_ERR_INVALID_PARAM;

    // 解析高位寄存器 (0x0003)
    uint8_t alarm_id = (high_reg >> 11) & 0x1F; // 11-15位: 闹钟ID (0-31)
    uint8_t hour = (high_reg >> 6) & 0x1F;      // 6-10位: 小时 (0-23)
    uint8_t minute = high_reg & 0x3F;           // 0-5位: 分钟 (0-59)

    // 验证时间有效性
    if (hour > 23 || minute > 59) { return ALARM_ERR_TIME_INVALID; }

    // 解析低位寄存器 (0x0004)
    bool    enabled = (low_reg >> 0) & 0x01;      // 0位: 是否启用
    uint8_t repeat_mode = (low_reg >> 1) & 0x01;  // 1位: 重复模式
    uint8_t weekday_mask = (low_reg >> 2) & 0x7F; // 2-8位: 星期标志位
    uint8_t ringtone_id = (low_reg >> 9) & 0x7F;  // 9-15位: 铃声ID

    // 检查ID范围
    if (alarm_id >= 32) { return ALARM_ERR_ID_OUT_OF_RANGE; }

    // 加锁保护
    if (xSemaphoreTake(manager->mutex, portMAX_DELAY) != pdTRUE) { return ALARM_ERR_LOCK_FAILED; }

    // 更新闹钟数据
    Alarm *alarm = &manager->alarms[alarm_id];
    alarm->enabled = enabled;
    alarm->hour = hour;
    alarm->minute = minute;
    alarm->repeat_mode = repeat_mode;
    alarm->weekday_mask = weekday_mask;
    alarm->ringtone_id = ringtone_id;
    alarm->triggered = false; // 重置触发状态

    // 释放锁
    xSemaphoreGive(manager->mutex);
    return ALARM_OK;
}

// 删除指定ID的闹钟
AlarmResult alarm_delete(AlarmManager *manager, uint8_t alarm_id)
{
    if (manager == NULL || alarm_id >= 32) { return ALARM_ERR_INVALID_PARAM; }

    if (xSemaphoreTake(manager->mutex, portMAX_DELAY) != pdTRUE) { return ALARM_ERR_LOCK_FAILED; }

    // 禁用闹钟即可视为删除
    manager->alarms[alarm_id].enabled = false;
    manager->alarms[alarm_id].triggered = false;

    xSemaphoreGive(manager->mutex);
    return ALARM_OK;
}

// 检查闹钟是否应该触发
bool alarm_is_triggered(Alarm *alarm, uint8_t current_hour, uint8_t current_minute, uint8_t current_weekday)
{
    // 闹钟未启用，不触发
    if (!alarm->enabled) return false;

    // 时间不匹配，不触发
    if (alarm->hour != current_hour || alarm->minute != current_minute) { return false; }

    // 检查星期是否匹配 (current_weekday: 0=星期日, 6=星期六)
    uint8_t weekday_bit = 1 << current_weekday;
    if (!(alarm->weekday_mask & weekday_bit)) { return false; }

    // 处理单次闹钟
    if (alarm->repeat_mode == ALARM_ONCE)
    {
        // 已经触发过，不再触发
        if (alarm->triggered) return false;
        // 标记为已触发
        alarm->triggered = true;
    }

    return true;
}

// 闹钟触发时的动作处理（可根据需要扩展）
static void alarm_execute_action(Alarm *alarm)
{
    // 示例：触发加热功能
    heat_set_status(HEAT_RUNNING);

    // 可根据闹钟ID设置不同档位
    if (alarm->id == 0)
    {
        heat_set_level(1); // 档位1
    }
    else if (alarm->id == 1)
    {
        heat_set_level(3); // 档位3
    }

    // 可添加其他动作：如播放铃声（根据alarm->ringtone_id）
}

// 闹钟检查任务
void alarm_check_task(void *params)
{
    (void) params;
    RTC_DateTimeTypeDef current_time;
    uint8_t             last_minute = 0xFF; // 初始值设为无效值

    for (;;)
    {
        // 获取当前时间
        RTC_GetDateTime(&current_time);

        // 每分钟检查一次（仅当分钟变化时）
        if (current_time.minute != last_minute)
        {
            last_minute = current_time.minute;

            // 加锁检查所有闹钟
            if (xSemaphoreTake(alarm_manager.mutex, portMAX_DELAY) == pdTRUE)
            {
                for (uint8_t i = 0; i < 32; i++)
                {
                    Alarm *alarm = &alarm_manager.alarms[i];
                    if (alarm_is_triggered(alarm, current_time.hour, current_time.minute, current_time.weekday))
                    {
                        alarm_execute_action(alarm);
                    }
                }
                xSemaphoreGive(alarm_manager.mutex);
            }
        }

        // 10秒检查一次时间（平衡精度和资源占用）
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// 初始化并启动闹钟系统
void alarm_system_init(void)
{
    alarm_manager_init(&alarm_manager);

    // 创建闹钟检查任务
    xTaskCreate(alarm_check_task, "alarm_check",
                256, // 堆栈大小
                NULL,
                2, // 任务优先级
                NULL);
}

// 供Modbus处理函数调用的接口
AlarmResult alarm_handle_modbus_write(RegisterID reg, uint16_t value)
{
    static uint16_t alarm_high_reg_cache = 0; // 缓存高位寄存器值

    switch (reg)
    {
        case REG_ALARM_SET_HIGH:
            // 缓存高位寄存器值，等待低位寄存器写入
            alarm_high_reg_cache = value;
            return ALARM_OK;

        case REG_ALARM_SET_LOW:
            // 高低位都已收到，解析并保存闹钟
            return alarm_parse_and_save(&alarm_manager, alarm_high_reg_cache, value);

        case REG_DELETE_ALARM:
            // 删除指定ID的闹钟
            return alarm_delete(&alarm_manager, (uint8_t) value);

        default:
            return ALARM_ERR_INVALID_PARAM;
    }
}
