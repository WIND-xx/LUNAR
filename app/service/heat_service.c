/**
 * @file heat_service.c
 * @brief 加热控制服务（PID温控 + 定时 + 状态机）
 * @version 1.0
 */

#include "heat_service.h"
#include "../core/app_config.h"
#include "bt401.h"
#include "buzzer.h"
#include "core/event_bus.h"
#include "heat.h"
#include "led.h"
#include "ntc.h"
#include "pid.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

static heat_upload_cb_t s_upload_cb = NULL;
void heat_service_set_upload_handler(heat_upload_cb_t cb) { s_upload_cb = cb; }

/* ---- 档位-温度映射表 ---- */
static const HeatLevelTempMap s_heat_level_temp_map[] = {
    {HEAT_LEVEL_1, 35.0f}, {HEAT_LEVEL_2, 45.0f}, {HEAT_LEVEL_3, 55.0f}};

/* ---- 内部消息类型 ---- */
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

/* ---- 私有数据 ---- */
typedef struct {
    HeatStatus status;
    HeatLevel level;
    float target_temperature;
    uint16_t set_time_min;
    uint32_t remain_sec;
    bool is_timing;
    pid_controller_t pid;
    SemaphoreHandle_t mutex;
    QueueHandle_t ctrl_queue;
    TimerHandle_t heating_timer;
    TimerHandle_t remain_timer;
    uint8_t ntc_fail_count;
} HeatTaskPrivData;

static HeatTaskPrivData s_heat_data = {
    .status = HEAT_STATUS_STOP,
    .level = HEAT_LEVEL_1,
    .target_temperature = 35.0f,
    .set_time_min = 0,
    .remain_sec = 0,
    .is_timing = false,
    .mutex = NULL,
    .ctrl_queue = NULL,
    .heating_timer = NULL,
    .remain_timer = NULL,
    .ntc_fail_count = 0
};

/* ---- 锁 ---- */
static bool heat_lock_internal(void)
{
    bool ok = (xSemaphoreTake(s_heat_data.mutex, pdMS_TO_TICKS(HEAT_MUTEX_TIMEOUT_MS)) == pdTRUE);
    if (!ok) bt401_printf("Heat mutex lock failed!\r\n");
    return ok;
}
#define HEAT_LOCK()   heat_lock_internal()
#define HEAT_UNLOCK() xSemaphoreGive(s_heat_data.mutex)

/* ---- 档位查表 ---- */
static float heat_get_temp_by_level(HeatLevel level)
{
    for (uint8_t i = 0; i < sizeof(s_heat_level_temp_map) / sizeof(HeatLevelTempMap); i++) {
        if (s_heat_level_temp_map[i].level == level)
            return s_heat_level_temp_map[i].target_temp;
    }
    return 35.0f;
}

/* ---- 硬件同步 ---- */
static void heat_hw_turn_off(void)
{
    if (heat_stop()) led_set_mode(LED_RF, LED_MODE_OFF, 0);
}
static void heat_hw_turn_on(void)
{
    if (heat_start()) led_set_mode(LED_RF, LED_MODE_ON, 0);
}

/* ---- 停止所有加热 ---- */
static void heat_stop_all(void)
{
    HeatStatus old_status = HEAT_STATUS_STOP;
    if (HEAT_LOCK()) {
        old_status = s_heat_data.status;
        s_heat_data.status = HEAT_STATUS_STOP;
        s_heat_data.remain_sec = 0;
        s_heat_data.set_time_min = 0;
        s_heat_data.is_timing = false;
        s_heat_data.ntc_fail_count = 0;
        HEAT_UNLOCK();
    }
    if (old_status == HEAT_STATUS_RUNNING) {
        heat_hw_turn_off();
        led_time_select(0);
        if (s_heat_data.remain_timer)  xTimerStop(s_heat_data.remain_timer, 0);
        if (s_heat_data.heating_timer) xTimerStop(s_heat_data.heating_timer, 0);
        pid_reset(&s_heat_data.pid);
        if (s_upload_cb) s_upload_cb();
    }
}

