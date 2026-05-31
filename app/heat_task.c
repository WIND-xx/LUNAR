/**
 * @file heat_task.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief 加热控制任务（基于FreeRTOS）
 * @version 0.3
 * @date 2025-11-30
 * @copyright Copyright (c) 2025
 */
#include "heat_task.h"
#include "bt401.h"
#include "buzzer.h"
#include "heat.h"
#include "led.h"
#include "ntc.h"
#include "pid.h"

#include "FreeRTOS.h"
#include "protocol.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

/**
 * @brief PID控制器配置参数（集中配置，方便调试）
 */
#define HEAT_PID_KP 20.0f             ///< PID比例系数
#define HEAT_PID_KI 3.0f              ///< PID积分系数
#define HEAT_PID_KD 1.0f              ///< PID微分系数
#define HEAT_PID_DEADBAND 1.0f        ///< PID死区(℃)
#define HEAT_PID_OUT_MIN 0            ///< PID输出下限(0~100)
#define HEAT_PID_OUT_MAX 100          ///< PID输出上限(0~100)
#define HEAT_PID_INTEGRAL_LIMIT 30.0f ///< PID积分限幅
#define HEAT_PID_D_FILTER_COEFF 0.1f  ///< PID微分滤波系数

/**
 * @brief 档位-温度映射表（修改温度只需改这里）
 */
static const HeatLevelTempMap s_heat_level_temp_map[] = {
    {HEAT_LEVEL_1, 35.0f}, {HEAT_LEVEL_2, 45.0f}, {HEAT_LEVEL_3, 55.0f}};

/**
 * @brief 加热控制消息类型
 */
typedef enum {
    MSG_TIMER_EXPIRE = 0x01,  ///< 定时超时消息
    MSG_UPDATE_REMAIN = 0x02, ///< 剩余时间更新消息
    MSG_SET_STATUS = 0x03,    ///< 设置状态消息
    MSG_SET_LEVEL = 0x04,     ///< 设置档位消息
    MSG_SET_TIMER = 0x05      ///< 设置定时消息
} HeatMsgType;

/**
 * @brief 加热控制消息结构体
 */
typedef struct {
    HeatMsgType type; ///< 消息类型
    union {
        HeatStatus status; ///< 状态参数
        HeatLevel level;   ///< 档位参数
        uint16_t minute;   ///< 定时参数
    } param;               ///< 消息参数
} HeatMsg;

/**
 * @brief 加热任务私有数据结构体（封装所有私有资源）
 */
typedef struct {
    HeatStatus status;           ///< 加热状态
    HeatLevel level;             ///< 加热档位
    float target_temperature;    ///< 目标温度(℃)
    uint16_t set_time_min;       ///< 设置的定时时间(分钟)
    uint32_t remain_sec;         ///< 剩余时间(秒)
    bool is_timing;              ///< 是否处于定时状态
    pid_controller_t pid;        ///< PID控制器实例
    SemaphoreHandle_t mutex;     ///< 互斥锁
    QueueHandle_t ctrl_queue;    ///< 控制队列
    TimerHandle_t heating_timer; ///< 加热定时总定时器
    TimerHandle_t remain_timer;  ///< 剩余时间更新定时器
    uint8_t ntc_fail_count;      ///< NTC读取失败计数
} HeatTaskPrivData;

/**
 * @brief 加热任务私有数据实例（静态封装，不对外暴露）
 */
static HeatTaskPrivData s_heat_data = {.status = HEAT_STATUS_STOP,
                                       .level = HEAT_LEVEL_1,
                                       .target_temperature = 35.0f,
                                       .set_time_min = 0,
                                       .remain_sec = 0,
                                       .is_timing = false,
                                       .mutex = NULL,
                                       .ctrl_queue = NULL,
                                       .heating_timer = NULL,
                                       .remain_timer = NULL,
                                       .ntc_fail_count = 0};

