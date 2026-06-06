/**
 * @file heat_service.c
 * @brief 加热控制服务 PID温控 + 定时 + 状态机 (CMSIS-RTOS v2)
 * @version 2.1
 */

#include "heat_service.h"
#include "app_config.h"
#include "app_handles.h"
#include "bsp_buzzer.h"
#include "bsp_heat.h"
#include "bsp_led.h"
#include "bsp_ntc.h"
#include "event_bus.h"
#include "pid.h"

/* ---- 蜂鸣器辅助 ---- */
static osTimerId_t s_beep_timer = NULL;

static void beep_timer_cb(void *arg) { (void)arg; bsp_buzzer_off(g_buzzer); }

static bool buzzer_beep_local(uint32_t duration_ms) {
    if (!s_beep_timer) {
        s_beep_timer = osTimerNew(beep_timer_cb, osTimerOnce, NULL, NULL);
        if (!s_beep_timer) return false;
    } else {
        if (osTimerIsRunning(s_beep_timer)) osTimerStop(s_beep_timer);
    }
    bsp_buzzer_on(g_buzzer);
    return (osTimerStart(s_beep_timer, CMSIS_TICKS(duration_ms)) == osOK);
}

/* ---- LED 时间指示 ---- */
static void led_time_select(uint32_t remain_sec) {
    static const uint32_t SEC_10 = 10U * 60U;
    static const uint32_t SEC_30 = 30U * 60U;
    bsp_led_set_mode(g_led, BSP_LED_10MIN, BSP_LED_MODE_OFF, 0);
    bsp_led_set_mode(g_led, BSP_LED_30MIN, BSP_LED_MODE_OFF, 0);
    bsp_led_set_mode(g_led, BSP_LED_60MIN, BSP_LED_MODE_OFF, 0);
    if (remain_sec > SEC_30)      bsp_led_set_mode(g_led, BSP_LED_60MIN, BSP_LED_MODE_ON, 0);
    else if (remain_sec > SEC_10) bsp_led_set_mode(g_led, BSP_LED_30MIN, BSP_LED_MODE_ON, 0);
    else if (remain_sec > 0)      bsp_led_set_mode(g_led, BSP_LED_10MIN, BSP_LED_MODE_ON, 0);
}

/* ---- 上传回调 ---- */
static heat_upload_cb_t s_upload_cb = NULL;
void heat_service_set_upload_handler(heat_upload_cb_t cb) { s_upload_cb = cb; }

static const HeatLevelTempMap s_heat_level_temp_map[] = {{HEAT_LEVEL_1, 35.0f}, {HEAT_LEVEL_2, 45.0f}, {HEAT_LEVEL_3, 55.0f}};

typedef enum { MSG_TIMER_EXPIRE = 0x01, MSG_UPDATE_REMAIN = 0x02, MSG_SET_STATUS = 0x03, MSG_SET_LEVEL = 0x04, MSG_SET_TIMER = 0x05 } HeatMsgType;

typedef struct { HeatMsgType type; union { HeatStatus status; HeatLevel level; uint16_t minute; } param; } HeatMsg;

typedef struct {
    HeatStatus status; HeatLevel level; float target_temperature; uint16_t set_time_min; uint32_t remain_sec; bool is_timing;
    pid_controller_t pid; osMutexId_t mutex; osMessageQueueId_t ctrl_queue; osTimerId_t heating_timer; osTimerId_t remain_timer;
    uint8_t ntc_fail_count;
} HeatTaskPrivData;

static HeatTaskPrivData s_heat_data = {.status = HEAT_STATUS_STOP, .level = HEAT_LEVEL_1, .target_temperature = 35.0f};

/* ---- 锁 ---- */
static bool heat_lock_internal(void) {
    bool ok = (osMutexAcquire(s_heat_data.mutex, CMSIS_TICKS(HEAT_MUTEX_TIMEOUT_MS)) == osOK);
    if (!ok) LOG_PRINTF("Heat mutex lock failed!\r\n");
    return ok;
}
#define HEAT_LOCK()   heat_lock_internal()
#define HEAT_UNLOCK() osMutexRelease(s_heat_data.mutex)

static float heat_get_temp_by_level(HeatLevel level) {
    for (uint8_t i = 0; i < BSP_ARRAY_SIZE(s_heat_level_temp_map); i++)
        if (s_heat_level_temp_map[i].level == level) return s_heat_level_temp_map[i].target_temp;
    return 35.0f;
}

static void heat_hw_turn_off(void) { bsp_heat_stop(g_heat); bsp_led_set_mode(g_led, BSP_LED_RF, BSP_LED_MODE_OFF, 0); }
static void heat_hw_turn_on(void)  { bsp_heat_start(g_heat); bsp_led_set_mode(g_led, BSP_LED_RF, BSP_LED_MODE_ON, 0); }