/* ---- 定时器回调 ---- */
static void heating_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    HeatMsg msg = {.type = MSG_TIMER_EXPIRE};
    if (s_heat_data.ctrl_queue) xQueueSend(s_heat_data.ctrl_queue, &msg, 0);
}
static void remain_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    HeatMsg msg = {.type = MSG_UPDATE_REMAIN};
    if (s_heat_data.ctrl_queue) xQueueSend(s_heat_data.ctrl_queue, &msg, 0);
}

/* ---- 消息处理 ---- */
static void process_msg_timer_expire(void)
{
    HeatStatus st = HEAT_STATUS_STOP;
    uint16_t tm = 0;
    if (HEAT_LOCK()) { st = s_heat_data.status; tm = s_heat_data.set_time_min; HEAT_UNLOCK(); }
    if (st == HEAT_STATUS_RUNNING && tm > 0) heat_stop_all();
}

static void process_msg_update_remain(void)
{
    uint32_t remain = 0;
    HeatStatus st = HEAT_STATUS_STOP;
    if (HEAT_LOCK()) {
        st = s_heat_data.status;
        if (st == HEAT_STATUS_RUNNING && s_heat_data.remain_sec > 0) {
            s_heat_data.remain_sec--;
            remain = s_heat_data.remain_sec;
        }
        HEAT_UNLOCK();
    }
    if (st == HEAT_STATUS_RUNNING) {
        if (remain > 0) led_time_select(remain);
        else heat_stop_all();
    }
}

static void process_msg_set_status(HeatStatus new_status)
{
    HeatStatus old = HEAT_STATUS_STOP;
    uint16_t tm = 0;
    if (HEAT_LOCK()) { old = s_heat_data.status; tm = s_heat_data.set_time_min; s_heat_data.status = new_status; HEAT_UNLOCK(); }

    if (new_status == HEAT_STATUS_STOP) {
        heat_stop_all();
        event_publish(EVENT_HEAT_STATUS_CHANGE, HEAT_STATUS_STOP);
    } else if (new_status == HEAT_STATUS_RUNNING && old != HEAT_STATUS_RUNNING) {
        heat_hw_turn_on();
        event_publish(EVENT_HEAT_STATUS_CHANGE, HEAT_STATUS_RUNNING);
        if (tm > 0) {
            if (HEAT_LOCK()) { s_heat_data.remain_sec = (uint32_t)tm * 60; s_heat_data.is_timing = true; HEAT_UNLOCK(); }
            if (s_heat_data.heating_timer) {
                xTimerChangePeriod(s_heat_data.heating_timer, pdMS_TO_TICKS(s_heat_data.remain_sec * 1000), 0);
                xTimerStart(s_heat_data.heating_timer, 0);
            }
            if (s_heat_data.remain_timer && !xTimerIsTimerActive(s_heat_data.remain_timer))
                xTimerStart(s_heat_data.remain_timer, 0);
        }
    }
}

static void process_msg_set_level(HeatLevel new_level)
{
    if (new_level > HEAT_LEVEL_MAX) { bt401_printf("Invalid heat level: %d\r\n", new_level); return; }
    float t = heat_get_temp_by_level(new_level);
    if (HEAT_LOCK()) { s_heat_data.level = new_level; s_heat_data.target_temperature = t; HEAT_UNLOCK(); }
    bt401_printf("Heat level set to %d, target: %.1fC\r\n", new_level, t);
}

