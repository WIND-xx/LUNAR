// at_command_manager.c
#include "at_command_manager.h"
#include <stdlib.h>
#include <string.h>

// 生成唯一请求ID
static uint32_t generate_request_id(void)
{
    static uint32_t counter = 0;
    return counter++;
}

// 从响应数据中提取命令类型
static at_cmd_type_t extract_cmd_type_from_response(at_command_manager_t* manager, const uint8_t* data, uint16_t len)
{
    if (len < 3) return AT_CMD_GENERIC;

    // 转换为字符串进行前缀匹配
    char prefix[8] = {0};
    int i;
    for (i = 0; i < len && i < 7; i++)
    {
        if (data[i] == '+' || data[i] == '\r' || data[i] == '\n') break;
        prefix[i] = data[i];
    }
    prefix[i] = '\0';

    // 在注册表中查找匹配的命令类型
    for (int j = 0; j < manager->reg_count; j++)
    {
        if (strstr(prefix, manager->registrations[j].cmd_prefix) != NULL)
        {
            return manager->registrations[j].cmd_type;
        }
    }

    return AT_CMD_GENERIC;
}

// 检查响应是否成功
static bool check_response_success(const uint8_t* data, uint16_t len)
{
    // 检查是否包含成功码（如QM+00）
    const char* success_patterns[] = {"+00", "OK", "SUCCESS"};
    char response_str[128]         = {0};

    if (len >= sizeof(response_str)) len = sizeof(response_str) - 1;

    memcpy(response_str, data, len);
    response_str[len] = '\0';

    for (int i = 0; i < sizeof(success_patterns) / sizeof(success_patterns[0]); i++)
    {
        if (strstr(response_str, success_patterns[i]) != NULL)
        {
            return true;
        }
    }

    return false;
}

// 初始化AT命令管理器
at_command_manager_t* at_command_manager_init(void)
{
    at_command_manager_t* manager = malloc(sizeof(at_command_manager_t));
    if (!manager) return NULL;

    memset(manager, 0, sizeof(at_command_manager_t));

    // 创建请求队列（按优先级排序）
    manager->request_queue  = xQueueCreate(20, sizeof(at_request_t));
    manager->response_queue = xQueueCreate(20, sizeof(at_response_t));
    manager->mutex          = xSemaphoreCreateMutex();

    if (!manager->request_queue || !manager->response_queue || !manager->mutex)
    {
        free(manager);
        return NULL;
    }

    return manager;
}

// 注册AT命令处理器
bool at_register_command(at_command_manager_t* manager, at_cmd_type_t cmd_type, const char* cmd_prefix,
                         at_cmd_handler_t handler, void* user_data, bool wait_for_response)
{
    if (xSemaphoreTake(manager->mutex, portMAX_DELAY) != pdPASS) return false;

    if (manager->reg_count >= AT_CMD_MAX)
    {
        xSemaphoreGive(manager->mutex);
        return false;
    }

    manager->registrations[manager->reg_count].cmd_type          = cmd_type;
    manager->registrations[manager->reg_count].cmd_prefix        = cmd_prefix;
    manager->registrations[manager->reg_count].handler           = handler;
    manager->registrations[manager->reg_count].user_data         = user_data;
    manager->registrations[manager->reg_count].wait_for_response = wait_for_response;

    manager->reg_count++;

    xSemaphoreGive(manager->mutex);
    return true;
}

// 发送AT命令（异步）
bool at_send_command_async(at_command_manager_t* manager, at_cmd_type_t cmd_type, const uint8_t* cmd_data,
                           uint16_t cmd_len, at_priority_t priority)
{
    at_request_t request;

    request.cmd_type    = cmd_type;
    request.request_id  = generate_request_id();
    request.priority    = priority;
    request.timeout_ms  = 2000;   // 默认2秒超时
    request.timestamp   = xTaskGetTickCount();
    request.sender_task = xTaskGetCurrentTaskHandle();

    // 复制命令数据
    request.cmd_data = malloc(cmd_len);
    if (!request.cmd_data) return false;

    memcpy(request.cmd_data, cmd_data, cmd_len);
    request.cmd_len = cmd_len;

    // 添加到待处理列表
    if (xSemaphoreTake(manager->mutex, portMAX_DELAY) == pdPASS)
    {
        if (manager->pending_count < sizeof(manager->pending_requests) / sizeof(manager->pending_requests[0]))
        {
            manager->pending_requests[manager->pending_count++] = request;
            manager->total_requests++;
        }
        xSemaphoreGive(manager->mutex);
    }

    // 发送到请求队列
    if (xQueueSend(manager->request_queue, &request, 0) == pdPASS)
    {
        return true;
    }

    free(request.cmd_data);
    return false;
}