static bool heat_lock_internal(void)
{
    bool lock_ret = (xSemaphoreTake(s_heat_data.mutex, pdMS_TO_TICKS(HEAT_MUTEX_LOCK_TIMEOUT_MS)) == pdTRUE);
    if (!lock_ret) {
        bt401_printf("Heat mutex lock failed!\r\n");
    }
    return lock_ret;
}

#define HEAT_LOCK() heat_lock_internal()
#define HEAT_UNLOCK() xSemaphoreGive(s_heat_data.mutex)

/**
 * @brief 根据档位获取目标温度
 * @param level 加热档位
 * @return 对应目标温度(℃)，非法档位返回默认35.0f
 */
static float heat_get_temp_by_level(HeatLevel level)
{
    for (uint8_t i = 0; i < sizeof(s_heat_level_temp_map) / sizeof(HeatLevelTempMap); i++) {
        if (s_heat_level_temp_map[i].level == level) {
            return s_heat_level_temp_map[i].target_temp;
        }
    }
    return 35.0f; // 默认档位1温度
}

/**
 * @brief 加热硬件关闭同步
 */
static void heat_hw_turn_off(void)
{
    if (heat_stop()) {
        led_set_mode(LED_RF, LED_MODE_OFF, 0);
    }
}

/**
 * @brief 加热硬件开启同步
 */
static void heat_hw_turn_on(void)
{
    if (heat_start()) {
        led_set_mode(LED_RF, LED_MODE_ON, 0);
    }
}

/**
 * @brief 停止所有加热相关操作（核心停止逻辑）
 */
static void heat_stop_all(void)
{
    HeatStatus old_status = HEAT_STATUS_STOP;

    if (HEAT_LOCK()) {
        old_status = s_heat_data.status;
        // 重置所有状态
        s_heat_data.status = HEAT_STATUS_STOP;
        s_heat_data.remain_sec = 0;
        s_heat_data.set_time_min = 0;
        s_heat_data.is_timing = false;
        s_heat_data.ntc_fail_count = 0;
        HEAT_UNLOCK();
    }

    // 仅当原状态为运行时，执行硬件和定时器操作
    if (old_status == HEAT_STATUS_RUNNING) {
        heat_hw_turn_off();
        led_time_select(0);
        // 停止定时器（非阻塞）
        if (s_heat_data.remain_timer) {
            xTimerStop(s_heat_data.remain_timer, 0);
        }
        if (s_heat_data.heating_timer) {
            xTimerStop(s_heat_data.heating_timer, 0);
        }
        pid_reset(&s_heat_data.pid);      // 重置PID
        protocol_upload_heating_status(); // 上传状态
    }
}

/**
 * @brief 加热定时超时回调函数
 * @param xTimer 定时器句柄
 */
static void heating_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    HeatMsg msg = {.type = MSG_TIMER_EXPIRE};
    // 非阻塞发送消息到队列
    if (s_heat_data.ctrl_queue) {
        xQueueSend(s_heat_data.ctrl_queue, &msg, 0);
    }
}

/**
 * @brief 剩余时间更新定时器回调
 * @param xTimer 定时器句柄
 */
static void remain_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    HeatMsg msg = {.type = MSG_UPDATE_REMAIN};
    // 非阻塞发送消息到队列
    if (s_heat_data.ctrl_queue) {
        xQueueSend(s_heat_data.ctrl_queue, &msg, 0);
    }
}

/**
 * @brief 处理加热定时超时消息
 */
static void process_msg_timer_expire(void)
{
    HeatStatus curr_status = HEAT_STATUS_STOP;
    uint16_t curr_set_time = 0;

    if (HEAT_LOCK()) {
        curr_status = s_heat_data.status;
        curr_set_time = s_heat_data.set_time_min;
        HEAT_UNLOCK();
    }

    // 仅当运行中且设置了定时才处理
    if (curr_status == HEAT_STATUS_RUNNING && curr_set_time > 0) {
        heat_stop_all();
    }
}