static void process_msg_set_timer(uint16_t minute)
{
    if (minute > HEAT_MAX_TIMER_MIN) { bt401_printf("Invalid timer: %d min (max: %d)\r\n", minute, HEAT_MAX_TIMER_MIN); return; }
    HeatStatus st = HEAT_STATUS_STOP;
    bool timing = (minute > 0);
    uint32_t remain = timing ? (uint32_t)minute * 60 : 0;
    if (HEAT_LOCK()) { st = s_heat_data.status; s_heat_data.set_time_min = minute; s_heat_data.is_timing = timing; s_heat_data.remain_sec = remain; HEAT_UNLOCK(); }
    led_time_select(remain);
    if (timing && st == HEAT_STATUS_RUNNING) {
        if (s_heat_data.heating_timer) {
            xTimerStop(s_heat_data.heating_timer, 0);
            xTimerChangePeriod(s_heat_data.heating_timer, pdMS_TO_TICKS(remain * 1000), 0);
            xTimerStart(s_heat_data.heating_timer, 0);
        }
        if (s_heat_data.remain_timer && !xTimerIsTimerActive(s_heat_data.remain_timer))
            xTimerStart(s_heat_data.remain_timer, 0);
    } else {
        if (s_heat_data.heating_timer) xTimerStop(s_heat_data.heating_timer, 0);
        if (s_heat_data.remain_timer)  xTimerStop(s_heat_data.remain_timer, 0);
    }
}

static void process_heat_message(const HeatMsg *msg)
{
    if (!msg) return;
    switch (msg->type) {
    case MSG_TIMER_EXPIRE:  process_msg_timer_expire(); break;
    case MSG_UPDATE_REMAIN: process_msg_update_remain(); break;
    case MSG_SET_STATUS:    process_msg_set_status(msg->param.status); break;
    case MSG_SET_LEVEL:     process_msg_set_level(msg->param.level); break;
    case MSG_SET_TIMER:     process_msg_set_timer(msg->param.minute); break;
    default: bt401_printf("Unknown heat msg: %d\r\n", msg->type); break;
    }
    if (s_upload_cb) s_upload_cb();
}

/* ---- NTC读取 ---- */
static bool heat_read_ntc_temp(float *temp)
{
    if (!temp) return false;
    if (ntc_read(temp) == 0) { s_heat_data.ntc_fail_count = 0; return true; }
    s_heat_data.ntc_fail_count++;
    bt401_printf("NTC read fail, count: %d\r\n", s_heat_data.ntc_fail_count);
    return false;
}

/* ---- PID控制 ---- */
static void heat_pid_control(void)
{
    if (ntc_is_fault()) {
        heat_stop_all();
        s_heat_data.ntc_fail_count = 0;
        event_publish(EVENT_NTC_FAULT, 0);
        return;
    }
    float curr = 0.0f;
    if (!heat_read_ntc_temp(&curr)) {
        if (s_heat_data.ntc_fail_count >= HEAT_NTC_FAIL_THRESHOLD) {
            heat_stop_all();
            s_heat_data.ntc_fail_count = 0;
        }
        return;
    }
    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    uint16_t out = pid_calc_with_time(&s_heat_data.pid, curr, s_heat_data.target_temperature, now);
    heat_set_power(out);
    int16_t ts = (int16_t)(curr * 10.0f + 0.5f);
    int16_t gs = (int16_t)(s_heat_data.target_temperature * 10.0f + 0.5f);
    bt401_printf("Temp: %d.%d Target: %d.%d PWM: %u%%\r\n",
        ts / 10, ts % 10, gs / 10, gs % 10, out);
}

