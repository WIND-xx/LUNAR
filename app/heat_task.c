

/**
 * @file heat_task.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief Heat control task implementation with PID temperature control
 * @version 0.3
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
#include <stdlib.h>

/* 常量定义 */
#define HEAT_QUEUE_SIZE             5      // 加热控制队列大小
#define HEAT_TASK_STACK_SIZE        512    // 加热任务栈大小
#define HEAT_TASK_PRIORITY          3      // 加热任务优先级
#define HEAT_MUTEX_TIMEOUT_MS       50     // 互斥锁超时时间(毫秒)
#define HEAT_MAX_TIMER_MINUTES      720    // 最大定时时长(分钟)
#define NTC_MAX_FAIL_COUNT          3      // NTC读取失败最大次数
#define CONTROL_PERIOD_NORMAL_MS    100    // 正常控制周期(毫秒)
#define CONTROL_PERIOD_HIGH_MS      50     // 高温控制周期(毫秒)
#define CONTROL_PERIOD_IDLE_MS      200    // 空闲控制周期(毫秒)
#define HIGH_TEMP_THRESHOLD         45.0f  // 高温阈值(摄氏度)

/* 各加热档位对应的目标温度 */
static const float HEAT_LEVEL_TEMPS[] = {
    [HEAT_LEVEL_1] = 35.0f,  // 低温档
    [HEAT_LEVEL_2] = 45.0f,  // 中温档
    [HEAT_LEVEL_3] = 55.0f   // 高温档
};

/* 全局默认控制器实例（向后兼容） */
static HeatController* g_defaultController = NULL;

/* 向后兼容的全局状态 */
Heat_t heat = {
    .status             = HEAT_STOP,    // 加热状态
    .target_temperature = 35.0f,        // 目标温度
    .set_time           = 0,            // 设置的定时时长
    .remain_sec         = 0,            // 剩余秒数
    .level              = HEAT_LEVEL_1, // 加热档位
    .is_timing          = false         // 是否正在计时
};

/* 内部通信消息类型 */
typedef enum
{
    MSG_TIMER_EXPIRE  = 0x01,  // 定时器到期消息
    MSG_UPDATE_REMAIN = 0x02,  // 更新剩余时间消息
    MSG_SET_STATUS    = 0x03,  // 设置状态消息
    MSG_SET_LEVEL     = 0x04,  // 设置档位消息
    MSG_SET_TIMER     = 0x05   // 设置定时器消息
} HeatMsgType;

/* 消息结构体 */
typedef struct
{
    HeatMsgType type;  // 消息类型
    union
    {
        HeatStatus status;  // 加热状态参数
        HeatLevel level;    // 加热档位参数
        uint16_t minute;    // 定时分钟数参数
    } param;
} HeatMsg;

/* ===========================================
 * 硬件抽象层默认实现
 * ===========================================*/

static void hw_heat_on(uint16_t duty)
{
    heat_on(duty);
}

static void hw_heat_off(void)
{
    heat_off();
}

static void hw_led_on(void)
{
    led_set_mode(LED_RF, LED_MODE_ON, 0);
}

static void hw_led_off(void)
{
    led_set_mode(LED_RF, LED_MODE_OFF, 0);
}

static void hw_led_time_display(uint32_t seconds)
{
    led_time_select(seconds);
}

static void hw_buzzer_beep(uint16_t ms)
{
    buzzer_beep(ms);
}

static int hw_read_temperature(float* temp)
{
    return NTC_Read(temp);
}

static HeatHwInterface g_defaultHwInterface = {
    .heatOn = hw_heat_on,
    .heatOff = hw_heat_off,
    .ledOn = hw_led_on,
    .ledOff = hw_led_off,
    .ledTimeDisplay = hw_led_time_display,
    .buzzerBeep = hw_buzzer_beep,
    .readTemperature = hw_read_temperature
};