/**
 * @brief 处理剩余时间更新消息
 */
static void process_msg_update_remain(void)
{
    uint32_t remain_sec = 0;
    HeatStatus curr_status = HEAT_STATUS_STOP;

    if (HEAT_LOCK()) {
        curr_status = s_heat_data.status;
        if (curr_status == HEAT_STATUS_RUNNING && s_heat_data.remain_sec > 0) {
            s_heat_data.remain_sec--;
            remain_sec = s_heat_data.remain_sec;
        }
        HEAT_UNLOCK();
    }

    if (curr_status == HEAT_STATUS_RUNNING) {
        if (remain_sec > 0) {
            led_time_select(remain_sec);
        } else {
            heat_stop_all(); // 剩余时间为0，停止加热
        }
    }
}

/**
 * @brief 处理设置加热状态消息
 * @param new_status 目标状态
 */
static void process_msg_set_status(HeatStatus new_status)
{
    HeatStatus old_status = HEAT_STATUS_STOP;
    uint16_t set_time_min = 0;

    if (HEAT_LOCK()) {
        old_status = s_heat_data.status;
        set_time_min = s_heat_data.set_time_min;
        s_heat_data.status = new_status;
        HEAT_UNLOCK();
    }

    if (new_status == HEAT_STATUS_STOP) {
        heat_stop_all();
    } else if (new_status == HEAT_STATUS_RUNNING && old_status != HEAT_STATUS_RUNNING) {
        heat_hw_turn_on(); // 开启硬件

        // 有定时则启动定时器
        if (set_time_min > 0) {
            if (HEAT_LOCK()) {
                s_heat_data.remain_sec = (uint32_t)set_time_min * 60;
                s_heat_data.is_timing = true;
                HEAT_UNLOCK();
            }

            // 更新总定时定时器周期并启动
            if (s_heat_data.heating_timer) {
                xTimerChangePeriod(s_heat_data.heating_timer, pdMS_TO_TICKS(s_heat_data.remain_sec * 1000), 0);
                xTimerStart(s_heat_data.heating_timer, 0);
            }

            // 启动剩余时间更新定时器
            if (s_heat_data.remain_timer && !xTimerIsTimerActive(s_heat_data.remain_timer)) {
                xTimerStart(s_heat_data.remain_timer, 0);
            }
        }
    }
}

/**
 * @brief 处理设置加热档位消息
 * @param new_level 目标档位
 */
static void process_msg_set_level(HeatLevel new_level)
{
    // 校验档位合法性
    if (new_level > HEAT_LEVEL_MAX) {
        bt401_printf("Invalid heat level: %d\r\n", new_level);
        return;
    }

    float new_target_temp = heat_get_temp_by_level(new_level);

    if (HEAT_LOCK()) {
        s_heat_data.level = new_level;
        s_heat_data.target_temperature = new_target_temp;
        HEAT_UNLOCK();
    }

    bt401_printf("Heat level set to %d, target temp: %.1f℃\r\n", new_level, new_target_temp);
}

/**
 * @brief 处理设置定时消息
 * @param minute 定时分钟数
 */
