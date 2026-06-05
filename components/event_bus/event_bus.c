/**
 * @file event_bus.c
 * @brief 事件总线实现（固定数组 + FreeRTOS 队列，无动态内存）
 * @version 1.0
 */

#include "event_bus.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>

/*============================================================================
 * 配置
 *============================================================================*/
#define MAX_HANDLERS_PER_EVENT 4  // 每个事件最多订阅者数
#define EVENT_QUEUE_LEN 16        // 事件队列深度

/*============================================================================
 * 事件消息结构
 *============================================================================*/
typedef struct {
    EventType type;
    uint32_t param;
} EventMsg;

/*============================================================================
 * 订阅表项
 *============================================================================*/
typedef struct {
    EventHandler handlers[MAX_HANDLERS_PER_EVENT];
    uint8_t count;
} EventSubscribers;

/*============================================================================
 * 静态资源（零动态内存）
 *============================================================================*/
static EventSubscribers g_subscribers[EVENT_COUNT];
static QueueHandle_t g_event_queue = NULL;
static SemaphoreHandle_t g_event_mutex = NULL;

/*============================================================================
 * 内部任务：从队列消费事件并分发给订阅者
 *============================================================================*/
static void event_dispatch_task(void* arg)
{
    (void)arg;
    EventMsg msg;

    for (;;) {
        if (xQueueReceive(g_event_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.type >= EVENT_COUNT)
                continue;

            EventSubscribers* subs = &g_subscribers[msg.type];
            for (uint8_t i = 0; i < subs->count; i++) {
                if (subs->handlers[i]) {
                    subs->handlers[i](msg.type, msg.param);
                }
            }
        }
    }
}

/*============================================================================
 * 公共 API
 *============================================================================*/

void event_bus_init(void)
{
    memset(g_subscribers, 0, sizeof(g_subscribers));

    g_event_queue = xQueueCreate(EVENT_QUEUE_LEN, sizeof(EventMsg));
    configASSERT(g_event_queue != NULL);

    g_event_mutex = xSemaphoreCreateMutex();
    configASSERT(g_event_mutex != NULL);

    // 创建事件分发任务（优先级高于消费者，避免积压）
    xTaskCreate(event_dispatch_task, "evt_disp", 256, NULL, 4, NULL);
}

bool event_subscribe(EventType event, EventHandler handler)
{
    if (event >= EVENT_COUNT || handler == NULL)
        return false;

    bool result = false;
    if (xSemaphoreTake(g_event_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        EventSubscribers* subs = &g_subscribers[event];
        if (subs->count < MAX_HANDLERS_PER_EVENT) {
            subs->handlers[subs->count++] = handler;
            result = true;
        }
        xSemaphoreGive(g_event_mutex);
    }
    return result;
}

bool event_unsubscribe(EventType event, EventHandler handler)
{
    if (event >= EVENT_COUNT || handler == NULL)
        return false;

    bool result = false;
    if (xSemaphoreTake(g_event_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        EventSubscribers* subs = &g_subscribers[event];
        for (uint8_t i = 0; i < subs->count; i++) {
            if (subs->handlers[i] == handler) {
                // 用最后一个覆盖当前位置
                subs->handlers[i] = subs->handlers[--subs->count];
                result = true;
                break;
            }
        }
        xSemaphoreGive(g_event_mutex);
    }
    return result;
}

void event_publish(EventType event, uint32_t param)
{
    if (event >= EVENT_COUNT || g_event_queue == NULL)
        return;

    EventMsg msg = {.type = event, .param = param};
    xQueueSend(g_event_queue, &msg, 0);
}

void event_publish_from_isr(EventType event, uint32_t param)
{
    if (event >= EVENT_COUNT || g_event_queue == NULL)
        return;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    EventMsg msg = {.type = event, .param = param};
    xQueueSendFromISR(g_event_queue, &msg, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