/* ---- 主任务 ---- */
static void heat_control_task(void *arg)
{
    (void)arg;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(HEAT_CONTROL_PERIOD_MS);

    pid_init(&s_heat_data.pid, HEAT_PID_KP, HEAT_PID_KI, HEAT_PID_KD,
             HEAT_PID_DEADBAND, HEAT_PID_OUT_MIN, HEAT_PID_OUT_MAX);
    pid_set_integral_params(&s_heat_data.pid, HEAT_PID_DEADBAND / 2, HEAT_PID_INTEGRAL_LIMIT);
    pid_set_derivative_filter(&s_heat_data.pid, HEAT_PID_D_FILTER);
    heat_init();

    for (;;) {
        HeatMsg msg;
        while (xQueueReceive(s_heat_data.ctrl_queue, &msg, 0) == pdTRUE)
            process_heat_message(&msg);

        HeatStatus st = HEAT_STATUS_STOP;
        if (HEAT_LOCK()) { st = s_heat_data.status; HEAT_UNLOCK(); }

        if (st == HEAT_STATUS_RUNNING) heat_pid_control();
        else { pid_reset(&s_heat_data.pid); s_heat_data.ntc_fail_count = 0; }

        vTaskDelayUntil(&xLastWakeTime, period);
    }
}

static bool heat_create_freertos_resources(void)
{
    s_heat_data.mutex = xSemaphoreCreateMutex();
    if (!s_heat_data.mutex) { bt401_printf("Create heat mutex failed!\r\n"); return false; }

    s_heat_data.ctrl_queue = xQueueCreate(HEAT_QUEUE_LEN, sizeof(HeatMsg));
    if (!s_heat_data.ctrl_queue) {
        bt401_printf("Create heat queue failed!\r\n");
        vSemaphoreDelete(s_heat_data.mutex); s_heat_data.mutex = NULL; return false;
    }

    s_heat_data.heating_timer = xTimerCreate("HeatTimer", pdMS_TO_TICKS(1000), pdFALSE, NULL, heating_timer_callback);
    if (!s_heat_data.heating_timer) {
        bt401_printf("Create heating timer failed!\r\n");
        vQueueDelete(s_heat_data.ctrl_queue); vSemaphoreDelete(s_heat_data.mutex);
        s_heat_data.ctrl_queue = NULL; s_heat_data.mutex = NULL; return false;
    }

    s_heat_data.remain_timer = xTimerCreate("RemainTimer", pdMS_TO_TICKS(1000), pdTRUE, NULL, remain_timer_callback);
    if (!s_heat_data.remain_timer) {
        bt401_printf("Create remain timer failed!\r\n");
        xTimerDelete(s_heat_data.heating_timer, 0);
        vQueueDelete(s_heat_data.ctrl_queue); vSemaphoreDelete(s_heat_data.mutex);
        s_heat_data.heating_timer = NULL; s_heat_data.ctrl_queue = NULL; s_heat_data.mutex = NULL; return false;
    }
    return true;
}

bool heat_task_init(void)
{
    if (s_heat_data.mutex) { bt401_printf("Heat task already initialized!\r\n"); return true; }
    if (!heat_create_freertos_resources()) return false;

    BaseType_t ret = xTaskCreate(heat_control_task, TASK_HEAT_NAME, TASK_HEAT_STACK, NULL, TASK_HEAT_PRIORITY, NULL);
    if (ret != pdPASS) {
        bt401_printf("Create heat task failed!\r\n");
        xTimerDelete(s_heat_data.remain_timer, 0);
        xTimerDelete(s_heat_data.heating_timer, 0);
        vQueueDelete(s_heat_data.ctrl_queue);
        vSemaphoreDelete(s_heat_data.mutex);
        s_heat_data.remain_timer = NULL; s_heat_data.heating_timer = NULL;
        s_heat_data.ctrl_queue = NULL; s_heat_data.mutex = NULL;
        return false;
    }
    bt401_printf("Heat task init success!\r\n");
    return true;
}

void heat_task_deinit(void)
{
    heat_stop_all();
    if (s_heat_data.remain_timer)  { xTimerDelete(s_heat_data.remain_timer, pdMS_TO_TICKS(100)); s_heat_data.remain_timer = NULL; }
    if (s_heat_data.heating_timer) { xTimerDelete(s_heat_data.heating_timer, pdMS_TO_TICKS(100)); s_heat_data.heating_timer = NULL; }
    if (s_heat_data.ctrl_queue)    { vQueueDelete(s_heat_data.ctrl_queue); s_heat_data.ctrl_queue = NULL; }
    if (s_heat_data.mutex)         { vSemaphoreDelete(s_heat_data.mutex); s_heat_data.mutex = NULL; }
    bt401_printf("Heat task deinit success!\r\n");
}