static void process_msg_set_timer(uint16_t minute)
{
    // 校验定时时间合法性
    if (minute > HEAT_MAX_TIMER_MINUTE) {
        bt401_printf("Invalid timer value: %d min (max: %d)\r\n", minute, HEAT_MAX_TIMER_MINUTE);
        return;
    }

    HeatStatus curr_status = HEAT_STATUS_STOP;
    bool is_timing = (minute > 0);
    uint32_t remain_sec = is_timing ? (uint32_t)minute * 60 : 0;

    if (HEAT_LOCK()) {
        curr_status = s_heat_data.status;
        s_heat_data.set_time_min = minute;
        s_heat_data.is_timing = is_timing;
        s_heat_data.remain_sec = remain_sec;
        HEAT_UNLOCK();
    }

    // 更新LED显示剩余时间
    led_time_select(remain_sec);

    // 运行中且有定时，更新定时器
    if (is_timing && curr_status == HEAT_STATUS_RUNNING) {
        if (s_heat_data.heating_timer) {
            xTimerStop(s_heat_data.heating_timer, 0);
            xTimerChangePeriod(s_heat_data.heating_timer, pdMS_TO_TICKS(remain_sec * 1000), 0);
            xTimerStart(s_heat_data.heating_timer, 0);
        }
        if (s_heat_data.remain_timer && !xTimerIsTimerActive(s_heat_data.remain_timer)) {
            xTimerStart(s_heat_data.remain_timer, 0);
        }
    } else {
        // 无定时或未运行，停止定时器
        if (s_heat_data.heating_timer) {
            xTimerStop(s_heat_data.heating_timer, 0);
        }
        if (s_heat_data.remain_timer) {
            xTimerStop(s_heat_data.remain_timer, 0);
        }
    }
}

/**
 * @brief 处理加热控制消息
 * @param msg 消息指针
 */
static void process_heat_message(const HeatMsg* msg)
{
    if (msg == NULL) {
        return;
    }

    switch (msg->type) {
    case MSG_TIMER_EXPIRE:
        process_msg_timer_expire();
        break;
    case MSG_UPDATE_REMAIN:
        process_msg_update_remain();
        break;
    case MSG_SET_STATUS:
        process_msg_set_status(msg->param.status);
        break;
    case MSG_SET_LEVEL:
        process_msg_set_level(msg->param.level);
        break;
    case MSG_SET_TIMER:
        process_msg_set_timer(msg->param.minute);
        break;
    default:
        bt401_printf("Unknown heat msg type: %d\r\n", msg->type);
        break;
    }

    protocol_upload_heating_status(); // 上传加热状态
}

/**
 * @brief 读取NTC温度（封装读取逻辑）
 * @param curr_temp 输出参数，读取到的温度
 * @retval true - 读取成功, false - 读取失败
 */
static bool heat_read_ntc_temp(float* curr_temp)
{
    if (curr_temp == NULL) {
        return false;
    }

    if (ntc_read(curr_temp) == 0) {
        s_heat_data.ntc_fail_count = 0;
        return true;
    } else {
        s_heat_data.ntc_fail_count++;
        bt401_printf("NTC read fail, count: %d\r\n", s_heat_data.ntc_fail_count);
        return false;
    }
}

/**
 * @brief PID加热控制逻辑（核心控制逻辑拆分）
 */
static void heat_pid_control(void)
{
    float curr_temp = 0.0f;

    // 读取NTC温度
    if (!heat_read_ntc_temp(&curr_temp)) {
        // 读取失败次数超过阈值，停止加热
        if (s_heat_data.ntc_fail_count >= HEAT_NTC_FAIL_THRESHOLD) {
            heat_stop_all();
            s_heat_data.ntc_fail_count = 0;
        }
        return;
    }

    // 计算当前时间（ms）
    uint32_t current_time_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

    // 计算PID输出
    uint16_t pid_out = pid_calc_with_time(&s_heat_data.pid, curr_temp, s_heat_data.target_temperature, current_time_ms);

    // 设置加热功率
    heat_set_power(pid_out);

    // 打印调试信息（格式化输出）
    int16_t temp_scaled = (int16_t)(curr_temp * 10.0f + 0.5f);
    int16_t target_scaled = (int16_t)(s_heat_data.target_temperature * 10.0f + 0.5f);
    bt401_printf("Temp: %d.%d, Target: %d.%d, PWM: %u%%\r\n", temp_scaled / 10, temp_scaled % 10, target_scaled / 10,
                 target_scaled % 10, pid_out);
}

