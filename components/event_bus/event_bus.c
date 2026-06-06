/**
 * @file event_bus.c
 * @brief 事件总线实现 (CMSIS-RTOS v2 消息队列)
 * @version 1.1
 */

#include "event_bus.h"
#include "cmsis_os2.h"
#include <string.h>

#define MAX_HANDLERS_PER_EVENT 4
#define EVENT_QUEUE_LEN 16

typedef struct { EventType type; uint32_t param; } EventMsg;
typedef struct { EventHandler handlers[MAX_HANDLERS_PER_EVENT]; uint8_t count; } EventSubscribers;

static EventSubscribers    g_subscribers[EVENT_COUNT];
static osMessageQueueId_t  g_event_queue  = NULL;
static osMutexId_t         g_event_mutex  = NULL;

static void event_dispatch_task(void *arg) {
    (void)arg;
    EventMsg msg;
    for (;;) {
        if (osMessageQueueGet(g_event_queue, &msg, NULL, osWaitForever) == osOK) {
            if (msg.type >= EVENT_COUNT) continue;
            EventSubscribers *subs = &g_subscribers[msg.type];
            for (uint8_t i = 0; i < subs->count; i++)
                if (subs->handlers[i]) subs->handlers[i](msg.type, msg.param);
        }
    }
}

void event_bus_init(void) {
    memset(g_subscribers, 0, sizeof(g_subscribers));
    g_event_queue = osMessageQueueNew(EVENT_QUEUE_LEN, sizeof(EventMsg), NULL);
    g_event_mutex = osMutexNew(NULL);
    const osThreadAttr_t attr = {.name = "evt_disp", .stack_size = 256, .priority = osPriorityAboveNormal};
    osThreadNew(event_dispatch_task, NULL, &attr);
}

bool event_subscribe(EventType event, EventHandler handler) {
    if (event >= EVENT_COUNT || !handler) return false;
    bool result = false;
    if (osMutexAcquire(g_event_mutex, osWaitForever) == osOK) {
        EventSubscribers *subs = &g_subscribers[event];
        if (subs->count < MAX_HANDLERS_PER_EVENT) { subs->handlers[subs->count++] = handler; result = true; }
        osMutexRelease(g_event_mutex);
    }
    return result;
}

bool event_unsubscribe(EventType event, EventHandler handler) {
    if (event >= EVENT_COUNT || !handler) return false;
    bool result = false;
    if (osMutexAcquire(g_event_mutex, osWaitForever) == osOK) {
        EventSubscribers *subs = &g_subscribers[event];
        for (uint8_t i = 0; i < subs->count; i++)
            if (subs->handlers[i] == handler) { subs->handlers[i] = subs->handlers[--subs->count]; result = true; break; }
        osMutexRelease(g_event_mutex);
    }
    return result;
}

void event_publish(EventType event, uint32_t param) {
    if (event >= EVENT_COUNT || !g_event_queue) return;
    EventMsg msg = {.type = event, .param = param};
    osMessageQueuePut(g_event_queue, &msg, 0, 0);
}

void event_publish_from_isr(EventType event, uint32_t param) {
    if (event >= EVENT_COUNT || !g_event_queue) return;
    EventMsg msg = {.type = event, .param = param};
    osMessageQueuePut(g_event_queue, &msg, 0, 0); /* CMSIS-RTOS v2: ISR-safe with timeout=0 */
}
