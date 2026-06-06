/**
 * @file app_main.c
 * @brief 唯一编排者：初始化所有 BSP 句柄 + 注册回调 + 订阅事件
 * @version 4.0
 */

#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "app_handles.h"
#include "event_bus.h"
#include "gpio.h"

/* BSP 头文件（仅用于初始化配置） */
#include "bsp_bt401.h"
#include "bsp_buzzer.h"
#include "bsp_heat.h"
#include "bsp_key.h"
#include "bsp_led.h"
#include "bsp_ntc.h"
#include "bsp_rtc.h"

/* 服务层接口 */
#include "service/ble_service.h"
#include "service/heat_service.h"
#include "service/key_service.h"

/* 协议层接口 */
#include "at_parser.h"
#include "modbus_slave.h"

/*============================================================================
 * 全局 BSP 句柄定义
 *============================================================================*/
bsp_bt401_t*  g_bt401  = NULL;
bsp_buzzer_t* g_buzzer = NULL;
bsp_heat_t*   g_heat   = NULL;
bsp_key_t*    g_key    = NULL;
bsp_led_t*    g_led    = NULL;
bsp_ntc_t*    g_ntc    = NULL;
bsp_rtc_t*    g_rtc    = NULL;

/*============================================================================
 * BSP 初始化
 *============================================================================*/

/** LED 硬件配置表（顺序对应 bsp_led_index_t） */
static const bsp_led_hw_t led_hw_table[BSP_LED_COUNT] = {
    [BSP_LED_MUSIC] = {LED4_GPIO_Port, LED4_Pin, GPIO_PIN_RESET},
    [BSP_LED_BT]    = {LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET},
    [BSP_LED_10MIN] = {LED6_GPIO_Port, LED6_Pin, GPIO_PIN_RESET},
    [BSP_LED_30MIN] = {LED5_GPIO_Port, LED5_Pin, GPIO_PIN_RESET},
    [BSP_LED_60MIN] = {LED2_GPIO_Port, LED2_Pin, GPIO_PIN_RESET},
    [BSP_LED_RF]    = {LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET},
    [BSP_LED_B]     = {LED_B_GPIO_Port, LED_B_Pin, GPIO_PIN_RESET},
};

static void bsp_all_init(void)
{
    /* ---- LED ---- */
    bsp_led_config_t led_cfg = {
        .hw_table = led_hw_table,
        .count    = BSP_LED_COUNT,
    };
    bsp_led_init(&g_led, &led_cfg);
    bsp_led_set_mode(g_led, BSP_LED_B, BSP_LED_MODE_ON, 0); /* 默认亮蓝灯 */

    /* ---- Buzzer ---- */
    bsp_buzzer_config_t buzzer_cfg = {
        .port         = BEEP_GPIO_Port,
        .pin          = BEEP_Pin,
        .active_level = GPIO_PIN_SET,
    };
    bsp_buzzer_init(&g_buzzer, &buzzer_cfg);

    /* ---- BT401 ---- */
    extern UART_HandleTypeDef huart3;
    bsp_bt401_config_t bt401_cfg = { .huart = &huart3 };
    bsp_bt401_init(&g_bt401, &bt401_cfg);

    /* ---- Heat (PWM) ---- */
    extern TIM_HandleTypeDef htim1;
    bsp_heat_config_t heat_cfg = {
        .htim       = &htim1,
        .channel    = TIM_CHANNEL_4,
        .freq_hz    = 1000,
        .resolution = (uint32_t)htim1.Init.Period + 1,
    };
    bsp_heat_init(&g_heat, &heat_cfg);

    /* ---- NTC ---- */
    extern ADC_HandleTypeDef hadc1;
    bsp_ntc_config_t ntc_cfg = { .hadc = &hadc1, .temp_offset = 0.0f };
    bsp_ntc_init(&g_ntc, &ntc_cfg);

    /* ---- RTC ---- */
    extern RTC_HandleTypeDef hrtc;
    bsp_rtc_config_t rtc_cfg = { .hrtc = &hrtc };
    bsp_rtc_init(&g_rtc, &rtc_cfg);
}