HeatHwInterface* HeatController_CreateDefaultHwInterface(void)
{
    return &g_defaultHwInterface;
}

HeatConfig HeatController_GetDefaultConfig(void)
{
    HeatConfig config = {
        .queueSize = HEAT_QUEUE_SIZE,
        .taskStackSize = HEAT_TASK_STACK_SIZE,
        .taskPriority = HEAT_TASK_PRIORITY,
        .mutexTimeoutMs = HEAT_MUTEX_TIMEOUT_MS,
        .maxTimerMinutes = HEAT_MAX_TIMER_MINUTES,
        .ntcMaxFailCount = NTC_MAX_FAIL_COUNT,
        .controlPeriodNormalMs = CONTROL_PERIOD_NORMAL_MS,
        .controlPeriodHighMs = CONTROL_PERIOD_HIGH_MS,
        .controlPeriodIdleMs = CONTROL_PERIOD_IDLE_MS,
        .highTempThreshold = HIGH_TEMP_THRESHOLD
    };
    return config;
}

/* 互斥锁加锁/解锁宏 */
#define LOCK(ctrl) (xSemaphoreTake((ctrl)->mutex, pdMS_TO_TICKS((ctrl)->config.mutexTimeoutMs)) == pdTRUE)
#define UNLOCK(ctrl) xSemaphoreGive((ctrl)->mutex)

/**
 * @brief 关闭加热硬件和LED指示灯
 */
static void heat_hw_sync_off(HeatController* ctrl)
{
    ctrl->hwInterface->heatOff();  // 关闭加热器
    ctrl->hwInterface->ledOff();   // 关闭LED
}

/**
 * @brief 打开加热LED指示灯
 */
static void heat_hw_sync_on(HeatController* ctrl)
{
    ctrl->hwInterface->ledOn();  // 打开LED
}

/**
 * @brief 停止所有加热操作并重置状态
 */
static void heat_stop_all(HeatController* ctrl)
{
    HeatStatus old_status = HEAT_STOP;

    // 获取并更新加热状态
    if (LOCK(ctrl))
    {
        old_status      = ctrl->state.status;
        ctrl->state.status     = HEAT_STOP;
        ctrl->state.remain_sec = 0;
        ctrl->state.set_time   = 0;
        ctrl->state.is_timing  = false;
        UNLOCK(ctrl);
    }

    // 如果之前正在运行，执行停止操作
    if (old_status == HEAT_RUNNING)
    {
        heat_hw_sync_off(ctrl);  // 关闭硬件
        ctrl->hwInterface->ledTimeDisplay(0);  // 清除时间显示
        xTimerStop(ctrl->remainTimer, 0);   // 停止倒计时定时器
        xTimerStop(ctrl->heatingTimer, 0);  // 停止加热定时器
        protocal_uplode_heat();  // 上报状态
        ctrl->hwInterface->buzzerBeep(5);  // 短促提示音
    }
}

/**
 * @brief 加热时长到期时的定时器回调函数
 */
static void heating_timer_callback(TimerHandle_t xTimer)
{
    HeatController* ctrl = (HeatController*)pvTimerGetTimerID(xTimer);
    if (!ctrl) return;
    
    HeatMsg msg = {.type = MSG_TIMER_EXPIRE};  // 构造定时器到期消息
    xQueueSend(ctrl->ctrlQueue, &msg, 0);  // 发送到控制队列
}

/**
 * @brief 剩余时间倒计时定时器回调函数(每秒触发一次)
 */
static void remain_timer_callback(TimerHandle_t xTimer)
{
    HeatController* ctrl = (HeatController*)pvTimerGetTimerID(xTimer);
    if (!ctrl) return;
    
    HeatMsg msg = {.type = MSG_UPDATE_REMAIN};  // 构造更新剩余时间消息
    xQueueSend(ctrl->ctrlQueue, &msg, 0);  // 发送到控制队列
}

/**
 * @brief 处理定时器到期消息
 */
