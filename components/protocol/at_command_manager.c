/**
 * @file at_command_manager.c
 * @brief AT命令管理器实现 (CMSIS-RTOS v2)
 * @version 2.1
 */

#include "at_command_manager.h"
#include <string.h>

#ifndef AT_DEFAULT_TIMEOUT_MS
#define AT_DEFAULT_TIMEOUT_MS 2000
#endif

static at_request_t *pool_alloc(at_command_manager_t *mgr) {
    for (uint8_t i = 0; i < AT_PENDING_MAX; i++)
        if (!mgr->request_pool[i].in_use) { mgr->request_pool[i].in_use = true; return &mgr->request_pool[i]; }
    return NULL;
}
static void pool_free(at_request_t *req) { if (req) req->in_use = false; }

static bool check_success(const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i + 2 < len; i++) { if (data[i] == '+' && data[i + 1] == '0' && data[i + 2] == '0') return true; if (i + 1 < len && data[i] == 'O' && data[i + 1] == 'K') return true; }
    return false;
}

bool at_manager_init(at_command_manager_t *mgr) {
    if (!mgr) return false;
    memset(mgr, 0, sizeof(*mgr));
    mgr->request_queue  = osMessageQueueNew(AT_PENDING_MAX, sizeof(at_request_t *), NULL);
    mgr->response_queue = osMessageQueueNew(AT_PENDING_MAX, sizeof(at_response_t), NULL);
    mgr->mutex          = osMutexNew(NULL);
    return (mgr->request_queue && mgr->response_queue && mgr->mutex);
}

bool at_register_command(at_command_manager_t *mgr, at_cmd_type_t type, const char *prefix, at_cmd_handler_t handler,
                         void *user_data, bool wait_resp) {
    if (!mgr || mgr->reg_count >= AT_CMD_MAX) return false;
    if (osMutexAcquire(mgr->mutex, osWaitForever) != osOK) return false;
    mgr->registrations[mgr->reg_count].cmd_type = type; mgr->registrations[mgr->reg_count].cmd_prefix = prefix;
    mgr->registrations[mgr->reg_count].handler = handler; mgr->registrations[mgr->reg_count].user_data = user_data;
    mgr->registrations[mgr->reg_count].wait_for_response = wait_resp; mgr->reg_count++;
    osMutexRelease(mgr->mutex);
    return true;
}

bool at_send_command_async(at_command_manager_t *mgr, at_cmd_type_t type, const uint8_t *data, uint16_t len, at_priority_t prio) {
    if (!mgr || !data || len > AT_CMD_DATA_MAX) return false;
    at_request_t *req = pool_alloc(mgr); if (!req) return false;
    req->cmd_type = type; req->request_id = ++mgr->request_id_counter; req->priority = prio;
    req->timeout_ms = AT_DEFAULT_TIMEOUT_MS; req->timestamp = osKernelGetTickCount();
    req->cmd_len = (len < AT_CMD_DATA_MAX) ? len : AT_CMD_DATA_MAX; memcpy(req->cmd_data, data, req->cmd_len);
    if (osMutexAcquire(mgr->mutex, osWaitForever) == osOK) { mgr->pending_count++; mgr->total_requests++; osMutexRelease(mgr->mutex); }
    if (osMessageQueuePut(mgr->request_queue, &req, 0, 0) == osOK) return true;
    pool_free(req); return false;
}

bool at_send_command_sync(at_command_manager_t *mgr, at_cmd_type_t type, const uint8_t *data, uint16_t len, at_response_t *resp, uint32_t timeout_ms) {
    if (!at_send_command_async(mgr, type, data, len, AT_PRIORITY_NORMAL)) return false;
    uint32_t start = osKernelGetTickCount(), timeout_ticks = (timeout_ms * osKernelGetTickFreq()) / 1000U;
    while ((osKernelGetTickCount() - start) < timeout_ticks)
        if (osMessageQueueGet(mgr->response_queue, resp, NULL, osWaitForever) == osOK) return true; /* simplified */
    return false;
}

void at_process_frame(at_command_manager_t *mgr, const uint8_t *data, uint16_t len) {
    if (!mgr || len < 2) return;
    at_response_t resp; memset(&resp, 0, sizeof(resp));
    resp.timestamp = osKernelGetTickCount(); resp.is_success = check_success(data, len);
    resp.status = AT_RESP_RECEIVED; resp.response_len = (len < AT_RESP_DATA_MAX) ? len : AT_RESP_DATA_MAX;
    memcpy(resp.response_data, data, resp.response_len);
    if (osMutexAcquire(mgr->mutex, osWaitForever) == osOK) {
        for (uint8_t i = 0; i < AT_PENDING_MAX; i++) if (mgr->request_pool[i].in_use) { resp.cmd_type = mgr->request_pool[i].cmd_type; resp.request_id = mgr->request_pool[i].request_id; pool_free(&mgr->request_pool[i]); mgr->pending_count--; mgr->total_responses++; break; }
        osMutexRelease(mgr->mutex);
    }
    for (uint8_t i = 0; i < mgr->reg_count; i++) if (mgr->registrations[i].handler) mgr->registrations[i].handler(&resp, mgr->registrations[i].user_data);
    osMessageQueuePut(mgr->response_queue, &resp, 0, 0);
}

void at_check_timeouts(at_command_manager_t *mgr) {
    if (!mgr) return;
    uint32_t now = osKernelGetTickCount(), ms_per_tick = 1000U / osKernelGetTickFreq();
    if (osMutexAcquire(mgr->mutex, 0) == osOK) {
        for (uint8_t i = 0; i < AT_PENDING_MAX; i++) {
            if (!mgr->request_pool[i].in_use) continue;
            uint32_t elapsed = (now - mgr->request_pool[i].timestamp) * ms_per_tick;
            if (elapsed > mgr->request_pool[i].timeout_ms) {
                at_response_t resp = {0}; resp.cmd_type = mgr->request_pool[i].cmd_type; resp.request_id = mgr->request_pool[i].request_id; resp.status = AT_RESP_TIMEOUT; resp.timestamp = now;
                for (uint8_t j = 0; j < mgr->reg_count; j++) if (mgr->registrations[j].handler) mgr->registrations[j].handler(&resp, mgr->registrations[j].user_data);
                pool_free(&mgr->request_pool[i]); mgr->pending_count--; mgr->timeout_count++;
            }
        }
        osMutexRelease(mgr->mutex);
    }
}