static void heat_stop_all(void) {
    HeatStatus old_status = HEAT_STATUS_STOP;
    if (HEAT_LOCK()) { old_status = s_heat_data.status; s_heat_data.status = HEAT_STATUS_STOP; s_heat_data.remain_sec = 0; s_heat_data.set_time_min = 0; s_heat_data.is_timing = false; s_heat_data.ntc_fail_count = 0; HEAT_UNLOCK(); }
    if (old_status == HEAT_STATUS_RUNNING) {
        heat_hw_turn_off(); led_time_select(0);
        if (s_heat_data.remain_timer)  osTimerStop(s_heat_data.remain_timer);
        if (s_heat_data.heating_timer) osTimerStop(s_heat_data.heating_timer);
        pid_reset(&s_heat_data.pid); if (s_upload_cb) s_upload_cb();
    }
}

static void heating_timer_cb(void *arg) { (void)arg; HeatMsg msg = {.type = MSG_TIMER_EXPIRE};  if (s_heat_data.ctrl_queue) osMessageQueuePut(s_heat_data.ctrl_queue, &msg, 0, 0); }
static void remain_timer_cb(void *arg)  { (void)arg; HeatMsg msg = {.type = MSG_UPDATE_REMAIN}; if (s_heat_data.ctrl_queue) osMessageQueuePut(s_heat_data.ctrl_queue, &msg, 0, 0); }

static void process_msg_timer_expire(void) {
    HeatStatus st = HEAT_STATUS_STOP; uint16_t tm = 0;
    if (HEAT_LOCK()) { st = s_heat_data.status; tm = s_heat_data.set_time_min; HEAT_UNLOCK(); }
    if (st == HEAT_STATUS_RUNNING && tm > 0) heat_stop_all();
}

static void process_msg_update_remain(void) {
    uint32_t remain = 0; HeatStatus st = HEAT_STATUS_STOP;
    if (HEAT_LOCK()) { st = s_heat_data.status; if (st == HEAT_STATUS_RUNNING && s_heat_data.remain_sec > 0) { s_heat_data.remain_sec--; remain = s_heat_data.remain_sec; } HEAT_UNLOCK(); }
    if (st == HEAT_STATUS_RUNNING) { if (remain > 0) led_time_select(remain); else heat_stop_all(); }
}

static void process_msg_set_status(HeatStatus new_status) {
    HeatStatus old = HEAT_STATUS_STOP; uint16_t tm = 0;
    if (HEAT_LOCK()) { old = s_heat_data.status; tm = s_heat_data.set_time_min; s_heat_data.status = new_status; HEAT_UNLOCK(); }
    if (new_status == HEAT_STATUS_STOP) { heat_stop_all(); event_publish(EVENT_HEAT_STATUS_CHANGE, HEAT_STATUS_STOP); }
    else if (new_status == HEAT_STATUS_RUNNING && old != HEAT_STATUS_RUNNING) {
        heat_hw_turn_on(); event_publish(EVENT_HEAT_STATUS_CHANGE, HEAT_STATUS_RUNNING);
        if (tm > 0) { if (HEAT_LOCK()) { s_heat_data.remain_sec = (uint32_t)tm * 60; s_heat_data.is_timing = true; HEAT_UNLOCK(); }
            if (s_heat_data.heating_timer) { osTimerStop(s_heat_data.heating_timer); osTimerStart(s_heat_data.heating_timer, CMSIS_TICKS(s_heat_data.remain_sec * 1000)); }
            if (s_heat_data.remain_timer && !osTimerIsRunning(s_heat_data.remain_timer)) osTimerStart(s_heat_data.remain_timer, CMSIS_TICKS(1000)); }
    }
}

static void process_msg_set_level(HeatLevel new_level) {
    if (new_level > HEAT_LEVEL_MAX) return;
    float t = heat_get_temp_by_level(new_level);
    if (HEAT_LOCK()) { s_heat_data.level = new_level; s_heat_data.target_temperature = t; HEAT_UNLOCK(); }
    LOG_PRINTF("Heat level set to %d, target: %.1fC\r\n", new_level, t);
}

static void process_msg_set_timer(uint16_t minute) {
    if (minute > HEAT_MAX_TIMER_MIN) return;
    HeatStatus st = HEAT_STATUS_STOP; bool timing = (minute > 0); uint32_t remain = timing ? (uint32_t)minute * 60 : 0;
    if (HEAT_LOCK()) { st = s_heat_data.status; s_heat_data.set_time_min = minute; s_heat_data.is_timing = timing; s_heat_data.remain_sec = remain; HEAT_UNLOCK(); }
    led_time_select(remain);
    if (timing && st == HEAT_STATUS_RUNNING) { if (s_heat_data.heating_timer) { osTimerStop(s_heat_data.heating_timer); osTimerStart(s_heat_data.heating_timer, CMSIS_TICKS(remain * 1000)); } if (s_heat_data.remain_timer && !osTimerIsRunning(s_heat_data.remain_timer)) osTimerStart(s_heat_data.remain_timer, CMSIS_TICKS(1000)); }
    else { if (s_heat_data.heating_timer) osTimerStop(s_heat_data.heating_timer); if (s_heat_data.remain_timer) osTimerStop(s_heat_data.remain_timer); }
}