static void handle_timer_expire(HeatController* ctrl)
{
    HeatStatus old_status = HEAT_STOP;
    uint16_t old_set_time = 0;

    // 获取旧状态并重置
    if (LOCK(ctrl))
    {
        old_status      = ctrl->state.status;
        old_set_time    = ctrl->state.set_time;
        ctrl->state.status     = HEAT_STOP;
        ctrl->state.remain_sec = 0;
        ctrl->state.set_time   = 0;
        ctrl->state.is_timing  = false;
        UNLOCK(ctrl);
    }

    // 如果之前正在运行且设置了定时，执行停止操作
    if (old_status == HEAT_RUNNING && old_set_time > 0)
    {
        heat_hw_sync_off(ctrl);  // 关闭硬件
        ctrl->hwInterface->ledTimeDisplay(0);  // 清除时间显示
        xTimerStop(ctrl->remainTimer, 0);   // 停止倒计时定时器
        xTimerStop(ctrl->heatingTimer, 0);  // 停止加热定时器
        protocal_uplode_heat();  // 上报状态
        ctrl->hwInterface->buzzerBeep(200);  // 长提示音表示定时完成
    }
}

/**
 * @brief 处理剩余时间更新消息
 */
static void handle_update_remain(HeatController* ctrl)
{
    uint32_t remain   = 0;
    HeatStatus status = HEAT_STOP;

    // 更新剩余时间
    if (LOCK(ctrl))
    {
        status = ctrl->state.status;
        if (status == HEAT_RUNNING && ctrl->state.remain_sec > 0)
        {
            ctrl->state.remain_sec--;  // 秒数递减
            remain = ctrl->state.remain_sec;
        }
        UNLOCK(ctrl);
    }

    // 更新显示或停止加热
    if (status == HEAT_RUNNING)
    {
        if (remain > 0)
        {
            ctrl->hwInterface->ledTimeDisplay(remain);  // 更新剩余时间显示
        }
        else
        {
            heat_stop_all(ctrl);  // 时间到，停止加热
        }
    }
}

/**
 * @brief 处理状态变更消息
 */
static void handle_set_status(HeatController* ctrl, HeatStatus new_status)
{
    HeatStatus old_status = HEAT_STOP;
    uint16_t set_time     = 0;

    // 更新状态
    if (LOCK(ctrl))
    {
        old_status  = ctrl->state.status;
        set_time    = ctrl->state.set_time;
        ctrl->state.status = new_status;
        UNLOCK(ctrl);
    }

    // 根据新状态执行相应操作
    if (new_status == HEAT_STOP)
    {
        heat_stop_all(ctrl);  // 停止加热
    }
    else if (new_status == HEAT_RUNNING && old_status != HEAT_RUNNING)
    {
        heat_hw_sync_on(ctrl);  // 打开硬件
        
        // 如果设置了定时，启动定时器
        if (set_time > 0)
        {
            uint32_t remain_sec = (uint32_t)set_time * 60;  // 转换为秒
            
            if (LOCK(ctrl))
            {
                ctrl->state.remain_sec = remain_sec;
                ctrl->state.is_timing  = true;
                UNLOCK(ctrl);
            }
            
            // 配置并启动加热定时器
            xTimerChangePeriod(ctrl->heatingTimer, pdMS_TO_TICKS(remain_sec * 1000), 0);
            xTimerStart(ctrl->heatingTimer, 0);
            
            // 启动倒计时定时器
            if (!xTimerIsTimerActive(ctrl->remainTimer))
            {
                xTimerStart(ctrl->remainTimer, 0);
            }
        }
        protocal_uplode_heat();  // 上报状态
    }
}

/**
 * @brief 处理加热档位变更消息
 */
