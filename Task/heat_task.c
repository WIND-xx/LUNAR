#include "heat_task.h"
#include "heat.h"
#include "ntc.h"
#include "pid.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"
#include <stdint.h>

#include "protocal_task.h" // 用于寄存器变更处理
// 加热控制结构体实例
Heat_t heat = {.status = HEAT_STOP, .target_temperature = 50.0f, .set_time = 0, .remain_sec = 0};

// 定时相关资源
static TimerHandle_t     xHeatingTimer = NULL; // 加热定时器句柄
static SemaphoreHandle_t xHeatMutex = NULL;    // 保护heat结构体的互斥锁
static QueueHandle_t     xHeatMsgQueue = NULL; // 加热任务消息队列

// 消息类型定义
typedef enum
{
    MSG_TIMER_EXPIRE = 0x01, // 定时结束消息
    MSG_UPDATE_REMAIN = 0x02 // 更新剩余时间消息
} HeatMsgType;

// 定时器回调函数：定时结束时发送消息
static void heating_timer_callback(TimerHandle_t xTimer)
{
    (void) xTimer;
    HeatMsgType msg = MSG_TIMER_EXPIRE;
    xQueueSend(xHeatMsgQueue, &msg, 0); // 非阻塞发送
}

// 定时更新剩余时间的软件定时器（1秒一次）
static TimerHandle_t xRemainTimer = NULL;

static void remain_timer_callback(TimerHandle_t xTimer)
{
    (void) xTimer;
    HeatMsgType msg = MSG_UPDATE_REMAIN;
    xQueueSend(xHeatMsgQueue, &msg, 0); // 非阻塞发送
}

// 加热控制任务：整合PID控制与定时逻辑
void heat_control_task(void *arg)
{
    (void) arg;
    static TickType_t xLastWakeTime = 0;
    xLastWakeTime = xTaskGetTickCount();
    PID_Controller heater_pid;
    PID_Init(&heater_pid, 10.0f, 0.1f, 4.5f, 50.0f, 0.0f, 100.0f);

    for (;;)
    {
        // 处理消息队列（优先处理定时相关事件）
        HeatMsgType msg;
        if (xQueueReceive(xHeatMsgQueue, &msg, 0) == pdTRUE)
        {
            xSemaphoreTake(xHeatMutex, portMAX_DELAY);

            switch (msg)
            {
                case MSG_TIMER_EXPIRE:
                    // 定时结束：关闭加热
                    heat.status = HEAT_STOP;
                    heat.remain_sec = 0;
                    heat.set_time = 0;
                    heat_off();                  // 关闭硬件加热
                    xTimerStop(xRemainTimer, 0); // 停止剩余时间更新
                    break;

                case MSG_UPDATE_REMAIN:
                    // 每秒更新剩余时间
                    if (heat.status == HEAT_RUNNING && heat.remain_sec > 0)
                    {
                        heat.remain_sec--;
                        // 剩余时间为0时触发定时结束（双重保障）
                        if (heat.remain_sec == 0)
                        {
                            heat.status = HEAT_STOP;
                            heat.set_time = 0;
                            heat_off();
                            xTimerStop(xRemainTimer, 0);
                            xTimerStop(xHeatingTimer, 0);
                        }
                    }
                    break;
            }

            xSemaphoreGive(xHeatMutex);
        }

        // 执行PID温度控制（仅在运行状态）
        xSemaphoreTake(xHeatMutex, portMAX_DELAY);
        HeatStatus current_status = heat.status;
        float      target_temp = heat.target_temperature;
        xSemaphoreGive(xHeatMutex);

        if (current_status == HEAT_RUNNING)
        {
            float current_temp;
            int   ret = NTC_Read(&current_temp);

            if (ret != 0)
            {
                PID_Reset(&heater_pid);
                heat_off(); // 温度读取失败时关闭加热
            }
            else
            {
                float pid_output = PID(&heater_pid, current_temp, target_temp, 100);
                if (pid_output > 0)
                {
                    heat_on(pid_output); // 按PID输出控制加热强度
                }
                else { heat_off(); }
            }
        }
        else
        {
            heat_off(); // 非运行状态关闭加热
        }

        // 100ms周期阻塞（保证控制频率）
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
    }
}