/*============================================================================
 * BLE帧 → 协议层分发
 *============================================================================*/
static void on_ble_frame(const uint8_t* data, uint16_t len, bool is_modbus)
{
    if (is_modbus) {
        protocol_handle_request(data, len);
    } else {
        decode_at_command((uint8_t*)data, len);
    }
}

/*============================================================================
 * 加热状态上传 → 协议层
 *============================================================================*/
static void on_heat_upload(void)
{
    protocol_upload_heating_status();
}

/*============================================================================
 * 按键事件 → 业务层路由
 *============================================================================*/
static void on_key_event(EventType event, uint32_t param)
{
    uint8_t key_id = (uint8_t)param;

    if (event == EVENT_KEY_SHORT_PRESS) {
        switch (key_id) {
            case KEY_MUSIC:      ble_mode(BT_MODE_MUSIC); break;
            case KEY_BLUETOOTH:  ble_mode(BT_MODE_BT);    break;
            case KEY_PLAY_PAUSE: music_switch();           break;
            case KEY_PREV:       music_prev();             break;
            case KEY_NEXT:       music_next();             break;
            case KEY_VOL_DOWN:   music_volume_control(VOL_DIR_DOWN); break;
            case KEY_VOL_UP:     music_volume_control(VOL_DIR_UP);   break;
            case KEY_HEAT:       heat_status_switch();     break;
            case KEY_HEAT_PLUS:  heat_level_down();        break;
            case KEY_HEAT_MINUS: heat_level_up();          break;
            case KEY_MIN10:      heat_timer_set(10);       break;
            case KEY_MIN30:      heat_timer_set(30);       break;
            case KEY_MIN60:      heat_timer_set(60);       break;
            default: break;
        }
    } else if (event == EVENT_KEY_LONG_PRESS) {
        switch (key_id) {
            case KEY_MUSIC:
            case KEY_BLUETOOTH:
                ble_mode(BT_MODE_OFF);
                break;
            case KEY_MIN10:
            case KEY_MIN30:
            case KEY_MIN60:
                heat_timer_set(0);
                break;
            default: break;
        }
    }
}

/*============================================================================
 * Modbus 寄存器回调
 *============================================================================*/
static void on_reg_write(RegisterID reg, uint16_t value)
{
    switch (reg) {
        case REG_HEATING_STATUS: heat_status_set(value ? HEAT_STATUS_RUNNING : HEAT_STATUS_STOP); break;
        case REG_HEATING_LEVEL:  heat_level_set((HeatLevel)value);  break;
        case REG_HEATING_TIMER:  heat_timer_set(value);             break;
        case REG_POWER_SWITCH:
            if (value) event_publish(EVENT_POWER_OFF, 0);
            break;
        default: break;
    }
}

static uint16_t on_reg_read(RegisterID reg)
{
    switch (reg) {
        case REG_HEATING_STATUS: return heat_status_get();
        case REG_HEATING_LEVEL:  return heat_level_get();
        case REG_HEATING_TIMER:  return heat_remain_time_get();
        default:                 return register_read(reg);
    }
}

/*============================================================================
 * 入口
 *============================================================================*/
void task_init(void)
{
    event_bus_init();
    bsp_all_init();

    /* 订阅事件（跨层通信） */
    event_subscribe(EVENT_KEY_SHORT_PRESS, on_key_event);
    event_subscribe(EVENT_KEY_LONG_PRESS, on_key_event);

    /* 初始化服务层 */
    key_service_init();
    heat_task_init();
    ble_service_init();

    /* 注册层间回调（app_main 是唯一知道所有层的文件） */
    ble_service_set_frame_handler(on_ble_frame);
    heat_service_set_upload_handler(on_heat_upload);
    protocol_init();
    protocol_register_write_callback(on_reg_write);
    protocol_register_read_callback(on_reg_read);
}