bool heat_status_set(HeatStatus status)
{
    if (status != HEAT_STATUS_STOP && status != HEAT_STATUS_RUNNING) {
        bt401_printf("Invalid heat status: %d\r\n", status); return false;
    }
    if (!s_heat_data.ctrl_queue) { bt401_printf("Heat queue not initialized!\r\n"); return false; }
    HeatMsg msg = {.type = MSG_SET_STATUS, .param.status = status};
    return (xQueueSend(s_heat_data.ctrl_queue, &msg, 0) == pdPASS);
}

void heat_status_switch(void)
{
    HeatStatus st = HEAT_STATUS_STOP;
    if (HEAT_LOCK()) { st = s_heat_data.status; HEAT_UNLOCK(); }
    heat_status_set(st == HEAT_STATUS_RUNNING ? HEAT_STATUS_STOP : HEAT_STATUS_RUNNING);
}

bool heat_level_set(HeatLevel level)
{
    if (level > HEAT_LEVEL_MAX) { bt401_printf("Invalid heat level: %d\r\n", level); return false; }
    if (!s_heat_data.ctrl_queue) { bt401_printf("Heat queue not initialized!\r\n"); return false; }
    HeatMsg msg = {.type = MSG_SET_LEVEL, .param.level = level};
    return (xQueueSend(s_heat_data.ctrl_queue, &msg, 0) == pdPASS);
}

void heat_level_up(void)
{
    HeatLevel lv = HEAT_LEVEL_1;
    if (HEAT_LOCK()) { lv = (s_heat_data.level < HEAT_LEVEL_MAX) ? (s_heat_data.level + 1) : HEAT_LEVEL_MAX; HEAT_UNLOCK(); }
    if (lv == HEAT_LEVEL_MAX) buzzer_beep(HEAT_BUZZER_BEEP_COUNT);
    heat_level_set(lv);
}

void heat_level_down(void)
{
    HeatLevel lv = HEAT_LEVEL_MAX;
    if (HEAT_LOCK()) { lv = (s_heat_data.level > HEAT_LEVEL_1) ? (s_heat_data.level - 1) : HEAT_LEVEL_1; HEAT_UNLOCK(); }
    if (lv == HEAT_LEVEL_1) buzzer_beep(HEAT_BUZZER_BEEP_COUNT);
    heat_level_set(lv);
}

bool heat_timer_set(uint16_t minute)
{
    if (minute > HEAT_MAX_TIMER_MIN) {
        bt401_printf("Invalid timer: %d min (max: %d)\r\n", minute, HEAT_MAX_TIMER_MIN); return false;
    }
    if (!s_heat_data.ctrl_queue) { bt401_printf("Heat queue not initialized!\r\n"); return false; }
    HeatMsg msg = {.type = MSG_SET_TIMER, .param.minute = minute};
    return (xQueueSend(s_heat_data.ctrl_queue, &msg, 0) == pdPASS);
}

HeatStatus heat_status_get(void)
{
    HeatStatus st = HEAT_STATUS_STOP;
    if (HEAT_LOCK()) { st = s_heat_data.status; HEAT_UNLOCK(); }
    return st;
}

HeatLevel heat_level_get(void)
{
    HeatLevel lv = HEAT_LEVEL_1;
    if (HEAT_LOCK()) { lv = s_heat_data.level; HEAT_UNLOCK(); }
    return lv;
}

uint16_t heat_remain_time_get(void)
{
    uint16_t min = 0;
    if (HEAT_LOCK()) { min = s_heat_data.remain_sec / 60; HEAT_UNLOCK(); }
    return min;
}
