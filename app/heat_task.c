/**
 * @file heat_task.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief
 * @version 0.2
 * @date 2025-11-30
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "heat_task.h"
#include "bt401.h"
#include "buzzer.h"
#include "heat.h"
#include "led.h"
#include "ntc.h"
#include "pid.h"

#include "FreeRTOS.h"
#include "protocal.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"
#include <stdint.h>

Heat_t heat = {.status = HEAT_STOP,
               .target_temperature = 35.0f,
               .set_time = 0,
               .remain_sec = 0,
               .level = HEAT_LEVEL_1,
               .is_timing = false};

static TimerHandle_t xHeatingTimer = NULL;
static TimerHandle_t xRemainTimer = NULL;
static SemaphoreHandle_t xHeatMutex = NULL;
static QueueHandle_t xHeatCtrlQueue = NULL;

typedef enum {
    MSG_TIMER_EXPIRE = 0x01,
    MSG_UPDATE_REMAIN = 0x02,
    MSG_SET_STATUS = 0x03,
    MSG_SET_LEVEL = 0x04,
    MSG_SET_TIMER = 0x05
} HeatMsgType;

typedef struct {
    HeatMsgType type;
    union {
        HeatStatus status;
        HeatLevel level;
        uint16_t minute;
    } param;
} HeatMsg;

#define LOCK() (xSemaphoreTake(xHeatMutex, pdMS_TO_TICKS(50)) == pdTRUE)
#define UNLOCK() xSemaphoreGive(xHeatMutex)

static void heat_hw_sync_off(void)
{
    heat_off();
    led_set_mode(LED_RF, LED_MODE_OFF, 0);
}

static void heat_hw_sync_on(void)
{
    led_set_mode(LED_RF, LED_MODE_ON, 0);
}

static void heat_stop_all(void)
{
    HeatStatus old_status = HEAT_STOP;

    if (LOCK()) {
        old_status = heat.status;
        heat.status = HEAT_STOP;
        heat.remain_sec = 0;
        heat.set_time = 0;
        heat.is_timing = false;
        UNLOCK();
    }

    if (old_status == HEAT_RUNNING) {
        heat_hw_sync_off();
        led_time_select(0);
        xTimerStop(xRemainTimer, 0);
        xTimerStop(xHeatingTimer, 0);
        protocal_uplode_heat();
    }
}

static void heating_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    HeatMsg msg = {.type = MSG_TIMER_EXPIRE};
    xQueueSend(xHeatCtrlQueue, &msg, 0);
}

static void remain_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    HeatMsg msg = {.type = MSG_UPDATE_REMAIN};
    xQueueSend(xHeatCtrlQueue, &msg, 0);
}

static void process_heat_message(HeatMsg* msg)
{
    switch (msg->type) {
    case MSG_TIMER_EXPIRE: {
        HeatStatus old_status = HEAT_STOP;
        uint16_t old_set_time = 0;

        if (LOCK()) {
            old_status = heat.status;
            old_set_time = heat.set_time;
            heat.status = HEAT_STOP;
            heat.remain_sec = 0;
            heat.set_time = 0;
            heat.is_timing = false;
            UNLOCK();
        }

        if (old_status == HEAT_RUNNING && old_set_time > 0) {
            heat_hw_sync_off();
            led_time_select(0);
            xTimerStop(xRemainTimer, 0);
            xTimerStop(xHeatingTimer, 0);
            protocal_uplode_heat();
        }
        break;
    }

    case MSG_UPDATE_REMAIN: {
        uint32_t remain = 0;
        HeatStatus status = HEAT_STOP;

        if (LOCK()) {
            status = heat.status;
            if (status == HEAT_RUNNING && heat.remain_sec > 0) {
                heat.remain_sec--;
                remain = heat.remain_sec;
            }
            UNLOCK();
        }

        if (status == HEAT_RUNNING && remain > 0) {
            led_time_select(remain);
        } else if (status == HEAT_RUNNING && remain == 0) {
            heat_stop_all();
        }
        break;
    }

    case MSG_SET_STATUS: {
        HeatStatus new_status = msg->param.status;
        HeatStatus old_status = HEAT_STOP;
        uint16_t set_time = 0;

        if (LOCK()) {
            old_status = heat.status;
            set_time = heat.set_time;
            heat.status = new_status;
            UNLOCK();
        }

        if (new_status == HEAT_STOP) {
            heat_stop_all();
        } else if (new_status == HEAT_RUNNING && old_status != HEAT_RUNNING) {
            heat_hw_sync_on();
            if (set_time > 0) {
                if (LOCK()) {
                    heat.remain_sec = (uint32_t)set_time * 60;
                    heat.is_timing = true;
                    UNLOCK();
                }
                xTimerChangePeriod(xHeatingTimer, pdMS_TO_TICKS(heat.remain_sec * 1000), 0);
                xTimerStart(xHeatingTimer, 0);
                if (!xTimerIsTimerActive(xRemainTimer)) {
                    xTimerStart(xRemainTimer, 0);
                }
            }
            protocal_uplode_heat();
        }
        break;
    }

    case MSG_SET_LEVEL: {
        if (msg->param.level > HEAT_LEVEL_3)
            break;

        float new_target = 35.0f;

        switch (msg->param.level) {
        case HEAT_LEVEL_1:
            new_target = 35.0f;
            break;
        case HEAT_LEVEL_2:
            new_target = 45.0f;
            break;
        case HEAT_LEVEL_3:
            new_target = 55.0f;
            break;
        }

        if (LOCK()) {
            heat.level = msg->param.level;
            heat.target_temperature = new_target;
            UNLOCK();
        }
        protocal_uplode_heat();
        break;
    }

    case MSG_SET_TIMER: {
        if (msg->param.minute > 720)
            break;

        HeatStatus status = HEAT_STOP;

        if (LOCK()) {
            status = heat.status;
            heat.set_time = msg->param.minute;
            heat.is_timing = (msg->param.minute > 0);
            if (heat.is_timing) {
                heat.remain_sec = (uint32_t)heat.set_time * 60;
            } else {
                heat.remain_sec = 0;
            }
            UNLOCK();
        }

        led_time_select(heat.remain_sec);

        if (heat.is_timing && status == HEAT_RUNNING) {
            xTimerStop(xHeatingTimer, 0);
            xTimerChangePeriod(xHeatingTimer, pdMS_TO_TICKS(heat.remain_sec * 1000), 0);
            xTimerStart(xHeatingTimer, 0);
            if (!xTimerIsTimerActive(xRemainTimer)) {
                xTimerStart(xRemainTimer, 0);
            }
        } else {
            xTimerStop(xHeatingTimer, 0);
            xTimerStop(xRemainTimer, 0);
        }
        protocal_uplode_heat();
        break;
    }

    default:
        break;
    }
}

void heat_control_task(void* arg)
{
    (void)arg;

    TickType_t xLastWakeTime = xTaskGetTickCount();

    // PID 控制器实例
    pid_controller_t heater_pid;
    uint8_t ntc_fail_count = 0;
    TickType_t control_period = pdMS_TO_TICKS(500); // 固定 500ms

    pid_init(&heater_pid,
             20.0f, // Kp
             3.0f,  // Ki
             1.0f,  // Kd
             1.0f,  // 死区（1°C总宽，即±0.5°C内不调节）
             0,     // 输出下限
             99);   // 输出上限

    // 积分参数：小死区，合理限幅
    pid_set_integral_params(&heater_pid, 0.5f, 30.0f);
    pid_set_derivative_filter(&heater_pid, 0.1f); // 滤波稍弱，让 D 起作用
    // 初始化硬件
    heat_init();

    for (;;) {
        // 处理控制消息（非阻塞）
        HeatMsg msg;
        while (xQueueReceive(xHeatCtrlQueue, &msg, 0) == pdTRUE) {
            process_heat_message(&msg);
        }

        // 安全读取当前状态和目标温度
        HeatStatus status;
        float target_temp;

        if (LOCK()) {
            status = heat.status;
            target_temp = heat.target_temperature;
            UNLOCK();
        } else {
            // 锁失败时保守处理：停止加热
            status = HEAT_STOP;
            target_temp = 35.0f; // 默认值，实际不会使用
        }

        if (status == HEAT_RUNNING) {
            heat_hw_sync_on(); // 启用同步信号（如需要）

            float curr_temp = 0.0f;
            if (ntc_read(&curr_temp) == 0) {
                ntc_fail_count = 0;

                uint32_t current_time_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
                uint16_t pid_out = pid_calc_with_time(&heater_pid,
                                                      curr_temp,   // 实际测量温度（浮点）
                                                      target_temp, // 目标温度（浮点）
                                                      current_time_ms);

                heat_on(pid_out); // 输出 PWM 占空比（0~1000）
                int16_t temp_scaled = (int16_t)(curr_temp * 10.0f + 0.5f);
                int16_t target_scaled = (int16_t)(target_temp * 10.0f + 0.5f);

                bt401_printf("Temp: %d.%d°C, Target: %d.%d°C, Output: %u\n", temp_scaled / 10, temp_scaled % 10,
                             target_scaled / 10, target_scaled % 10, pid_out);
            } else {
                // NTC 读取失败
                if (++ntc_fail_count > 3) {
                    heat_stop_all();
                    ntc_fail_count = 0;
                    pid_reset(&heater_pid); // 重置积分等状态
                }
            }
        } else {
            // 非运行状态：关闭硬件，重置 PID
            heat_hw_sync_off();
            pid_reset(&heater_pid);
            ntc_fail_count = 0;
        }

        // 按照固定 500ms 周期休眠
        vTaskDelayUntil(&xLastWakeTime, control_period);
    }
}

void heat_task_init(void)
{
    xHeatMutex = xSemaphoreCreateMutex();
    xHeatCtrlQueue = xQueueCreate(5, sizeof(HeatMsg));
    xHeatingTimer = xTimerCreate("HeatTimer", pdMS_TO_TICKS(1000), pdFALSE, NULL, heating_timer_callback);
    xRemainTimer = xTimerCreate("RemainTimer", pdMS_TO_TICKS(1000), pdTRUE, NULL, remain_timer_callback);

    configASSERT(xHeatMutex && xHeatCtrlQueue && xHeatingTimer && xRemainTimer);
    xTaskCreate(heat_control_task, "heat_task", 512, NULL, 3, NULL);
}

bool heat_status_set(HeatStatus status)
{
    if (!xHeatCtrlQueue)
        return false;
    HeatMsg msg = {.type = MSG_SET_STATUS, .param.status = status};
    return xQueueSend(xHeatCtrlQueue, &msg, 0) == pdPASS;
}
void heat_status_switch(void)
{
    HeatStatus status = HEAT_STOP;
    if (LOCK()) {
        status = heat.status;
        UNLOCK();
    }
    heat_status_set(status == HEAT_RUNNING ? HEAT_STOP : HEAT_RUNNING);
}

bool heat_level_set(HeatLevel level)
{
    if (level > HEAT_LEVEL_3 || !xHeatCtrlQueue)
        return false;
    HeatMsg msg = {.type = MSG_SET_LEVEL, .param.level = level};
    return xQueueSend(xHeatCtrlQueue, &msg, 0) == pdPASS;
}

void heat_level_up(void)
{
    HeatLevel new_level = HEAT_LEVEL_1;
    if (LOCK()) {
        new_level = (heat.level < HEAT_LEVEL_3) ? (heat.level + 1) : HEAT_LEVEL_3;
        UNLOCK();
    }
    if (new_level == HEAT_LEVEL_3) {
        buzzer_beep(5);
    }
    heat_level_set(new_level);
}

void heat_level_down(void)
{
    HeatLevel new_level = HEAT_LEVEL_3;
    if (LOCK()) {
        new_level = (heat.level > HEAT_LEVEL_1) ? (heat.level - 1) : HEAT_LEVEL_1;
        UNLOCK();
    }
    if (new_level == HEAT_LEVEL_1) {
        buzzer_beep(5);
    }
    heat_level_set(new_level);
}

bool heat_timer_set(uint16_t minute)
{
    if (minute > 720 || !xHeatCtrlQueue)
        return false;
    HeatMsg msg = {.type = MSG_SET_TIMER, .param.minute = minute};
    return xQueueSend(xHeatCtrlQueue, &msg, 0) == pdPASS;
}
