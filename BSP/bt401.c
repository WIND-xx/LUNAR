#include "bt401.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "usart.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// 互斥信号量：保护 USART3 发送
static SemaphoreHandle_t xBt401TxMutex = NULL;
// 环形缓冲区相关
uint8_t            usart3_rx_byte = 0;   // 单字节接收缓冲区
RingBuffer_TypeDef USART3_RingBuf = {0}; // USART1环形缓冲区（需与串口中断接收函数关联）

// USART3接收完成回调函数
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3)
    {
        // 写入环形缓冲区（临界区保护，防止与任务读冲突）
        RingBuffer_WriteByteFromISR(&USART3_RingBuf, usart3_rx_byte);
        // 重新启动中断接收（重要！）
        HAL_UART_Receive_IT(&huart3, &usart3_rx_byte, 1);
    }
}
// 初始化函数
void bt401_init(void)
{
    // 初始化环形缓冲区
    RingBuffer_Init(&USART3_RingBuf);
    HAL_UART_Receive_IT(&huart3, &usart3_rx_byte, 1);
    // 创建互斥信号量（仅一次）
    if (xBt401TxMutex == NULL)
    {
        xBt401TxMutex = xSemaphoreCreateMutex();
        configASSERT(xBt401TxMutex != NULL);
    }
}

// 原始字节发送（带互斥保护）
uint8_t bt401_sendbytes(uint8_t *buf, uint16_t len)
{
    if (xBt401TxMutex == NULL)
    {
        return 1; // 未初始化
    }

    // 获取互斥锁（带超时，避免死锁）
    if (xSemaphoreTake(xBt401TxMutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return 1; // 超时，发送失败
    }

    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart3, buf, len, HAL_MAX_DELAY);

    xSemaphoreGive(xBt401TxMutex);

    return (status == HAL_OK) ? 0 : 1;
}

// 线程安全的 printf 风格发送
int bt401_printf(const char *format, ...)
{
    if (xBt401TxMutex == NULL)
    {
        return -1;
    }

    // 获取互斥锁
    if (xSemaphoreTake(xBt401TxMutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return -1; // 获取锁失败
    }

// 使用栈上缓冲区（建议 128~256 字节，根据需求调整）
#define BT401_PRINTF_BUF_SIZE 128
    char buf[BT401_PRINTF_BUF_SIZE];
    int  len;

    va_list args;
    va_start(args, format);
    len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    // 截断超长字符串
    if (len < 0)
    {
        len = 0;
    }
    else if ((size_t) len >= sizeof(buf))
    {
        len = (int) sizeof(buf) - 1;
    }

    // 发送数据（使用 HAL_MAX_DELAY，因为已在临界区）
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart3, (uint8_t *) buf, len, HAL_MAX_DELAY);

    xSemaphoreGive(xBt401TxMutex);

    return (status == HAL_OK) ? len : -1;
}

uint8_t bt401_readbyte(uint8_t *rx_byte)
{
    return RingBuffer_ReadByte(&USART3_RingBuf, rx_byte);
}

uint16_t bt401_readbytes(uint8_t *buf, uint16_t len)
{
    return RingBuffer_ReadBytes(&USART3_RingBuf, buf, len);
}
