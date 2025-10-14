#include "BufferProcess.h"
#include "bt401.h"
#include "crc16.h"
#include <string.h>

QueueHandle_t xQueue_AT = NULL;
QueueHandle_t xQueue_Modbus = NULL;

void vBufferProcessTask(void *pvParameters)
{
    (void) pvParameters;

    uint8_t  temp_buf[MODBUS_FRAME_MAX_LEN] = {0};
    uint16_t temp_idx = 0;
    uint8_t  rx_byte = 0;
    uint8_t  is_at_command = 0; // 标记是否正在接收AT指令

    xQueue_AT = xQueueCreate(QUEUE_AT_LEN, AT_FRAME_MAX_LEN * sizeof(uint8_t));
    xQueue_Modbus = xQueueCreate(QUEUE_MODBUS_LEN, MODBUS_FRAME_MAX_LEN * sizeof(uint8_t));
    if ((xQueue_AT == NULL) || (xQueue_Modbus == NULL)) { vTaskDelete(NULL); }

    for (;;)
    {
        while (bt401_readbyte(&rx_byte))
        {
            // 检查是否是AT指令的开始
            if (temp_idx == 0 && rx_byte == 'A') { is_at_command = 1; }
            else if (temp_idx == 1 && rx_byte == 'T')
            {
                is_at_command = 1; // 确认是AT指令
            }

            if (temp_idx < MODBUS_FRAME_MAX_LEN - 1) { temp_buf[temp_idx++] = rx_byte; }
            else
            {
                // 缓冲区满，重置
                temp_idx = 0;
                memset(temp_buf, 0, MODBUS_FRAME_MAX_LEN);
                is_at_command = 0;
                break;
            }

            // 检查AT指令结束符(\r\n)
            if (is_at_command && temp_idx >= 2)
            {
                if ((temp_buf[temp_idx - 2] == '\r') && (temp_buf[temp_idx - 1] == '\n'))
                {
                    // 发送AT指令到队列
                    xQueueSend(xQueue_AT, temp_buf, pdMS_TO_TICKS(10));

                    // 重置缓冲区
                    temp_idx = 0;
                    memset(temp_buf, 0, MODBUS_FRAME_MAX_LEN);
                    is_at_command = 0;
                    continue;
                }
            }

            // 处理Modbus帧
            if (!is_at_command && temp_idx >= 4)
            {
                uint16_t modbus_len = 0;
                switch (temp_buf[1])
                {
                    case 0x03:
                        modbus_len = 6; // 读寄存器指令长度固定为6字节
                        break;
                    case 0x10:
                        modbus_len = 7 + temp_buf[6]; // 写多个寄存器指令长度
                        break;
                    default:
                        modbus_len = MODBUS_FRAME_MAX_LEN;
                        break;
                }

                // 检查是否接收完一帧
                if (temp_idx >= modbus_len)
                {
                    // 计算并验证CRC
                    uint16_t calc_crc = Modbus_CRC16(temp_buf, modbus_len - 2);
                    uint16_t frame_crc = (temp_buf[modbus_len - 1] << 8) | temp_buf[modbus_len - 2];

                    if (calc_crc == frame_crc)
                    {
                        // CRC验证通过，发送到Modbus队列
                        xQueueSend(xQueue_Modbus, temp_buf, pdMS_TO_TICKS(10));
                    }

                    // 重置缓冲区
                    temp_idx = 0;
                    memset(temp_buf, 0, MODBUS_FRAME_MAX_LEN);
                    continue;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
