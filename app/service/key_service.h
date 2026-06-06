/**
 * @file key_service.h
 * @brief 按键服务接口：扫描 + 去抖 + 事件发布 + 业务处理
 * @version 3.0
 */

#ifndef KEY_SERVICE_H
#define KEY_SERVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { KEY_EVENT_NONE = 0, KEY_EVENT_SHORT_PRESS, KEY_EVENT_LONG_PRESS } key_event_t;

void key_service_init(void);

#ifdef __cplusplus
}
#endif

#endif
