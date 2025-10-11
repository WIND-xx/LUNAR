#include "at_process_task.h"

void vATCommandProcessTask(void *pvParameters)
{
    (void) pvParameters;

    uint8_t at_frame[AT_FRAME_MAX_LEN] = {0};
    uint8_t response[AT_FRAME_MAX_LEN] = {0};

    for (;;)
    {
        // 从队列接收AT指令
        if (xQueueReceive(xQueue_AT, at_frame, portMAX_DELAY) == pdTRUE)
        {
            // 简单处理AT指令示例
            // 实际应用中需要根据具体AT指令集进行扩展

            if (strstr((char *) at_frame, "AT+POWER=0") != NULL)
            {
                // 关机指令
                g_registers[REG_POWER_SWITCH] = 0;
                strcpy((char *) response, "OK\r\n");
            }
            else if (strstr((char *) at_frame, "AT+POWER=1") != NULL)
            {
                // 开机指令
                g_registers[REG_POWER_SWITCH] = 1;
                strcpy((char *) response, "OK\r\n");
            }
            else if (strstr((char *) at_frame, "AT+HEATING=") != NULL)
            {
                // 设置热敷档位
                uint8_t level = atoi((char *) at_frame + 10);
                if (level >= 0 && level <= 5)
                { // 假设最大档位是5
                    g_registers[REG_HEATING_LEVEL] = level;
                    strcpy((char *) response, "OK\r\n");
                }
                else { strcpy((char *) response, "ERROR\r\n"); }
            }
            else if (strcmp((char *) at_frame, "AT+STATUS?\r\n") == 0)
            {
                // 查询状态
                sprintf((char *) response, "POWER=%d,HEATING=%d,LEVEL=%d\r\n", g_registers[REG_POWER_SWITCH],
                        g_registers[REG_HEATING_STATUS], g_registers[REG_HEATING_LEVEL]);
            }
            else if (strcmp((char *) at_frame, "AT\r\n") == 0)
            {
                // 测试指令
                strcpy((char *) response, "OK\r\n");
            }
            else
            {
                // 未知指令
                strcpy((char *) response, "ERROR\r\n");
            }

            // 发送响应
            for (uint16_t i = 0; i < strlen((char *) response); i++)
            {
                bt401_sendbyte(response[i]);
            }

            // 清空缓冲区
            memset(at_frame, 0, AT_FRAME_MAX_LEN);
            memset(response, 0, AT_FRAME_MAX_LEN);
        }
    }
}