// 初始化加热任务相关资源
void heat_task_init(void)
{
    // 创建互斥锁（保护heat结构体）
    xHeatMutex = xSemaphoreCreateMutex();
    configASSERT(xHeatMutex != NULL);

    // 创建消息队列（容量为5，存放消息类型）
    xHeatMsgQueue = xQueueCreate(5, sizeof(HeatMsgType));
    configASSERT(xHeatMsgQueue != NULL);

    // 创建加热定时主定时器（一次性触发）
    xHeatingTimer = xTimerCreate("HeatingTimer",
                                 pdMS_TO_TICKS(1000), // 初始周期（后续动态修改）
                                 pdFALSE, (void *) 0, heating_timer_callback);
    configASSERT(xHeatingTimer != NULL);

    // 创建剩余时间更新定时器（1秒周期触发）
    xRemainTimer = xTimerCreate("RemainTimer",
                                pdMS_TO_TICKS(1000), // 1秒周期
                                pdTRUE,              // 自动重载
                                (void *) 1, remain_timer_callback);
    configASSERT(xRemainTimer != NULL);

    // 创建加热控制任务
    xTaskCreate(heat_control_task, "heat_task",
                512, // 堆栈大小
                NULL,
                3, // 任务优先级（高于空闲任务）
                NULL);
}

// 设置加热定时（外部调用接口）
void heat_set_timer(uint16_t minute)
{
    xSemaphoreTake(xHeatMutex, portMAX_DELAY);

    // 更新定时参数
    heat.set_time = minute;
    heat.remain_sec = (uint32_t) minute * 60; // 转换为秒

    if (minute > 0 && heat.status == HEAT_RUNNING)
    {
        // 启动/重置主定时器（定时时间=总秒数）
        xTimerStop(xHeatingTimer, 0);
        xTimerChangePeriod(xHeatingTimer, pdMS_TO_TICKS(heat.remain_sec * 1000), 0);
        xTimerStart(xHeatingTimer, 0);

        // 启动剩余时间更新定时器
        if (xTimerIsTimerActive(xRemainTimer) == pdFALSE) { xTimerStart(xRemainTimer, 0); }
    }
    else
    {
        // 定时为0时停止定时器
        xTimerStop(xHeatingTimer, 0);
        xTimerStop(xRemainTimer, 0);
    }

    xSemaphoreGive(xHeatMutex);
}

// 启动/停止加热（外部调用接口）
void heat_set_status(HeatStatus status)
{
    xSemaphoreTake(xHeatMutex, portMAX_DELAY);

    heat.status = status;
    if (status == HEAT_STOP)
    {
        // 停止加热时关闭所有定时器
        xTimerStop(xHeatingTimer, 0);
        xTimerStop(xRemainTimer, 0);
        heat.remain_sec = 0;
        heat.set_time = 0;
    }
    else
    {
        // 启动加热时，如果有定时设置则启动定时器
        if (heat.set_time > 0)
        {
            heat.remain_sec = (uint32_t) heat.set_time * 60;
            xTimerChangePeriod(xHeatingTimer, pdMS_TO_TICKS(heat.remain_sec * 1000), 0);
            xTimerStart(xHeatingTimer, 0);
            xTimerStart(xRemainTimer, 0);
        }
    }

    xSemaphoreGive(xHeatMutex);
}
void heat_status_switch(void)
{
    xSemaphoreTake(xHeatMutex, portMAX_DELAY);
    HeatStatus status = heat.status;
    xSemaphoreGive(xHeatMutex);

    if (status == HEAT_RUNNING)
        heat_set_status(HEAT_STOP);
    else
        heat_set_status(HEAT_RUNNING);
}
// 设置加热档位（外部调用接口）
void heat_set_level(HeatLevel level)
{
    xSemaphoreTake(xHeatMutex, portMAX_DELAY);

    switch (level)
    {
        case HEAT_LEVEL_1:
            heat.target_temperature = 35.0f;
            break;
        case HEAT_LEVEL_2:
            heat.target_temperature = 45.0f;
            break;
        case HEAT_LEVEL_3:
            heat.target_temperature = 55.0f;
            break;
        default:
            heat.target_temperature = 0.0f;
            break;
    }
    heat.level = level;
    xSemaphoreGive(xHeatMutex);
}
void heat_level_up(void)
{

    int8_t level = heat.level + 1;
    if (level > HEAT_LEVEL_3) level = HEAT_LEVEL_3;
    heat_set_level((HeatLevel) level);
}
void heat_level_down(void)
{
    int8_t level = heat.level - 1;
    if (level < HEAT_LEVEL_1) level = HEAT_LEVEL_1;
    heat_set_level((HeatLevel) level);
}
