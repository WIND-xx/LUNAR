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
#include "semphr.h"
#include "usart.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// 互斥信号量：保护 USART3 发送
static SemaphoreHandle_t xBt401TxMutex = NULL;
static uint8_t           dma_buffer[2][BT401_DMA_BUFFER_SIZE]; // 双缓冲区
static volatile uint8_t  current_dma_buf = 0;                  // 当前DMA使用的缓冲区索引
static volatile uint8_t  ready_buffer = 0;                     // 已准备好的缓冲区索引
static volatile uint16_t buffer_lengths[2] = {0};              // 各缓冲区数据长度

// 二值信号量：用于通知数据处理任务有新的数据帧到达
static SemaphoreHandle_t xBt401DataReadySem = NULL;

// 初始化函数
void bt401_init(void)
{
    // 创建数据就绪信号量
    xBt401DataReadySem = xSemaphoreCreateBinary();
    configASSERT(xBt401DataReadySem != NULL);

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

        // 记录准备好处理的缓冲区索引
        ready_buffer = current_dma_buf;

        // 切换DMA缓冲区，避免新数据覆盖未处理帧
        current_dma_buf = 1 - current_dma_buf;

        // 重新启动DMA接收（使用新缓冲区）
        if (HAL_UARTEx_ReceiveToIdle_DMA(&huart3, dma_buffer[current_dma_buf], BT401_DMA_BUFFER_SIZE) != HAL_OK)
        {
            Error_Handler();
        }

        // 通知处理任务有新数据到达
        xSemaphoreGiveFromISR(xBt401DataReadySem, &xHigherPriorityTaskWoken);
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
    // 等待数据就绪信号
    if (xSemaphoreTake(xBt401DataReadySem, timeout) != pdTRUE) return 0; // 超时

    // 获取准备好的数据
    frame->len = buffer_lengths[ready_buffer];
    memcpy(frame->data, dma_buffer[ready_buffer], frame->len);

    return 1; // 成功
}
