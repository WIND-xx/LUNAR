/**
 * @file heat_task.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief
 * @version 0.1
 * @date 2025-11-30
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "heat_task.h"
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

static TimerHandle_t     xHeatingTimer = NULL;
static TimerHandle_t     xRemainTimer = NULL;
static SemaphoreHandle_t xHeatMutex = NULL;
static QueueHandle_t     xHeatCtrlQueue = NULL;

typedef enum
{
    MSG_TIMER_EXPIRE = 0x01,
    MSG_UPDATE_REMAIN = 0x02,
    MSG_SET_STATUS = 0x03,
    MSG_SET_LEVEL = 0x04,
    MSG_SET_TIMER = 0x05
} HeatMsgType;

typedef struct
{
    HeatMsgType type;
    union {
        HeatStatus status;
        HeatLevel  level;
        uint16_t   minute;
    } param;
} HeatMsg;

#define LOCK()   (xSemaphoreTake(xHeatMutex, pdMS_TO_TICKS(50)) == pdTRUE)
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

    if (LOCK())
    {
        old_status = heat.status;
        heat.status = HEAT_STOP;
        heat.remain_sec = 0;
        heat.set_time = 0;
        heat.is_timing = false;
        UNLOCK();
    }

    if (old_status == HEAT_RUNNING)
    {
        heat_hw_sync_off();
        led_time_select(0);
        xTimerStop(xRemainTimer, 0);
        xTimerStop(xHeatingTimer, 0);
        protocal_uplode_heat();
        buzzer_beep(10);
    }
}

static void heating_timer_callback(TimerHandle_t xTimer)
{
    (void) xTimer;
    HeatMsg msg = {.type = MSG_TIMER_EXPIRE};
    xQueueSend(xHeatCtrlQueue, &msg, 0);
}

static void remain_timer_callback(TimerHandle_t xTimer)
{
    (void) xTimer;
    HeatMsg msg = {.type = MSG_UPDATE_REMAIN};
    xQueueSend(xHeatCtrlQueue, &msg, 0);
}

static void process_heat_message(HeatMsg *msg)
{
    switch (msg->type)
    {
        case MSG_TIMER_EXPIRE: {
            HeatStatus old_status = HEAT_STOP;
            uint16_t   old_set_time = 0;

            if (LOCK())
            {
                old_status = heat.status;
                old_set_time = heat.set_time;
                heat.status = HEAT_STOP;
                heat.remain_sec = 0;
                heat.set_time = 0;
                heat.is_timing = false;
                UNLOCK();
            }

            if (old_status == HEAT_RUNNING && old_set_time > 0)
            {
                heat_hw_sync_off();
                led_time_select(0);
                xTimerStop(xRemainTimer, 0);
                xTimerStop(xHeatingTimer, 0);
                protocal_uplode_heat();
                buzzer_beep(200);
            }
            break;
        }

        case MSG_UPDATE_REMAIN: {
            uint32_t   remain = 0;
            HeatStatus status = HEAT_STOP;

            if (LOCK())
            {
                status = heat.status;
                if (status == HEAT_RUNNING && heat.remain_sec > 0)
                {
                    heat.remain_sec--;
                    remain = heat.remain_sec;
                }
                UNLOCK();
            }

            if (status == HEAT_RUNNING && remain > 0) { led_time_select(remain); }
            else if (status == HEAT_RUNNING && remain == 0) { heat_stop_all(); }
            break;
        }

        case MSG_SET_STATUS: {
            HeatStatus new_status = msg->param.status;
            HeatStatus old_status = HEAT_STOP;
            uint16_t   set_time = 0;

            if (LOCK())
            {
                old_status = heat.status;
                set_time = heat.set_time;
                heat.status = new_status;
                UNLOCK();
            }

            if (new_status == HEAT_STOP) { heat_stop_all(); }
            else if (new_status == HEAT_RUNNING && old_status != HEAT_RUNNING)
            {
                heat_hw_sync_on();
                if (set_time > 0)
                {
                    if (LOCK())
                    {
                        heat.remain_sec = (uint32_t) set_time * 60;
                        heat.is_timing = true;
                        UNLOCK();
                    }
                    xTimerChangePeriod(xHeatingTimer, pdMS_TO_TICKS(heat.remain_sec * 1000), 0);
                    xTimerStart(xHeatingTimer, 0);
                    if (!xTimerIsTimerActive(xRemainTimer)) { xTimerStart(xRemainTimer, 0); }
                }
                protocal_uplode_heat();
            }
            break;
        }

        case MSG_SET_LEVEL: {
            if (msg->param.level > HEAT_LEVEL_3) break;

            float new_target = 35.0f;

            switch (msg->param.level)
            {
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

            if (LOCK())
            {
                heat.level = msg->param.level;
                heat.target_temperature = new_target;
                UNLOCK();
            }
            protocal_uplode_heat();
            break;
        }

        case MSG_SET_TIMER: {
            if (msg->param.minute > 720) break;

            HeatStatus status = HEAT_STOP;

            if (LOCK())
            {
                status = heat.status;
                heat.set_time = msg->param.minute;
                heat.is_timing = (msg->param.minute > 0);
                if (heat.is_timing) { heat.remain_sec = (uint32_t) heat.set_time * 60; }
                else { heat.remain_sec = 0; }
                UNLOCK();
            }

            led_time_select(heat.remain_sec);

            if (heat.is_timing && status == HEAT_RUNNING)
            {
                xTimerStop(xHeatingTimer, 0);
                xTimerChangePeriod(xHeatingTimer, pdMS_TO_TICKS(heat.remain_sec * 1000), 0);
                xTimerStart(xHeatingTimer, 0);
                if (!xTimerIsTimerActive(xRemainTimer)) { xTimerStart(xRemainTimer, 0); }
            }
            else
            {
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

void heat_control_task(void *arg)
{
    (void) arg;
    TickType_t     xLastWakeTime = xTaskGetTickCount();
    PID_Controller heater_pid;
    uint8_t        ntc_fail_count = 0;
    TickType_t     control_period = pdMS_TO_TICKS(100);

    PID_Init(&heater_pid, 10.0f, 0.1f, 4.5f, 50.0f, 0.0f, 100.0f);
    heat_init();

    for (;;)
    {
        HeatMsg msg;
        while (xQueueReceive(xHeatCtrlQueue, &msg, 0) == pdTRUE)
        {
            process_heat_message(&msg);
        }

        HeatStatus status = HEAT_STOP;
        float      target_temp = 0.0f;
        if (LOCK())
        {
            status = heat.status;
            target_temp = heat.target_temperature;
            UNLOCK();
        }

        if (status == HEAT_RUNNING) { control_period = (target_temp > 45.0f) ? pdMS_TO_TICKS(50) : pdMS_TO_TICKS(100); }
        else { control_period = pdMS_TO_TICKS(200); }

        if (status == HEAT_RUNNING)
        {
            heat_hw_sync_on();
            float curr_temp = 0.0f;

            if (NTC_Read(&curr_temp) == 0)
            {
                ntc_fail_count = 0;
                float pid_out = PID(&heater_pid, curr_temp, target_temp, control_period);
                heat_on(pid_out);
            }
            else
            {
                if (++ntc_fail_count > 3)
                {
                    heat_stop_all();
                    ntc_fail_count = 0;
                }
                else { vTaskDelay(pdMS_TO_TICKS(50)); }
            }
        }
        else
        {
            heat_hw_sync_off();
            PID_Reset(&heater_pid);
            ntc_fail_count = 0;
        }

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

bool heat_set_status(HeatStatus status)
{
    if (!xHeatCtrlQueue) return false;
    HeatMsg msg = {.type = MSG_SET_STATUS, .param.status = status};
    return xQueueSend(xHeatCtrlQueue, &msg, 0) == pdPASS;
}

bool heat_set_level(HeatLevel level)
{
    if (level > HEAT_LEVEL_3 || !xHeatCtrlQueue) return false;
    HeatMsg msg = {.type = MSG_SET_LEVEL, .param.level = level};
    return xQueueSend(xHeatCtrlQueue, &msg, 0) == pdPASS;
}

bool heat_set_timer(uint16_t minute)
{
    if (minute > 720 || !xHeatCtrlQueue) return false;
    HeatMsg msg = {.type = MSG_SET_TIMER, .param.minute = minute};
    return xQueueSend(xHeatCtrlQueue, &msg, 0) == pdPASS;
}

void heat_status_switch(void)
{
    HeatStatus status = HEAT_STOP;
    if (LOCK())
    {
        status = heat.status;
        UNLOCK();
    }
    heat_set_status(status == HEAT_RUNNING ? HEAT_STOP : HEAT_RUNNING);
}

void heat_level_up(void)
{
    HeatLevel new_level = HEAT_LEVEL_1;
    if (LOCK())
    {
        new_level = (heat.level < HEAT_LEVEL_3) ? (heat.level + 1) : HEAT_LEVEL_3;
        UNLOCK();
    }
    if (new_level == HEAT_LEVEL_3) { buzzer_beep(5); }
    heat_set_level(new_level);
}

void heat_level_down(void)
{
    HeatLevel new_level = HEAT_LEVEL_3;
    if (LOCK())
    {
        new_level = (heat.level > HEAT_LEVEL_1) ? (heat.level - 1) : HEAT_LEVEL_1;
        UNLOCK();
    }
    if (new_level == HEAT_LEVEL_1) { buzzer_beep(5); }
    heat_set_level(new_level);
}
