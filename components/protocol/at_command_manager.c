/**
 * @file at_command_manager.c
 * @brief AT命令管理器实现（静态池，零动态内存）
 * @version 2.0
 */

#include "at_command_manager.h"
#include <string.h>

#ifndef AT_DEFAULT_TIMEOUT_MS
#define AT_DEFAULT_TIMEOUT_MS 2000
#endif

/*============================================================================
 * 内部辅助
 *============================================================================*/

/* 从池中分配一个请求槽 */
static at_request_t* pool_alloc(at_command_manager_t* mgr) {
    for (uint8_t i = 0; i < AT_PENDING_MAX; i++) {
        if (!mgr->request_pool[i].in_use) {
            mgr->request_pool[i].in_use = true;
            return &mgr->request_pool[i];
        }
    }
    return NULL;
}

/* 释放请求槽 */
static void pool_free(at_request_t* req) {
    if (req) req->in_use = false;
}

/* 检查响应是否成功 */
static bool check_success(const uint8_t* data, uint16_t len) {
    /* 查找 "+00" 或 "OK" 成功标记 */
    for (uint16_t i = 0; i + 2 < len; i++) {
        if (data[i] == '+' && data[i + 1] == '0' && data[i + 2] == '0') return true;
        if (i + 1 < len && data[i] == 'O' && data[i + 1] == 'K') return true;
    }
    return false;
}

/*============================================================================
 * 公共 API
 *============================================================================*/

bool at_manager_init(at_command_manager_t* mgr) {
    if (!mgr) return false;

    memset(mgr, 0, sizeof(*mgr));

    mgr->request_queue = xQueueCreate(AT_PENDING_MAX, sizeof(at_request_t*));
    mgr->response_queue = xQueueCreate(AT_PENDING_MAX, sizeof(at_response_t));
    mgr->mutex = xSemaphoreCreateMutex();

    return (mgr->request_queue && mgr->response_queue && mgr->mutex);
}

bool at_register_command(at_command_manager_t* mgr, at_cmd_type_t type, const char* prefix, at_cmd_handler_t handler,
                         void* user_data, bool wait_resp) {
    if (!mgr || mgr->reg_count >= AT_CMD_MAX) return false;

    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(100)) != pdPASS) return false;

    mgr->registrations[mgr->reg_count].cmd_type = type;
    mgr->registrations[mgr->reg_count].cmd_prefix = prefix;
    mgr->registrations[mgr->reg_count].handler = handler;
    mgr->registrations[mgr->reg_count].user_data = user_data;
    mgr->registrations[mgr->reg_count].wait_for_response = wait_resp;
    mgr->reg_count++;

    xSemaphoreGive(mgr->mutex);
    return true;
}

bool at_send_command_async(at_command_manager_t* mgr, at_cmd_type_t type, const uint8_t* data, uint16_t len,
                           at_priority_t prio) {
    if (!mgr || !data || len > AT_CMD_DATA_MAX) return false;

    /* 从静态池分配 */
    at_request_t* req = pool_alloc(mgr);
    if (!req) return false;

    req->cmd_type = type;
    req->request_id = ++mgr->request_id_counter;
    req->priority = prio;
    req->timeout_ms = AT_DEFAULT_TIMEOUT_MS;
    req->timestamp = xTaskGetTickCount();
    req->cmd_len = (len < AT_CMD_DATA_MAX) ? len : AT_CMD_DATA_MAX;
    memcpy(req->cmd_data, data, req->cmd_len);

    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(100)) == pdPASS) {
        mgr->pending_count++;
        mgr->total_requests++;
        xSemaphoreGive(mgr->mutex);
    }

    /* 发送请求指针到队列 */
    if (xQueueSend(mgr->request_queue, &req, 0) == pdPASS) { return true; }

    pool_free(req);
    return false;
}

bool at_send_command_sync(at_command_manager_t* mgr, at_cmd_type_t type, const uint8_t* data, uint16_t len,
                          at_response_t* resp, uint32_t timeout_ms) {
    if (!at_send_command_async(mgr, type, data, len, AT_PRIORITY_NORMAL)) return false;

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);

    while ((xTaskGetTickCount() - start) < timeout) {
        if (xQueueReceive(mgr->response_queue, resp, pdMS_TO_TICKS(50)) == pdPASS) { return true; }
    }
    return false;
}

void at_process_frame(at_command_manager_t* mgr, const uint8_t* data, uint16_t len) {
    if (!mgr || len < 2) return;

    at_response_t resp;
    memset(&resp, 0, sizeof(resp));
    resp.timestamp = xTaskGetTickCount();
    resp.is_success = check_success(data, len);
    resp.status = AT_RESP_RECEIVED;
    resp.response_len = (len < AT_RESP_DATA_MAX) ? len : AT_RESP_DATA_MAX;
    memcpy(resp.response_data, data, resp.response_len);

    /* 匹配请求类型 */
    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(50)) == pdPASS) {
        for (uint8_t i = 0; i < AT_PENDING_MAX; i++) {
            if (mgr->request_pool[i].in_use) {
                /* 简化匹配：相同类型即匹配 */
                resp.cmd_type = mgr->request_pool[i].cmd_type;
                resp.request_id = mgr->request_pool[i].request_id;

                pool_free(&mgr->request_pool[i]);
                mgr->pending_count--;
                mgr->total_responses++;
                break;
            }
        }
        xSemaphoreGive(mgr->mutex);
    }

    /* 调用注册的回调 */
    for (uint8_t i = 0; i < mgr->reg_count; i++) {
        if (mgr->registrations[i].handler) { mgr->registrations[i].handler(&resp, mgr->registrations[i].user_data); }
    }

    xQueueSend(mgr->response_queue, &resp, 0);
}

void at_check_timeouts(at_command_manager_t* mgr) {
    if (!mgr) return;
    TickType_t now = xTaskGetTickCount();

    if (xSemaphoreTake(mgr->mutex, 0) == pdPASS) {
        for (uint8_t i = 0; i < AT_PENDING_MAX; i++) {
            if (!mgr->request_pool[i].in_use) continue;

            uint32_t elapsed = (now - mgr->request_pool[i].timestamp) * portTICK_PERIOD_MS;
            if (elapsed > mgr->request_pool[i].timeout_ms) {
                /* 超时：通知回调 */
                at_response_t resp = {0};
                resp.cmd_type = mgr->request_pool[i].cmd_type;
                resp.request_id = mgr->request_pool[i].request_id;
                resp.status = AT_RESP_TIMEOUT;
                resp.timestamp = now;

                for (uint8_t j = 0; j < mgr->reg_count; j++) {
                    if (mgr->registrations[j].handler) {
                        mgr->registrations[j].handler(&resp, mgr->registrations[j].user_data);
                    }
                }

                pool_free(&mgr->request_pool[i]);
                mgr->pending_count--;
                mgr->timeout_count++;
            }
        }
        xSemaphoreGive(mgr->mutex);
    }
}