static void process_heat_message(const HeatMsg *msg) {
    if (!msg) return;
    switch (msg->type) { case MSG_TIMER_EXPIRE: process_msg_timer_expire(); break; case MSG_UPDATE_REMAIN: process_msg_update_remain(); break; case MSG_SET_STATUS: process_msg_set_status(msg->param.status); break; case MSG_SET_LEVEL: process_msg_set_level(msg->param.level); break; case MSG_SET_TIMER: process_msg_set_timer(msg->param.minute); break; default: break; }
    if (s_upload_cb) s_upload_cb();
}

static bool heat_read_ntc_temp(float *temp) {
    if (!temp) return false;
    if (bsp_ntc_read(g_ntc, temp) == BSP_OK) { s_heat_data.ntc_fail_count = 0; return true; }
    s_heat_data.ntc_fail_count++; return false;
}

static void heat_pid_control(void) {
    if (bsp_ntc_is_fault(g_ntc)) { heat_stop_all(); s_heat_data.ntc_fail_count = 0; event_publish(EVENT_NTC_FAULT, 0); return; }
    float curr = 0.0f; if (!heat_read_ntc_temp(&curr)) { if (s_heat_data.ntc_fail_count >= HEAT_NTC_FAIL_THRESHOLD) { heat_stop_all(); s_heat_data.ntc_fail_count = 0; } return; }
    uint32_t now = (osKernelGetTickCount() * 1000U) / osKernelGetTickFreq();
    uint16_t out = pid_calc_with_time(&s_heat_data.pid, curr, s_heat_data.target_temperature, now);
    bsp_heat_set_power(g_heat, out);
    int16_t ts = (int16_t)(curr * 10.0f + 0.5f), gs = (int16_t)(s_heat_data.target_temperature * 10.0f + 0.5f);
    LOG_PRINTF("Temp: %d.%d Target: %d.%d PWM: %u%%\r\n", ts / 10, ts % 10, gs / 10, gs % 10, out);
}

static void heat_control_task(void *arg) {
    (void)arg;
    uint32_t period_ticks = CMSIS_TICKS(HEAT_CONTROL_PERIOD_MS);
    uint32_t last = osKernelGetTickCount();
    pid_init(&s_heat_data.pid, HEAT_PID_KP, HEAT_PID_KI, HEAT_PID_KD, HEAT_PID_DEADBAND, HEAT_PID_OUT_MIN, HEAT_PID_OUT_MAX);
    pid_set_integral_params(&s_heat_data.pid, HEAT_PID_DEADBAND / 2, HEAT_PID_INTEGRAL_LIMIT);
    pid_set_derivative_filter(&s_heat_data.pid, HEAT_PID_D_FILTER);
    for (;;) {
        HeatMsg msg; while (osMessageQueueGet(s_heat_data.ctrl_queue, &msg, NULL, 0) == osOK) process_heat_message(&msg);
        HeatStatus st = HEAT_STATUS_STOP; if (HEAT_LOCK()) { st = s_heat_data.status; HEAT_UNLOCK(); }
        if (st == HEAT_STATUS_RUNNING) heat_pid_control(); else { pid_reset(&s_heat_data.pid); s_heat_data.ntc_fail_count = 0; }
        bsp_led_poll(g_led, HEAT_CONTROL_PERIOD_MS);
        last += period_ticks; osDelayUntil(last);
    }
}

static bool heat_create_resources(void) {
    s_heat_data.mutex = osMutexNew(NULL); if (!s_heat_data.mutex) return false;
    s_heat_data.ctrl_queue = osMessageQueueNew(HEAT_QUEUE_LEN, sizeof(HeatMsg), NULL); if (!s_heat_data.ctrl_queue) { osMutexDelete(s_heat_data.mutex); s_heat_data.mutex = NULL; return false; }
    s_heat_data.heating_timer = osTimerNew(heating_timer_cb, osTimerOnce, NULL, NULL); if (!s_heat_data.heating_timer) { osMessageQueueDelete(s_heat_data.ctrl_queue); osMutexDelete(s_heat_data.mutex); s_heat_data.ctrl_queue = NULL; s_heat_data.mutex = NULL; return false; }
    s_heat_data.remain_timer = osTimerNew(remain_timer_cb, osTimerPeriodic, NULL, NULL); if (!s_heat_data.remain_timer) { osTimerDelete(s_heat_data.heating_timer); osMessageQueueDelete(s_heat_data.ctrl_queue); osMutexDelete(s_heat_data.mutex); s_heat_data.ctrl_queue = NULL; s_heat_data.mutex = NULL; return false; }
    return true;
}