// 发送AT命令并等待响应（同步）
bool at_send_command_sync(at_command_manager_t* manager, at_cmd_type_t cmd_type, const uint8_t* cmd_data,
                          uint16_t cmd_len, at_response_t* response, uint32_t timeout_ms)
{
    uint32_t request_id      = generate_request_id();
    TickType_t start_ticks   = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    // 发送异步命令
    if (!at_send_command_async(manager, cmd_type, cmd_data, cmd_len, AT_PRIORITY_NORMAL)) return false;

    // 等待响应
    while ((xTaskGetTickCount() - start_ticks) < timeout_ticks)
    {
        if (xQueueReceive(manager->response_queue, response, pdMS_TO_TICKS(100)) == pdPASS)
        {
            if (response->request_id == request_id)
            {
                return true;
            }
            // 不是我们的响应，放回队列
            xQueueSendToFront(manager->response_queue, response, 0);
        }
    }

    // 超时，从待处理列表中移除请求
    if (xSemaphoreTake(manager->mutex, portMAX_DELAY) == pdPASS)
    {
        for (int i = 0; i < manager->pending_count; i++)
        {
            if (manager->pending_requests[i].request_id == request_id)
            {
                free(manager->pending_requests[i].cmd_data);
                // 移除请求
                for (int j = i; j < manager->pending_count - 1; j++)
                {
                    manager->pending_requests[j] = manager->pending_requests[j + 1];
                }
                manager->pending_count--;
                manager->timeout_count++;
                break;
            }
        }
        xSemaphoreGive(manager->mutex);
    }

    return false;
}

// 处理接收到的AT响应帧
void at_process_received_frame(at_command_manager_t* manager, const uint8_t* data, uint16_t len)
{
    if (len < 2) return;

    at_response_t response;
    at_request_t* matched_request = NULL;
    int matched_index             = -1;

    // 提取命令类型
    response.cmd_type   = extract_cmd_type_from_response(manager, data, len);
    response.timestamp  = xTaskGetTickCount();
    response.is_success = check_response_success(data, len);
    response.status     = AT_RESPONSE_RECEIVED;

    // 复制响应数据
    response.response_data = malloc(len);
    if (!response.response_data) return;

    memcpy(response.response_data, data, len);
    response.response_len = len;

    // 在待处理列表中查找匹配的请求
    if (xSemaphoreTake(manager->mutex, portMAX_DELAY) == pdPASS)
    {
        for (int i = 0; i < manager->pending_count; i++)
        {
            if (manager->pending_requests[i].cmd_type == response.cmd_type)
            {
                matched_request = &manager->pending_requests[i];
                matched_index   = i;
                break;
            }
        }

        if (matched_request)
        {
            response.request_id = matched_request->request_id;

            // 从待处理列表中移除
            free(matched_request->cmd_data);
            for (int i = matched_index; i < manager->pending_count - 1; i++)
            {
                manager->pending_requests[i] = manager->pending_requests[i + 1];
            }
            manager->pending_count--;
            manager->total_responses++;
        }
        xSemaphoreGive(manager->mutex);
    }

    // 如果没有找到匹配的请求，使用默认请求ID
    if (!matched_request)
    {
        response.request_id = 0xFFFFFFFF;   // 特殊ID表示未匹配的响应
    }

    // 调用注册的处理器
    for (int i = 0; i < manager->reg_count; i++)
    {
        if (manager->registrations[i].cmd_type == response.cmd_type && manager->registrations[i].handler)
        {
            manager->registrations[i].handler(&response, manager->registrations[i].user_data);
        }
    }

    // 发送到响应队列
    xQueueSend(manager->response_queue, &response, 0);
}

// 超时检查
void at_check_timeouts(at_command_manager_t* manager)
{
    TickType_t current_ticks = xTaskGetTickCount();

    if (xSemaphoreTake(manager->mutex, 0) == pdPASS)
    {
        for (int i = 0; i < manager->pending_count; i++)
        {
            uint32_t elapsed = (current_ticks - manager->pending_requests[i].timestamp) * 1000 / configTICK_RATE_HZ;

            if (elapsed > manager->pending_requests[i].timeout_ms)
            {
                // 创建超时响应
                at_response_t timeout_response;
                timeout_response.cmd_type      = manager->pending_requests[i].cmd_type;
                timeout_response.request_id    = manager->pending_requests[i].request_id;
                timeout_response.response_data = NULL;
                timeout_response.response_len  = 0;
                timeout_response.status        = AT_RESPONSE_TIMEOUT;
                timeout_response.is_success    = false;
                timeout_response.timestamp     = current_ticks;

                // 调用处理器通知超时
                for (int j = 0; j < manager->reg_count; j++)
                {
                    if (manager->registrations[j].cmd_type == timeout_response.cmd_type &&
                        manager->registrations[j].handler)
                    {
                        manager->registrations[j].handler(&timeout_response, manager->registrations[j].user_data);
                    }
                }

                // 发送超时响应到队列
                xQueueSend(manager->response_queue, &timeout_response, 0);

                // 清理请求数据
                free(manager->pending_requests[i].cmd_data);

                // 从列表中移除
                for (int j = i; j < manager->pending_count - 1; j++)
                {
                    manager->pending_requests[j] = manager->pending_requests[j + 1];
                }
                manager->pending_count--;
                manager->timeout_count++;
                i--;   // 重新检查当前位置
            }
        }
        xSemaphoreGive(manager->mutex);
    }
}
