// at_command_manager.h
#ifndef AT_COMMAND_MANAGER_H
#define AT_COMMAND_MANAGER_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include <stdbool.h>

// AT命令类型枚举
typedef enum
{
    AT_CMD_QM,        // AT+QM 相关命令
    AT_CMD_QR,        // AT+QR 相关命令
    AT_CMD_QS,        // AT+QS 相关命令
    AT_CMD_QP,        // AT+QP 相关命令
    AT_CMD_GENERIC,   // 通用AT命令
    AT_CMD_MAX
} at_cmd_type_t;

// AT命令优先级
typedef enum
{
    AT_PRIORITY_HIGH = 0,
    AT_PRIORITY_NORMAL,
    AT_PRIORITY_LOW
} at_priority_t;

// AT响应状态
typedef enum
{
    AT_RESPONSE_PENDING,    // 等待响应中
    AT_RESPONSE_RECEIVED,   // 响应已收到
    AT_RESPONSE_TIMEOUT,    // 响应超时
    AT_RESPONSE_ERROR       // 响应错误
} at_response_status_t;

// AT命令请求结构
typedef struct
{
    at_cmd_type_t cmd_type;     // 命令类型
    uint32_t request_id;        // 请求ID（唯一标识）
    uint8_t* cmd_data;          // 命令数据
    uint16_t cmd_len;           // 命令长度
    TaskHandle_t sender_task;   // 发送者任务句柄
    at_priority_t priority;     // 命令优先级
    uint32_t timeout_ms;        // 超时时间（毫秒）
    uint32_t timestamp;         // 发送时间戳
} at_request_t;

// AT命令响应结构
typedef struct
{
    at_cmd_type_t cmd_type;        // 命令类型
    uint32_t request_id;           // 对应的请求ID
    uint8_t* response_data;        // 响应数据
    uint16_t response_len;         // 响应长度
    at_response_status_t status;   // 响应状态
    bool is_success;               // 是否成功（如QM+00为成功）
    uint32_t timestamp;            // 接收时间戳
} at_response_t;

// AT命令处理器函数类型
typedef void (*at_cmd_handler_t)(at_response_t* response, void* user_data);

// AT命令注册信息
typedef struct
{
    at_cmd_type_t cmd_type;     // 命令类型
    const char* cmd_prefix;     // 命令前缀（如"QM+"）
    at_cmd_handler_t handler;   // 响应处理函数
    void* user_data;            // 用户数据
    bool wait_for_response;     // 是否等待响应
} at_cmd_registration_t;

// AT命令管理器结构
typedef struct
{
    QueueHandle_t request_queue;    // 请求队列
    QueueHandle_t response_queue;   // 响应队列
    SemaphoreHandle_t mutex;        // 互斥锁

    // 命令类型注册表
    at_cmd_registration_t registrations[AT_CMD_MAX];
    uint8_t reg_count;

    // 待处理的请求表（用于匹配响应）
    at_request_t pending_requests[10];
    uint8_t pending_count;

    // 统计信息
    uint32_t total_requests;
    uint32_t total_responses;
    uint32_t timeout_count;
} at_command_manager_t;

// 初始化AT命令管理器
at_command_manager_t* at_command_manager_init(void);

// 注册AT命令处理器
bool at_register_command(at_command_manager_t* manager, at_cmd_type_t cmd_type, const char* cmd_prefix,
                         at_cmd_handler_t handler, void* user_data, bool wait_for_response);

// 发送AT命令（异步）
bool at_send_command_async(at_command_manager_t* manager, at_cmd_type_t cmd_type, const uint8_t* cmd_data,
                           uint16_t cmd_len, at_priority_t priority);

// 发送AT命令并等待响应（同步）
bool at_send_command_sync(at_command_manager_t* manager, at_cmd_type_t cmd_type, const uint8_t* cmd_data,
                          uint16_t cmd_len, at_response_t* response, uint32_t timeout_ms);

// 获取响应（非阻塞）
bool at_get_response(at_command_manager_t* manager, at_response_t* response, uint32_t timeout_ms);

// 检查是否有指定命令类型的响应
bool at_has_response_type(at_command_manager_t* manager, at_cmd_type_t cmd_type);

// 清理响应资源
void at_response_free(at_response_t* response);

// 处理接收到的AT响应帧（由接收任务调用）
void at_process_received_frame(at_command_manager_t* manager, const uint8_t* data, uint16_t len);

// 超时检查（需要定期调用）
void at_check_timeouts(at_command_manager_t* manager);

#endif