bool heat_task_init(void) {
    if (s_heat_data.mutex) return true; if (!heat_create_resources()) return false;
    const osThreadAttr_t attr = {.name = TASK_HEAT_NAME, .stack_size = TASK_HEAT_STACK, .priority = (osPriority_t)TASK_HEAT_PRIORITY};
    if (!osThreadNew(heat_control_task, NULL, &attr)) { osTimerDelete(s_heat_data.remain_timer); osTimerDelete(s_heat_data.heating_timer); osMessageQueueDelete(s_heat_data.ctrl_queue); osMutexDelete(s_heat_data.mutex); s_heat_data.remain_timer = NULL; s_heat_data.heating_timer = NULL; s_heat_data.ctrl_queue = NULL; s_heat_data.mutex = NULL; return false; }
    return true;
}

void heat_task_deinit(void) { heat_stop_all(); if (s_heat_data.remain_timer) { osTimerDelete(s_heat_data.remain_timer); s_heat_data.remain_timer = NULL; } if (s_heat_data.heating_timer) { osTimerDelete(s_heat_data.heating_timer); s_heat_data.heating_timer = NULL; } if (s_heat_data.ctrl_queue) { osMessageQueueDelete(s_heat_data.ctrl_queue); s_heat_data.ctrl_queue = NULL; } if (s_heat_data.mutex) { osMutexDelete(s_heat_data.mutex); s_heat_data.mutex = NULL; } }

bool heat_status_set(HeatStatus status) { if (status != HEAT_STATUS_STOP && status != HEAT_STATUS_RUNNING) return false; if (!s_heat_data.ctrl_queue) return false; HeatMsg msg = {.type = MSG_SET_STATUS, .param.status = status}; return (osMessageQueuePut(s_heat_data.ctrl_queue, &msg, 0, 0) == osOK); }

void heat_status_switch(void) { HeatStatus st = HEAT_STATUS_STOP; if (HEAT_LOCK()) { st = s_heat_data.status; HEAT_UNLOCK(); } heat_status_set(st == HEAT_STATUS_RUNNING ? HEAT_STATUS_STOP : HEAT_STATUS_RUNNING); }

bool heat_level_set(HeatLevel level) { if (level > HEAT_LEVEL_MAX) return false; if (!s_heat_data.ctrl_queue) return false; HeatMsg msg = {.type = MSG_SET_LEVEL, .param.level = level}; return (osMessageQueuePut(s_heat_data.ctrl_queue, &msg, 0, 0) == osOK); }

void heat_level_up(void) { HeatLevel lv = HEAT_LEVEL_1; if (HEAT_LOCK()) { lv = (s_heat_data.level < HEAT_LEVEL_MAX) ? (s_heat_data.level + 1) : HEAT_LEVEL_MAX; HEAT_UNLOCK(); } if (lv == HEAT_LEVEL_MAX) buzzer_beep_local(HEAT_BUZZER_BEEP_COUNT); heat_level_set(lv); }

void heat_level_down(void) { HeatLevel lv = HEAT_LEVEL_MAX; if (HEAT_LOCK()) { lv = (s_heat_data.level > HEAT_LEVEL_1) ? (s_heat_data.level - 1) : HEAT_LEVEL_1; HEAT_UNLOCK(); } if (lv == HEAT_LEVEL_1) buzzer_beep_local(HEAT_BUZZER_BEEP_COUNT); heat_level_set(lv); }

bool heat_timer_set(uint16_t minute) { if (minute > HEAT_MAX_TIMER_MIN) return false; if (!s_heat_data.ctrl_queue) return false; HeatMsg msg = {.type = MSG_SET_TIMER, .param.minute = minute}; return (osMessageQueuePut(s_heat_data.ctrl_queue, &msg, 0, 0) == osOK); }

HeatStatus heat_status_get(void) { HeatStatus st = HEAT_STATUS_STOP; if (HEAT_LOCK()) { st = s_heat_data.status; HEAT_UNLOCK(); } return st; }
HeatLevel heat_level_get(void)  { HeatLevel lv = HEAT_LEVEL_1; if (HEAT_LOCK()) { lv = s_heat_data.level; HEAT_UNLOCK(); } return lv; }
uint16_t heat_remain_time_get(void) { uint16_t m = 0; if (HEAT_LOCK()) { m = s_heat_data.remain_sec / 60; HEAT_UNLOCK(); } return m; }