/**
 * @brief 加热控制任务主函数
 * @param arg 任务参数（未使用）
 */
static void heat_control_task(void* arg)
{
    (void)arg;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t control_period = pdMS_TO_TICKS(HEAT_CONTROL_PERIOD_MS);

    // 初始化PID控制器
    pid_init(&s_heat_data.pid, HEAT_PID_KP, HEAT_PID_KI, HEAT_PID_KD, HEAT_PID_DEADBAND, HEAT_PID_OUT_MIN,
             HEAT_PID_OUT_MAX);
    pid_set_integral_params(&s_heat_data.pid, HEAT_PID_DEADBAND / 2, HEAT_PID_INTEGRAL_LIMIT);
    pid_set_derivative_filter(&s_heat_data.pid, HEAT_PID_D_FILTER_COEFF);

    // 初始化加热硬件
    heat_init();

    for (;;) {
        // 非阻塞处理队列消息
        HeatMsg msg;
        while (xQueueReceive(s_heat_data.ctrl_queue, &msg, 0) == pdTRUE) {
            process_heat_message(&msg);
        }

        // 读取当前加热状态（加锁保护）
        HeatStatus curr_status = HEAT_STATUS_STOP;
        if (HEAT_LOCK()) {
            curr_status = s_heat_data.status;
            HEAT_UNLOCK();
        }

        // 根据状态执行对应逻辑
        if (curr_status == HEAT_STATUS_RUNNING) {
            heat_pid_control(); // 执行PID控制
        } else {
            pid_reset(&s_heat_data.pid);    // 重置PID
            s_heat_data.ntc_fail_count = 0; // 重置NTC失败计数
        }

        // 固定周期休眠（保证控制周期稳定）
        vTaskDelayUntil(&xLastWakeTime, control_period);
    }
}

/**
 * @brief 创建FreeRTOS资源（互斥锁、队列、定时器）
 * @retval true - 创建成功, false - 创建失败
 */
static bool heat_create_freertos_resources(void)
{
    // 创建互斥锁
    s_heat_data.mutex = xSemaphoreCreateMutex();
    if (s_heat_data.mutex == NULL) {
        bt401_printf("Create heat mutex failed!\r\n");
        return false;
    }

    // 创建控制队列
    s_heat_data.ctrl_queue = xQueueCreate(HEAT_CTRL_QUEUE_LEN, sizeof(HeatMsg));
    if (s_heat_data.ctrl_queue == NULL) {
        bt401_printf("Create heat queue failed!\r\n");
        vSemaphoreDelete(s_heat_data.mutex);
        s_heat_data.mutex = NULL;
        return false;
    }

    // 创建加热定时总定时器（一次性定时器）
    s_heat_data.heating_timer = xTimerCreate("HeatTimer", pdMS_TO_TICKS(1000), pdFALSE, NULL, heating_timer_callback);
    if (s_heat_data.heating_timer == NULL) {
        bt401_printf("Create heating timer failed!\r\n");
        vQueueDelete(s_heat_data.ctrl_queue);
        vSemaphoreDelete(s_heat_data.mutex);
        s_heat_data.ctrl_queue = NULL;
        s_heat_data.mutex = NULL;
        return false;
    }

    // 创建剩余时间更新定时器（周期定时器，1秒）
    s_heat_data.remain_timer = xTimerCreate("RemainTimer", pdMS_TO_TICKS(1000), pdTRUE, NULL, remain_timer_callback);
    if (s_heat_data.remain_timer == NULL) {
        bt401_printf("Create remain timer failed!\r\n");
        xTimerDelete(s_heat_data.heating_timer, 0);
        vQueueDelete(s_heat_data.ctrl_queue);
        vSemaphoreDelete(s_heat_data.mutex);
        s_heat_data.heating_timer = NULL;
        s_heat_data.ctrl_queue = NULL;
        s_heat_data.mutex = NULL;
        return false;
    }

    return true;
}

