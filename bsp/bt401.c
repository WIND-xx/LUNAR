/**
 * @file bt401.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief
 * @version 0.1
 * @date 2025-11-05
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "bt401.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "usart.h"

// 互斥信号量：保护 USART3 发送
static SemaphoreHandle_t xBt401TxMutex = NULL;

// 可配置数量的DMA缓冲区
static uint8_t           dma_buffer[BT401_DMA_BUFFER_COUNT][BT401_DMA_BUFFER_SIZE];
static volatile uint8_t  current_dma_buf = 0;                          // 当前DMA使用的缓冲区索引
static volatile uint16_t buffer_lengths[BT401_DMA_BUFFER_COUNT] = {0}; // 各缓冲区数据长度

// 就绪队列：从ISR发送就绪缓冲区索引到处理任务
static QueueHandle_t xBt401ReadyQ = NULL;

// 初始化函数
void bt401_init(void)
{
    // 创建数据就绪信号量
    // 创建就绪队列（存放就绪缓冲区索引）
    xBt401ReadyQ = xQueueCreate(BT401_DMA_BUFFER_COUNT, sizeof(uint8_t));
    configASSERT(xBt401ReadyQ != NULL);

    // 禁用DMA半传输中断
    __HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT);

    // 启动IDLE中断+DMA接收（初始使用缓冲区0）
    current_dma_buf = 0;
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buffer[current_dma_buf], BT401_DMA_BUFFER_SIZE) != HAL_OK)
    {
        Error_Handler();
    }

    // 创建发送互斥信号量
    if (xBt401TxMutex == NULL)
    {
        xBt401TxMutex = xSemaphoreCreateMutex();
        configASSERT(xBt401TxMutex != NULL);
    }
}

// UART Rx Event Callback - 关键：捕获完整帧并切换缓冲区
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // 仅处理 USART3 的 IDLE 事件
    if (huart->Instance == USART3 && HAL_UARTEx_GetRxEventType(huart) == HAL_UART_RXEVENT_IDLE)
    {
        // 保存当前缓冲区的数据长度
        buffer_lengths[current_dma_buf] = Size;

        // 将就绪缓冲区索引发送到队列，供处理任务弹出
        uint8_t idx = current_dma_buf;
        // 若队列已满，xQueueSendFromISR会失败；为了保证最近的帧被接收，尝试覆盖最旧元素：
        if (xQueueSendFromISR(xBt401ReadyQ, &idx, &xHigherPriorityTaskWoken) != pdTRUE)
        {
            // 队列满：为简洁起见，先尝试移除一个旧项再发送（非阻塞）
            uint8_t discard;
            xQueueReceiveFromISR(xBt401ReadyQ, &discard, &xHigherPriorityTaskWoken);
            xQueueSendFromISR(xBt401ReadyQ, &idx, &xHigherPriorityTaskWoken);
        }

        // 切换到下一个缓冲区索引
        current_dma_buf = (current_dma_buf + 1) % BT401_DMA_BUFFER_COUNT;

        // 重新启动DMA接收（使用新缓冲区）
        if (HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buffer[current_dma_buf], BT401_DMA_BUFFER_SIZE) != HAL_OK)
        {
            Error_Handler();
        }
    }

    // 若有高优先级任务被唤醒，立即调度
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

// 原始字节发送（带互斥保护）
uint8_t bt401_sendbytes(uint8_t *buf, uint16_t len)
{
    if (xBt401TxMutex == NULL) return 1;

    if (xSemaphoreTake(xBt401TxMutex, pdMS_TO_TICKS(100)) != pdTRUE) return 1;

    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart3, buf, len, HAL_MAX_DELAY);
    xSemaphoreGive(xBt401TxMutex);

    return (status == HAL_OK) ? 1 : 0;
}

// 线程安全的 printf 风格发送
int bt401_printf(const char *format, ...)
{
    if (xBt401TxMutex == NULL) return -1;

    if (xSemaphoreTake(xBt401TxMutex, pdMS_TO_TICKS(100)) != pdTRUE) return -1;

#define BT401_PRINTF_BUF_SIZE 128
    char buf[BT401_PRINTF_BUF_SIZE];
    int  len;

    va_list args;
    va_start(args, format);
    len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (len < 0)
        len = 0;
    else if ((size_t) len >= sizeof(buf))
        len = sizeof(buf) - 1;

    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart3, (uint8_t *) buf, len, HAL_MAX_DELAY);
    xSemaphoreGive(xBt401TxMutex);

    return (status == HAL_OK) ? len : 0;
}

// 供解析任务调用：获取一帧数据（阻塞等待）
uint8_t bt401_get_frame(frame_t *frame, TickType_t timeout)
{
    if (xBt401ReadyQ == NULL) return 0;

    uint8_t idx;
    // 从队列获取就绪缓冲区索引（阻塞至 timeout）
    if (xQueueReceive(xBt401ReadyQ, &idx, timeout) != pdTRUE) return 0; // 超时

    // 拷贝数据到调用者提供的 frame
    frame->len = buffer_lengths[idx];
    memcpy(frame->data, dma_buffer[idx], frame->len);

    return 1; // 成功
}