static void handle_set_level(HeatController* ctrl, HeatLevel level)
{
    if (level > HEAT_LEVEL_3)
        return;

    // 更新档位和目标温度
    if (LOCK(ctrl))
    {
        ctrl->state.level              = level;
        ctrl->state.target_temperature = HEAT_LEVEL_TEMPS[level];
        UNLOCK(ctrl);
    }
    protocal_uplode_heat();  // 上报状态
}

/**
 * @brief 处理定时器设置消息
 */
static void handle_set_timer(HeatController* ctrl, uint16_t minute)
{
    if (minute > ctrl->config.maxTimerMinutes)
        return;

    HeatStatus status = HEAT_STOP;
    uint32_t remain_sec = 0;

    // 更新定时设置
    if (LOCK(ctrl))
    {
        status         = ctrl->state.status;
        ctrl->state.set_time  = minute;
        ctrl->state.is_timing = (minute > 0);
        remain_sec     = ctrl->state.is_timing ? ((uint32_t)minute * 60) : 0;
        ctrl->state.remain_sec = remain_sec;
        UNLOCK(ctrl);
    }

    ctrl->hwInterface->ledTimeDisplay(remain_sec);  // 更新时间显示

    // 如果正在运行且设置了定时，重新配置定时器
    if (ctrl->state.is_timing && status == HEAT_RUNNING)
    {
        xTimerStop(ctrl->heatingTimer, 0);
        xTimerChangePeriod(ctrl->heatingTimer, pdMS_TO_TICKS(remain_sec * 1000), 0);
        xTimerStart(ctrl->heatingTimer, 0);
        
        if (!xTimerIsTimerActive(ctrl->remainTimer))
        {
            xTimerStart(ctrl->remainTimer, 0);
        }
    }
    else
    {
        // 取消定时，停止定时器
        xTimerStop(ctrl->heatingTimer, 0);
        xTimerStop(ctrl->remainTimer, 0);
    }
    protocal_uplode_heat();  // 上报状态
}

/**
 * @brief 处理接收到的加热控制消息
 * @param ctrl 控制器实例指针
 * @param msg 指向消息结构体的指针
 */
static void process_heat_message(HeatController* ctrl, HeatMsg* msg)
{
    switch (msg->type)
    {
        case MSG_TIMER_EXPIRE:
            handle_timer_expire(ctrl);  // 处理定时器到期
            break;

        case MSG_UPDATE_REMAIN:
            handle_update_remain(ctrl);  // 处理剩余时间更新
            break;

        case MSG_SET_STATUS:
            handle_set_status(ctrl, msg->param.status);  // 处理状态设置
            break;

        case MSG_SET_LEVEL:
            handle_set_level(ctrl, msg->param.level);  // 处理档位设置
            break;

        case MSG_SET_TIMER:
            handle_set_timer(ctrl, msg->param.minute);  // 处理定时器设置
            break;

        default:
            break;
    }
}

/**
 * @brief 带PID温度调节的主加热控制任务
 * @param arg 控制器实例指针
 */