/**
 * @brief 初始化加热任务及相关资源
 * @retval true - 初始化成功, false - 初始化失败
 */
bool heat_task_init(void)
{
    // 检查是否已初始化
    if (s_heat_data.mutex != NULL) {
        bt401_printf("Heat task already initialized!\r\n");
        return true;
    }

    // 创建FreeRTOS资源
    if (!heat_create_freertos_resources()) {
        return false;
    }

    // 创建加热控制任务
    BaseType_t ret =
        xTaskCreate(heat_control_task, HEAT_TASK_NAME, HEAT_TASK_STACK_SIZE, NULL, HEAT_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        bt401_printf("Create heat task failed!\r\n");
        // 清理已创建的资源
        xTimerDelete(s_heat_data.remain_timer, 0);
        xTimerDelete(s_heat_data.heating_timer, 0);
        vQueueDelete(s_heat_data.ctrl_queue);
        vSemaphoreDelete(s_heat_data.mutex);
        // 重置资源句柄
        s_heat_data.remain_timer = NULL;
        s_heat_data.heating_timer = NULL;
        s_heat_data.ctrl_queue = NULL;
        s_heat_data.mutex = NULL;
        return false;
    }

    bt401_printf("Heat task init success!\r\n");
    return true;
}

/**
 * @brief 反初始化加热任务（释放资源）
 */
void heat_task_deinit(void)
{
    // 先停止所有加热操作
    heat_stop_all();

    // 删除任务（注：FreeRTOS中删除自身需用vTaskDelete(NULL)，外部删除需先确认任务句柄）
    // 此处简化处理，先清理资源
    if (s_heat_data.remain_timer) {
        xTimerDelete(s_heat_data.remain_timer, pdMS_TO_TICKS(100));
        s_heat_data.remain_timer = NULL;
    }
    if (s_heat_data.heating_timer) {
        xTimerDelete(s_heat_data.heating_timer, pdMS_TO_TICKS(100));
        s_heat_data.heating_timer = NULL;
    }
    if (s_heat_data.ctrl_queue) {
        vQueueDelete(s_heat_data.ctrl_queue);
        s_heat_data.ctrl_queue = NULL;
    }
    if (s_heat_data.mutex) {
        vSemaphoreDelete(s_heat_data.mutex);
        s_heat_data.mutex = NULL;
    }

    bt401_printf("Heat task deinit success!\r\n");
}

/**
 * @brief 设置加热运行状态
 * @param status 目标状态（HEAT_STATUS_STOP/HEAT_STATUS_RUNNING）
 * @retval true - 设置成功, false - 参数无效/队列发送失败
 */
bool heat_status_set(HeatStatus status)
{
    // 校验状态合法性
    if (status != HEAT_STATUS_STOP && status != HEAT_STATUS_RUNNING) {
        bt401_printf("Invalid heat status: %d\r\n", status);
        return false;
    }

    if (s_heat_data.ctrl_queue == NULL) {
        bt401_printf("Heat queue not initialized!\r\n");
        return false;
    }

    HeatMsg msg = {.type = MSG_SET_STATUS, .param.status = status};
    return (xQueueSend(s_heat_data.ctrl_queue, &msg, 0) == pdPASS);
}

/**
 * @brief 切换加热运行状态（运行↔停止）
 */
void heat_status_switch(void)
{
    HeatStatus curr_status = HEAT_STATUS_STOP;
    if (HEAT_LOCK()) {
        curr_status = s_heat_data.status;
        HEAT_UNLOCK();
    }
    heat_status_set(curr_status == HEAT_STATUS_RUNNING ? HEAT_STATUS_STOP : HEAT_STATUS_RUNNING);
}

/**
 * @brief 设置加热档位
 * @param level 目标档位（HEAT_LEVEL_1~HEAT_LEVEL_3）
 * @retval true - 设置成功, false - 参数无效/队列发送失败
 */
