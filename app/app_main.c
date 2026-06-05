/**
 * @file app_main.c
 * @brief 唯一编排者：注册回调 + 订阅事件，连接 service ↔ protocol
 * @version 3.0
 */

#include "FreeRTOS.h"
#include "task.h"

#include "buzzer.h"
#include "core/app_config.h"
#include "core/event_bus.h"
#include "led.h"
#include "key.h"

/* 服务层接口 */
#include "service/ble_service.h"
#include "service/heat_service.h"
#include "service/key_service.h"

/* 协议层接口 */
#include "protocol/at_parser.h"
#include "protocol/modbus_slave.h"

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
            case KEY_MUSIC:
                ble_mode(BT_MODE_MUSIC);
                break;
            case KEY_BLUETOOTH:
                ble_mode(BT_MODE_BT);
                break;
            case KEY_PLAY_PAUSE:
                music_switch();
                break;
            case KEY_PREV:
                music_prev();
                break;
            case KEY_NEXT:
                music_next();
                break;
            case KEY_VOL_DOWN:
                music_volume_control(VOL_DIR_DOWN);
                break;
            case KEY_VOL_UP:
                music_volume_control(VOL_DIR_UP);
                break;
            case KEY_HEAT:
                heat_status_switch();
                break;
            case KEY_HEAT_PLUS:
                heat_level_down();
                break;
            case KEY_HEAT_MINUS:
                heat_level_up();
                break;
            case KEY_MIN10:
                heat_timer_set(10);
                break;
            case KEY_MIN30:
                heat_timer_set(30);
                break;
            case KEY_MIN60:
                heat_timer_set(60);
                break;
            default:
                break;
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
                led_time_select(0);
                break;
            default:
                break;
        }
    }
}

/*============================================================================
 * Modbus 寄存器回调
 *============================================================================*/
static void on_reg_write(RegisterID reg, uint16_t value)
{
    switch (reg) {
        case REG_HEATING_STATUS:
            heat_status_set(value ? HEAT_STATUS_RUNNING : HEAT_STATUS_STOP);
            break;
        case REG_HEATING_LEVEL:
            heat_level_set((HeatLevel)value);
            break;
        case REG_HEATING_TIMER:
            heat_timer_set(value);
            break;
        case REG_POWER_SWITCH:
            if (value)
                event_publish(EVENT_POWER_OFF, 0);
            break;
        default:
            break;
    }
}

static uint16_t on_reg_read(RegisterID reg)
{
    switch (reg) {
        case REG_HEATING_STATUS:
            return heat_status_get();
        case REG_HEATING_LEVEL:
            return heat_level_get();
        case REG_HEATING_TIMER:
            return heat_remain_time_get();
        default:
            return register_read(reg);
    }
}

/*============================================================================
 * 入口
 *============================================================================*/
void task_init(void)
{
    event_bus_init();
    led_init();
    buzzer_init();

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
    protocol_register_write_callback(on_reg_write);
    protocol_register_read_callback(on_reg_read);
}
