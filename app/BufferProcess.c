/**
 * @file BufferProcess.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief
 * @version 0.1
 * @date 2025-11-06
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "BufferProcess.h"
#include "FreeRTOS.h"
#include "bt401.h"
#include "crc16.h"
#include "protocal.h"
#include "task.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static void vBufferProcessTask(void *pvParameters)
{
    (void) pvParameters;
    frame_t frame = {0};

    for (;;)
    {
        // 使用bt401_get_frame接口获取完整帧
        if (bt401_get_frame(&frame, portMAX_DELAY))
        {
            // 处理Modbus帧（地址0x01，功能码03/10）
            if (frame.len >= 5 && frame.data[0] == 0x01 && (frame.data[1] == 0x03 || frame.data[1] == 0x10))
            {
                decode_protocal(frame.data, frame.len);
            }
            // 处理AT帧：以\r\n结尾判断
            if (frame.len >= 2 && frame.data[frame.len - 2] == '\r' && frame.data[frame.len - 1] == '\n')
            {
                // at_decode(frame.data, frame.len);
            }
            frame.len = 0;                             // 重置帧长度以准备下一次接收
            memset(frame.data, 0, sizeof(frame.data)); // 清空数据缓冲区
        }
        else { vTaskDelay(pdMS_TO_TICKS(100)); }
    }
}

void buffer_process_init(void)
{
    xTaskCreate(vBufferProcessTask, "BufferProcessTask", 256, NULL, 5, NULL);
}
