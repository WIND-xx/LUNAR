#include "at_process_task.h"
#include "BufferProcess.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// 处理AT帧的任务
static void vATProcessTask(void *pvParameters)
{
    (void) pvParameters;

    uint8_t at_frame[AT_FRAME_MAX_LEN] = {0};
    size_t  frame_len;

    for (;;)
    {
        // 从队列获取AT帧（使用之前的get_at_frame函数）
        frame_len = get_at_frame(at_frame, AT_FRAME_MAX_LEN);
        if (frame_len == 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 确保字符串以'\0'结尾（避免strstr越界）
        at_frame[frame_len] = '\0'; // 覆盖末尾可能的'\r'或'\n'，或追加在后面

        // 示例1：判断是否返回成功（如"+OK\r\n"）
        if (strstr((char *) at_frame, "OK") != NULL) {}

        // 示例2：判断是否返回错误（如"+ERROR\r\n"）
        else if (strstr((char *) at_frame, "+ERROR") != NULL) {}

        // 示例3：提取参数（如"+NAME:BT401\r\n"中提取设备名）
        char *name_prefix = strstr((char *) at_frame, "+NAME:");
        if (name_prefix != NULL)
        {
            // 跳过"+NAME:"，指向实际名称（"BT401\r\n"）
            char *device_name = name_prefix + 6; // "+NAME:"长度为6
            // 截断末尾的\r\n（如果需要）
            char *crlf = strstr(device_name, "\r\n");
            if (crlf != NULL)
            {
                *crlf = '\0'; // 以'\0'终止，得到"BT401"
            }
        }

        memset(at_frame, 0, AT_FRAME_MAX_LEN); // 清空缓冲区
    }
}

// 初始化AT处理任务
void at_task_init(void)
{
    xTaskCreate(vATProcessTask, "ATProcessTask", 256, NULL, 3, NULL); // 优先级低于协议处理任务
}
