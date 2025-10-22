#include "BufferProcess.h"
#include "bt401.h"
#include "crc16.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// 队列元素：数据指针+长度
typedef struct
{
    uint8_t *data;
    uint16_t len;
} FrameInfo_t;

// 双缓冲静态存储
#define BUFFER_NUM 2
static uint8_t s_rx_buffers[BUFFER_NUM]
                           [MODBUS_FRAME_MAX_LEN > AT_FRAME_MAX_LEN ? MODBUS_FRAME_MAX_LEN : AT_FRAME_MAX_LEN];
static uint8_t           s_current_buf_idx = 0;
static SemaphoreHandle_t xBufferMutex;

QueueHandle_t xQueue_AT = NULL;
QueueHandle_t xQueue_Modbus = NULL;

static void vBufferProcessTask(void *pvParameters)
{
    (void) pvParameters;

    uint8_t  rx_byte;
    uint16_t temp_idx = 0;
    bool     has_bytes = false;

    // 初始化互斥锁
    xBufferMutex = xSemaphoreCreateMutex();
    if (xBufferMutex == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    // 创建队列
    xQueue_AT = xQueueCreate(QUEUE_AT_LEN, sizeof(FrameInfo_t));
    xQueue_Modbus = xQueueCreate(QUEUE_MODBUS_LEN, sizeof(FrameInfo_t));
    if ((xQueue_AT == NULL) || (xQueue_Modbus == NULL))
    {
        vTaskDelete(NULL);
        return;
    }

    for (;;)
    {
        has_bytes = false;

        while (bt401_readbyte(&rx_byte))
        {
            has_bytes = true;

            // 加锁获取当前缓冲区
            xSemaphoreTake(xBufferMutex, portMAX_DELAY);
            uint8_t *current_buf = s_rx_buffers[s_current_buf_idx];
            uint16_t max_buf_len = sizeof(s_rx_buffers[0]);

            // 缓冲区满则切换（避免溢出）
            if (temp_idx >= max_buf_len)
            {
                s_current_buf_idx = (s_current_buf_idx + 1) % BUFFER_NUM;
                current_buf = s_rx_buffers[s_current_buf_idx];
                temp_idx = 0;
            }
            current_buf[temp_idx++] = rx_byte;
            xSemaphoreGive(xBufferMutex);

            // 优先处理AT帧：仅以\r\n结尾判断（不检查开头）
            if (temp_idx >= 2) // 至少2字节才可能包含\r\n
            {
                if (current_buf[temp_idx - 2] == '\r' && current_buf[temp_idx - 1] == '\n')
                {
                    // 确保不超过AT帧最大长度
                    if (temp_idx > AT_FRAME_MAX_LEN)
                    {
                        temp_idx = 0; // 超长帧丢弃
                        continue;
                    }

                    FrameInfo_t frame_info;
                    frame_info.len = temp_idx;

                    // 切换缓冲区，避免数据覆盖
                    xSemaphoreTake(xBufferMutex, portMAX_DELAY);
                    frame_info.data = current_buf;
                    s_current_buf_idx = (s_current_buf_idx + 1) % BUFFER_NUM;
                    xSemaphoreGive(xBufferMutex);

                    // 发送到AT队列
                    xQueueSend(xQueue_AT, &frame_info, pdMS_TO_TICKS(10));
                    temp_idx = 0; // 重置索引
                    continue;     // 处理完AT帧，继续接收新数据
                }
            }

            // 处理Modbus帧（地址0x01，功能码03/10，避免与AT帧混淆）
            // 最小长度：地址(1)+功能(1)+数据(≥1)+CRC(2) → 至少5字节
            if (temp_idx >= 5)
            {
                // 地址必须为0x01（核心区分特征，避免与AT帧冲突）
                if (current_buf[0] != 0x01)
                {
                    continue; // 非目标地址，不处理（继续接收，可能是AT帧）
                }

                uint8_t  function = current_buf[1];
                uint16_t modbus_len = 0;

                // 仅支持功能码03和10
                switch (function)
                {
                    case 0x03:
                        modbus_len = 8; // 固定长度：地址+功能+起始地址(2)+数量(2)+CRC(2)
                        break;
                    case 0x10:
                        // 动态长度：地址+功能+起始地址(2)+数量(2)+字节数(1)+数据(n)+CRC(2)
                        if (temp_idx > 6) // 需收到字节计数字段（索引6）
                        {
                            modbus_len = 7 + current_buf[6] + 2; // 7=固定头部长度
                            // 检查长度合法性
                            if (modbus_len > max_buf_len || modbus_len > MODBUS_FRAME_MAX_LEN)
                            {
                                temp_idx = 0; // 超长帧丢弃
                                continue;
                            }
                        }
                        else
                        {
                            continue; // 等待足够字节
                        }
                        break;
                    default:
                        continue; // 不支持的功能码，不处理
                }

                // 帧长度足够时校验CRC
                if (modbus_len > 0 && temp_idx >= modbus_len)
                {
                    uint16_t calc_crc = Modbus_CRC16(current_buf, modbus_len - 2);
                    uint16_t frame_crc =
                        (uint16_t) current_buf[modbus_len - 2] | ((uint16_t) current_buf[modbus_len - 1] << 8);

                    if (calc_crc == frame_crc) // CRC正确才入队
                    {
                        FrameInfo_t frame_info;
                        frame_info.len = modbus_len;
                        xSemaphoreTake(xBufferMutex, portMAX_DELAY);
                        frame_info.data = current_buf;
                        s_current_buf_idx = (s_current_buf_idx + 1) % BUFFER_NUM;
                        xSemaphoreGive(xBufferMutex);

                        xQueueSend(xQueue_Modbus, &frame_info, pdMS_TO_TICKS(10));
                    }
                    temp_idx = 0; // 无论CRC是否正确，重置缓冲区
                }
            }
        } // end while readbyte

        if (!has_bytes)
        {
            vTaskDelay(pdMS_TO_TICKS(5)); // 无数据时延迟，降低CPU占用
        }
    }
}

void buffer_process_init(void)
{
    xTaskCreate(vBufferProcessTask, "BufferProcessTask", 256, NULL, TASK_BUFFER_PRIO, NULL);
}

size_t get_at_frame(uint8_t *buf, size_t buflen)
{
    if (buf == NULL || buflen == 0) return 0;

    FrameInfo_t frame;
    if (xQueueReceive(xQueue_AT, &frame, portMAX_DELAY) != pdTRUE) return 0;

    size_t to_copy = (frame.len > buflen) ? buflen : frame.len;
    memcpy(buf, frame.data, to_copy);
    return to_copy;
}

size_t get_data_frame(uint8_t *buf, size_t buflen)
{
    if (buf == NULL || buflen == 0) return 0;

    FrameInfo_t frame;
    if (xQueueReceive(xQueue_Modbus, &frame, portMAX_DELAY) != pdTRUE) return 0;

    size_t to_copy = (frame.len > buflen) ? buflen : frame.len;
    memcpy(buf, frame.data, to_copy);
    return to_copy;
}