void heat_control_task(void* arg)
{
    HeatController* ctrl = (HeatController*)arg;
    if (!ctrl) return;
    
    TickType_t xLastWakeTime   = xTaskGetTickCount();  // 记录上次唤醒时间
    TickType_t control_period  = pdMS_TO_TICKS(ctrl->config.controlPeriodNormalMs);  // 控制周期

    // 初始化PID控制器(Kp=10.0, Ki=0.1, Kd=4.5, 输出限制0-100)
    pid_init(&ctrl->pidController, 10.0f, 0.1f, 4.5f, 50.0f, 0.0f, 100.0f);
    heat_init();  // 初始化加热硬件

    for (;;)
    {
        /* 处理所有待处理的消息 */
        HeatMsg msg;
        while (xQueueReceive(ctrl->ctrlQueue, &msg, 0) == pdTRUE)
        {
            process_heat_message(ctrl, &msg);
        }

        /* 读取当前加热状态 */
        HeatStatus status = HEAT_STOP;
        float target_temp = 0.0f;
        
        if (LOCK(ctrl))
        {
            status      = ctrl->state.status;
            target_temp = ctrl->state.target_temperature;
            UNLOCK(ctrl);
        }

        /* 根据状态和温度调整控制周期 */
        if (status == HEAT_RUNNING)
        {
            // 高温时使用更快的控制周期
            control_period = (target_temp > ctrl->config.highTempThreshold) 
                           ? pdMS_TO_TICKS(ctrl->config.controlPeriodHighMs) 
                           : pdMS_TO_TICKS(ctrl->config.controlPeriodNormalMs);
        }
        else
        {
            // 空闲时使用较慢的周期节省资源
            control_period = pdMS_TO_TICKS(ctrl->config.controlPeriodIdleMs);
        }

        /* 执行加热控制 */
        if (status == HEAT_RUNNING)
        {
            heat_hw_sync_on(ctrl);  // 确保LED打开
            float curr_temp = 0.0f;

            // 读取当前温度
            if (ctrl->hwInterface->readTemperature(&curr_temp) == 0)
            {
                // 读取成功，清除失败计数
                ctrl->ntcFailCount   = 0;
                // 计算PID输出并控制加热器
                uint16_t pid_out = pid_compute(&ctrl->pidController, curr_temp, target_temp, control_period);
                ctrl->hwInterface->heatOn(pid_out);
            }
            else
            {
                /* 处理NTC读取失败 */
                if (++(ctrl->ntcFailCount) > ctrl->config.ntcMaxFailCount)
                {
                    // 连续失败次数过多，停止加热以保证安全
                    heat_stop_all(ctrl);
                    ctrl->ntcFailCount = 0;
                }
                else
                {
                    // 短暂延时后重试
                    vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
        }
        else
        {
            // 停止状态下关闭硬件并重置PID
            heat_hw_sync_off(ctrl);
            pid_reset(&ctrl->pidController);
            ctrl->ntcFailCount = 0;
        }

        // 周期性延时，维持固定的控制周期
        vTaskDelayUntil(&xLastWakeTime, control_period);
    }
}

/* ===========================================
 * OOP操作函数实现
 * ===========================================*/

static void HeatController_Init(HeatController* self);
static bool HeatController_SetStatus(HeatController* self, HeatStatus status);
static bool HeatController_SetLevel(HeatController* self, HeatLevel level);
static bool HeatController_SetTimer(HeatController* self, uint16_t minute);
static void HeatController_StatusSwitch(HeatController* self);
static void HeatController_LevelUp(HeatController* self);
static void HeatController_LevelDown(HeatController* self);
static Heat_t HeatController_GetState(HeatController* self);

/**
 * @brief 创建控制器实例
 */
HeatController* HeatController_Create(const HeatConfig* config, HeatHwInterface* hwInterface)
{
    if (!config || !hwInterface)
        return NULL;
    
    HeatController* ctrl = (HeatController*)pvPortMalloc(sizeof(HeatController));
    if (!ctrl)
        return NULL;
    
    // 初始化状态
    ctrl->state.status = HEAT_STOP;
    ctrl->state.level = HEAT_LEVEL_1;
    ctrl->state.target_temperature = HEAT_LEVEL_TEMPS[HEAT_LEVEL_1];
    ctrl->state.set_time = 0;
    ctrl->state.remain_sec = 0;
    ctrl->state.is_timing = false;
    
    // 设置依赖
    ctrl->hwInterface = hwInterface;
    ctrl->config = *config;
    ctrl->ntcFailCount = 0;
    
    // 绑定操作函数
    ctrl->ops.init = HeatController_Init;
    ctrl->ops.setStatus = HeatController_SetStatus;
    ctrl->ops.setLevel = HeatController_SetLevel;
    ctrl->ops.setTimer = HeatController_SetTimer;
    ctrl->ops.statusSwitch = HeatController_StatusSwitch;
    ctrl->ops.levelUp = HeatController_LevelUp;
    ctrl->ops.levelDown = HeatController_LevelDown;
    ctrl->ops.getState = HeatController_GetState;
    
    // FreeRTOS资源将在init中创建
    ctrl->mutex = NULL;
    ctrl->ctrlQueue = NULL;
    ctrl->heatingTimer = NULL;
    ctrl->remainTimer = NULL;
    
    return ctrl;
}

/**
 * @brief 初始化控制器和FreeRTOS资源
 */
static void HeatController_Init(HeatController* self)
{
    if (!self) return;
    
    // 创建互斥锁
    self->mutex = xSemaphoreCreateMutex();
    // 创建消息队列
    self->ctrlQueue = xQueueCreate(self->config.queueSize, sizeof(HeatMsg));
    // 创建加热定时器(单次触发)
    self->heatingTimer = xTimerCreate("HeatTimer", pdMS_TO_TICKS(1000), pdFALSE, self, heating_timer_callback);
    // 创建倒计时定时器(周期性触发,每秒一次)
    self->remainTimer = xTimerCreate("RemainTimer", pdMS_TO_TICKS(1000), pdTRUE, self, remain_timer_callback);

    // 断言检查所有资源创建成功
    configASSERT(self->mutex && self->ctrlQueue && self->heatingTimer && self->remainTimer);
    
    // 创建加热控制任务
    xTaskCreate(heat_control_task, "heat_task", self->config.taskStackSize, self, self->config.taskPriority, NULL);
}

/**
 * @brief 设置加热状态(启动/停止)
 */
static bool HeatController_SetStatus(HeatController* self, HeatStatus status)
{
    if (!self || !self->ctrlQueue)
        return false;
    
    HeatMsg msg = {.type = MSG_SET_STATUS, .param.status = status};
    return xQueueSend(self->ctrlQueue, &msg, 0) == pdPASS;
}

/**
 * @brief 设置加热档位(温度目标)
 */
static bool HeatController_SetLevel(HeatController* self, HeatLevel level)
{
    if (!self || level > HEAT_LEVEL_3 || !self->ctrlQueue)
        return false;
    
    HeatMsg msg = {.type = MSG_SET_LEVEL, .param.level = level};
    return xQueueSend(self->ctrlQueue, &msg, 0) == pdPASS;
}

/**
 * @brief 设置加热定时时长
 */
static bool HeatController_SetTimer(HeatController* self, uint16_t minute)
{
    if (!self || minute > self->config.maxTimerMinutes || !self->ctrlQueue)
        return false;
    
    HeatMsg msg = {.type = MSG_SET_TIMER, .param.minute = minute};
    return xQueueSend(self->ctrlQueue, &msg, 0) == pdPASS;
}

/**
 * @brief 切换加热状态(运行与停止之间切换)
 */
static void HeatController_StatusSwitch(HeatController* self)
{
    if (!self) return;
    
    HeatStatus status = HEAT_STOP;
    
    // 获取当前状态
    if (LOCK(self))
    {
        status = self->state.status;
        UNLOCK(self);
    }
    
    // 切换到相反状态
    HeatController_SetStatus(self, (status == HEAT_RUNNING) ? HEAT_STOP : HEAT_RUNNING);
}

/**
 * @brief 提升加热档位(带上限限制)
 */
static void HeatController_LevelUp(HeatController* self)
{
    if (!self) return;
    
    HeatLevel new_level = HEAT_LEVEL_1;
    
    // 计算新档位
    if (LOCK(self))
    {
        new_level = (self->state.level < HEAT_LEVEL_3) ? (self->state.level + 1) : HEAT_LEVEL_3;
        UNLOCK(self);
    }
    
    // 到达最高档位时提示
    if (new_level == HEAT_LEVEL_3)
    {
        self->hwInterface->buzzerBeep(5);
    }
    
    HeatController_SetLevel(self, new_level);
}

/**
 * @brief 降低加热档位(带下限限制)
 */
static void HeatController_LevelDown(HeatController* self)
{
    if (!self) return;
    
    HeatLevel new_level = HEAT_LEVEL_3;
    
    // 计算新档位
    if (LOCK(self))
    {
        new_level = (self->state.level > HEAT_LEVEL_1) ? (self->state.level - 1) : HEAT_LEVEL_1;
        UNLOCK(self);
    }
    
    // 到达最低档位时提示
    if (new_level == HEAT_LEVEL_1)
    {
        self->hwInterface->buzzerBeep(5);
    }
    
    HeatController_SetLevel(self, new_level);
}

/**
 * @brief 获取当前状态快照
 */
static Heat_t HeatController_GetState(HeatController* self)
{
    Heat_t state = {0};
    
    if (self && LOCK(self))
    {
        state = self->state;
        UNLOCK(self);
    }
    
    return state;
}

/* ===========================================
 * 向后兼容的全局API封装
 * ===========================================*/

/**
 * @brief 初始化默认加热任务
 */
void heat_task_init(void)
{
    // 创建默认配置和硬件接口
    HeatConfig config = HeatController_GetDefaultConfig();
    HeatHwInterface* hwInterface = HeatController_CreateDefaultHwInterface();
    
    // 创建默认控制器实例
    g_defaultController = HeatController_Create(&config, hwInterface);
    
    if (g_defaultController)
    {
        // 初始化控制器
        g_defaultController->ops.init(g_defaultController);
    }
}

/**
 * @brief 设置加热状态(向后兼容)
 */
bool heat_set_status(HeatStatus status)
{
    if (!g_defaultController)
        return false;
    
    bool result = g_defaultController->ops.setStatus(g_defaultController, status);
    
    // 同步全局状态
    if (LOCK(g_defaultController))
    {
        heat = g_defaultController->state;
        UNLOCK(g_defaultController);
    }
    
    return result;
}

/**
 * @brief 设置加热档位(向后兼容)
 */
bool heat_set_level(HeatLevel level)
{
    if (!g_defaultController)
        return false;
    
    bool result = g_defaultController->ops.setLevel(g_defaultController, level);
    
    // 同步全局状态
    if (LOCK(g_defaultController))
    {
        heat = g_defaultController->state;
        UNLOCK(g_defaultController);
    }
    
    return result;
}

/**
 * @brief 设置加热定时(向后兼容)
 */
bool heat_set_timer(uint16_t minute)
{
    if (!g_defaultController)
        return false;
    
    bool result = g_defaultController->ops.setTimer(g_defaultController, minute);
    
    // 同步全局状态
    if (LOCK(g_defaultController))
    {
        heat = g_defaultController->state;
        UNLOCK(g_defaultController);
    }
    
    return result;
}

/**
 * @brief 切换加热状态(向后兼容)
 */
void heat_status_switch(void)
{
    if (!g_defaultController)
        return;
    
    g_defaultController->ops.statusSwitch(g_defaultController);
    
    // 同步全局状态
    if (LOCK(g_defaultController))
    {
        heat = g_defaultController->state;
        UNLOCK(g_defaultController);
    }
}

/**
 * @brief 提升加热档位(向后兼容)
 */
void heat_level_up(void)
{
    if (!g_defaultController)
        return;
    
    g_defaultController->ops.levelUp(g_defaultController);
    
    // 同步全局状态
    if (LOCK(g_defaultController))
    {
        heat = g_defaultController->state;
        UNLOCK(g_defaultController);
    }
}

/**
 * @brief 降低加热档位(向后兼容)
 */
void heat_level_down(void)
{
    if (!g_defaultController)
        return;
    
    g_defaultController->ops.levelDown(g_defaultController);
    
    // 同步全局状态
    if (LOCK(g_defaultController))
    {
        heat = g_defaultController->state;
        UNLOCK(g_defaultController);
    }
}
