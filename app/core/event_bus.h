/**
 * @file event_bus.h
 * @brief 轻量级事件总线（发布-订阅模式，解耦模块间通信）
 * @version 1.0
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 * 事件类型枚举（统一管理所有系统事件）
 *============================================================================*/
typedef enum {
    /* 按键事件 */
    EVENT_KEY_SHORT_PRESS,          // 参数: key_id
    EVENT_KEY_LONG_PRESS,           // 参数: key_id

    /* 加热事件 */
    EVENT_HEAT_STATUS_CHANGE,       // 参数: 0=STOP, 1=RUNNING
    EVENT_HEAT_LEVEL_CHANGE,        // 参数: level (0-2)
    EVENT_HEAT_TIMER_CHANGE,        // 参数: minutes
    EVENT_HEAT_TOGGLE,              // 参数: 无
    EVENT_HEAT_STATUS_UPLOAD,       // 参数: 无 (触发协议上传)

    /* BLE 事件 */
    EVENT_BLE_MODE_CHANGE,          // 参数: bt_mode_t

    /* 系统事件 */
    EVENT_RTC_SYNC,                 // 参数: utc_timestamp
    EVENT_NTC_FAULT,                // 参数: 无
    EVENT_NTC_FAULT_CLEAR,          // 参数: 无
    EVENT_POWER_OFF,                // 参数: 无

    EVENT_COUNT
} EventType;

/*============================================================================
 * 事件处理器函数类型
 *============================================================================*/
typedef void (*EventHandler)(EventType event, uint32_t param);

/*============================================================================
 * 公共 API
 *============================================================================*/
void event_bus_init(void);
bool event_subscribe(EventType event, EventHandler handler);
bool event_unsubscribe(EventType event, EventHandler handler);
void event_publish(EventType event, uint32_t param);
void event_publish_from_isr(EventType event, uint32_t param);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_H */
