/**
 * @file bt401.c
 * @brief BT401蓝牙模块驱动（UART3 DMA + IDLE中断 + 多缓冲池）
 * @version 1.1
 */

#include "bt401.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "usart.h"

/*============================================================================
 * 静态资源
 *============================================================================*/
static SemaphoreHandle_t xBt401TxMutex = NULL;
static QueueHandle_t     xBt401ReadyQ  = NULL;
static volatile bool     bt401_initialized = false;

/* DMA多缓冲池（内存对齐） */
static __ALIGN_BEGIN uint8_t
    dma_buffer[BT401_DMA_BUFFER_COUNT][BT401_DMA_BUFFER_SIZE] __ALIGN_END;
static volatile uint8_t  current_dma_buf = 0;
static volatile uint16_t buffer_lengths[BT401_DMA_BUFFER_COUNT] = {0};
static volatile uint32_t dma_overrun_count = 0;  // DMA溢出计数

/*============================================================================
 * 初始化
 *============================================================================*/
void bt401_init(void)
{
    if (bt401_initialized) return;

    xBt401ReadyQ = xQueueCreate(BT401_DMA_BUFFER_COUNT, sizeof(uint8_t));
    configASSERT(xBt401ReadyQ != NULL);

    /* 禁用DMA半传输中断（仅使用IDLE中断组帧） */
    __HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT);

    /* 启动IDLE中断 + DMA接收 */
    current_dma_buf = 0;
    HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(
        &huart3, dma_buffer[current_dma_buf], BT401_DMA_BUFFER_SIZE);
    if (status != HAL_OK) {
        /* 初始化失败不阻塞系统，记录错误 */
        return;
    }

    if (xBt401TxMutex == NULL) {
        xBt401TxMutex = xSemaphoreCreateMutex();
        configASSERT(xBt401TxMutex != NULL);
    }

    bt401_initialized = true;
}

/*============================================================================
 * UART IDLE 中断回调（ISR上下文）
 *============================================================================*/
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (huart->Instance != USART3) return;
    if (HAL_UARTEx_GetRxEventType(huart) != HAL_UART_RXEVENT_IDLE) return;

    /* 保存当前缓冲区数据长度 */
    buffer_lengths[current_dma_buf] = Size;

    /* 将就绪缓冲区索引发到队列 */
    uint8_t idx = current_dma_buf;
    if (xQueueSendFromISR(xBt401ReadyQ, &idx, &xHigherPriorityTaskWoken) != pdTRUE) {
        /* 队列满：丢弃最旧帧，存入新帧 */
        uint8_t discard;
        xQueueReceiveFromISR(xBt401ReadyQ, &discard, &xHigherPriorityTaskWoken);
        xQueueSendFromISR(xBt401ReadyQ, &idx, &xHigherPriorityTaskWoken);
        dma_overrun_count++;
    }

    /* 切换到下一个缓冲区 */
    current_dma_buf = (current_dma_buf + 1) % BT401_DMA_BUFFER_COUNT;

    /* 重新启动DMA接收 */
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart3,
            dma_buffer[current_dma_buf], BT401_DMA_BUFFER_SIZE) != HAL_OK) {
        dma_overrun_count++;
    }

    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/*============================================================================
 * 发送接口
 *============================================================================*/
uint8_t bt401_sendbytes(uint8_t *buf, uint16_t len)
{
    if (xBt401TxMutex == NULL || buf == NULL || len == 0) return 0;

    if (xSemaphoreTake(xBt401TxMutex, pdMS_TO_TICKS(100)) != pdTRUE) return 0;

    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart3, buf, len, HAL_MAX_DELAY);
    xSemaphoreGive(xBt401TxMutex);

    return (status == HAL_OK) ? 1 : 0;
}

int bt401_printf(const char *format, ...)
{
    if (xBt401TxMutex == NULL) return -1;
    if (xSemaphoreTake(xBt401TxMutex, pdMS_TO_TICKS(100)) != pdTRUE) return -1;

    #define BT401_PRINTF_BUF_SIZE 128
    char buf[BT401_PRINTF_BUF_SIZE];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (len < 0) len = 0;
    if ((size_t)len >= sizeof(buf)) len = sizeof(buf) - 1;

    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart3, (uint8_t *)buf, len, HAL_MAX_DELAY);
    xSemaphoreGive(xBt401TxMutex);

    return (status == HAL_OK) ? len : 0;
}

/*============================================================================
 * 接收接口
 *============================================================================*/
uint8_t bt401_get_frame(frame_t *frame, TickType_t timeout)
{
    if (xBt401ReadyQ == NULL || frame == NULL) return 0;

    uint8_t idx;
    if (xQueueReceive(xBt401ReadyQ, &idx, timeout) != pdTRUE) return 0;

    frame->len = buffer_lengths[idx];
    if (frame->len > BT401_DMA_BUFFER_SIZE) frame->len = BT401_DMA_BUFFER_SIZE;
    memcpy(frame->data, dma_buffer[idx], frame->len);

    return 1;
}

/*============================================================================
 * 诊断接口
 *============================================================================*/
uint32_t bt401_get_overrun_count(void)
{
    return dma_overrun_count;
}