bool heat_level_set(HeatLevel level)
{
    if (level > HEAT_LEVEL_MAX) {
        bt401_printf("Invalid heat level: %d\r\n", level);
        return false;
    }

    if (s_heat_data.ctrl_queue == NULL) {
        bt401_printf("Heat queue not initialized!\r\n");
        return false;
    }

    HeatMsg msg = {.type = MSG_SET_LEVEL, .param.level = level};
    return (xQueueSend(s_heat_data.ctrl_queue, &msg, 0) == pdPASS);
}

/**
 * @brief 加热档位加1（到最大值后不再增加）
 */
void heat_level_up(void)
{
    HeatLevel new_level = HEAT_LEVEL_1;
    if (HEAT_LOCK()) {
        new_level = (s_heat_data.level < HEAT_LEVEL_MAX) ? (s_heat_data.level + 1) : HEAT_LEVEL_MAX;
        HEAT_UNLOCK();
    }

    // 到最大档位时蜂鸣提示
    if (new_level == HEAT_LEVEL_MAX) {
        buzzer_beep(HEAT_BUZZER_BEEP_COUNT);
    }

    heat_level_set(new_level);
}

/**
 * @brief 加热档位减1（到最小值后不再减少）
 */
void heat_level_down(void)
{
    HeatLevel new_level = HEAT_LEVEL_MAX;
    if (HEAT_LOCK()) {
        new_level = (s_heat_data.level > HEAT_LEVEL_1) ? (s_heat_data.level - 1) : HEAT_LEVEL_1;
        HEAT_UNLOCK();
    }

    // 到最小档位时蜂鸣提示
    if (new_level == HEAT_LEVEL_1) {
        buzzer_beep(HEAT_BUZZER_BEEP_COUNT);
    }

    heat_level_set(new_level);
}

/**
 * @brief 设置加热定时时间
 * @param minute 定时时间(分钟)，0表示取消定时，最大HEAT_MAX_TIMER_MINUTE
 * @retval true - 设置成功, false - 参数无效/队列发送失败
 */
bool heat_timer_set(uint16_t minute)
{
    if (minute > HEAT_MAX_TIMER_MINUTE) {
        bt401_printf("Invalid timer value: %d min (max: %d)\r\n", minute, HEAT_MAX_TIMER_MINUTE);
        return false;
    }

    if (s_heat_data.ctrl_queue == NULL) {
        bt401_printf("Heat queue not initialized!\r\n");
        return false;
    }

    HeatMsg msg = {.type = MSG_SET_TIMER, .param.minute = minute};
    return (xQueueSend(s_heat_data.ctrl_queue, &msg, 0) == pdPASS);
}

/**
 * @brief 获取当前加热状态
 * @retval 当前加热状态（HEAT_STATUS_STOP/HEAT_STATUS_RUNNING）
 */
HeatStatus heat_status_get(void)
{
    HeatStatus status = HEAT_STATUS_STOP;
    if (HEAT_LOCK()) {
        status = s_heat_data.status;
        HEAT_UNLOCK();
    }
    return status;
}

/**
 * @brief 获取当前加热档位
 * @retval 当前加热档位（HEAT_LEVEL_1~HEAT_LEVEL_3）
 */
HeatLevel heat_level_get(void)
{
    HeatLevel level = HEAT_LEVEL_1;
    if (HEAT_LOCK()) {
        level = s_heat_data.level;
        HEAT_UNLOCK();
    }
    return level;
}
/**
 * @brief 获取剩余定时时间
 * @retval 剩余时间(分钟)，0表示无定时
 */
uint16_t heat_remain_time_get(void)
{
    uint16_t remain_min = 0;
    if (HEAT_LOCK()) {
        remain_min = s_heat_data.remain_sec / 60;
        HEAT_UNLOCK();
    }
    return remain_min;
}
