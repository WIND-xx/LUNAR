/**
 * @file at_command_manager.h
 * @brief AT命令管理器（静态内存池，零动态分配）
 * @version 2.0
 */

#ifndef AT_COMMAND_MANAGER_H
#define AT_COMMAND_MANAGER_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include <stdbool.h>
#include <stdint.h>

/*============================================================================
 * 配置常量
 *============================================================================*/
#define AT_CMD_MAX           5     // 最大注册命令类型数
#define AT_PENDING_MAX       10    // 最大待处理请求数
#define AT_CMD_DATA_MAX      64    // 单条命令数据最大长度
#define AT_RESP_DATA_MAX     128   // 单条响应数据最大长度

/*============================================================================
 * 类型定义
 *============================================================================*/
typedef enum {
    AT_CMD_QM,
    AT_CMD_QR,
    AT_CMD_QS,
    AT_CMD_QP,
    AT_CMD_GENERIC,
} at_cmd_type_t;

typedef enum {
    AT_PRIORITY_HIGH = 0,
    AT_PRIORITY_NORMAL,
    AT_PRIORITY_LOW,
} at_priority_t;

typedef enum {
    AT_RESP_PENDING,
    AT_RESP_RECEIVED,
    AT_RESP_TIMEOUT,
    AT_RESP_ERROR,
} at_response_status_t;

/* 请求结构（数据内嵌，无单独分配） */
typedef struct {
    at_cmd_type_t cmd_type;
    uint32_t      request_id;
    uint8_t       cmd_data[AT_CMD_DATA_MAX];
    uint16_t      cmd_len;
    at_priority_t priority;
    uint32_t      timeout_ms;
    uint32_t      timestamp;
    bool          in_use;
} at_request_t;

/* 响应结构（数据内嵌） */
typedef struct {
    at_cmd_type_t        cmd_type;
    uint32_t             request_id;
    uint8_t              response_data[AT_RESP_DATA_MAX];
    uint16_t             response_len;
    at_response_status_t status;
    bool                 is_success;
    uint32_t             timestamp;
} at_response_t;

typedef void (*at_cmd_handler_t)(at_response_t *response, void *user_data);

typedef struct {
    at_cmd_type_t     cmd_type;
    const char       *cmd_prefix;
    at_cmd_handler_t  handler;
    void             *user_data;
    bool              wait_for_response;
} at_cmd_registration_t;

/* 管理器结构（全部静态分配） */
typedef struct {
    QueueHandle_t      request_queue;
    QueueHandle_t      response_queue;
    SemaphoreHandle_t  mutex;

    at_cmd_registration_t registrations[AT_CMD_MAX];
    uint8_t               reg_count;

    /* 静态请求池 */
    at_request_t  request_pool[AT_PENDING_MAX];
    uint8_t       pending_count;
    uint32_t      request_id_counter;

    /* 统计 */
    uint32_t total_requests;
    uint32_t total_responses;
    uint32_t timeout_count;
} at_command_manager_t;

/*============================================================================
 * 公共 API（管理器实例需静态分配）
 *============================================================================*/
bool at_manager_init(at_command_manager_t *mgr);
bool at_register_command(at_command_manager_t *mgr, at_cmd_type_t type, const char *prefix,
                         at_cmd_handler_t handler, void *user_data, bool wait_resp);
bool at_send_command_async(at_command_manager_t *mgr, at_cmd_type_t type,
                           const uint8_t *data, uint16_t len, at_priority_t prio);
bool at_send_command_sync(at_command_manager_t *mgr, at_cmd_type_t type,
                          const uint8_t *data, uint16_t len,
                          at_response_t *resp, uint32_t timeout_ms);
void at_process_frame(at_command_manager_t *mgr, const uint8_t *data, uint16_t len);
void at_check_timeouts(at_command_manager_t *mgr);

#endif /* AT_COMMAND_MANAGER_H */
