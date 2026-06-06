/**
 * @file    bsp_bt401.c
 * @brief   BT401 蓝牙模块驱动实现（静态分配，环形队列）
 * @version 2.1
 */

#include "bsp_bt401.h"
#include <stdio.h>
#include <string.h>

/*==============================================================================
 * 内部结构体
 *============================================================================*/

typedef struct {
    bsp_bt401_frame_t frames[BT401_FRAME_QUEUE_SIZE];
    volatile uint8_t  head;
    volatile uint8_t  tail;
    volatile uint32_t overrun;
} bt401_queue_t;

struct bsp_bt401_s {
    UART_HandleTypeDef* huart;
    uint8_t       dma_buffer[BT401_DMA_BUF_COUNT][BT401_DMA_BUF_SIZE] __attribute__((aligned(4)));
    volatile uint8_t   current_buf;
    volatile uint16_t  buf_lengths[BT401_DMA_BUF_COUNT];
    bt401_queue_t rx_queue;
    bool          initialized;
};

static struct bsp_bt401_s s_inst;
static bool s_inited = false;
static bsp_bt401_t* g_bt401_isr = NULL;

/*==============================================================================
 * 环形队列
 *============================================================================*/

static bool bt401_queue_push(uint8_t buf_idx)
{
    bt401_queue_t* q = &s_inst.rx_queue;
    uint8_t next = (q->head + 1) % BT401_FRAME_QUEUE_SIZE;
    if (next == q->tail) {
        q->tail = (q->tail + 1) % BT401_FRAME_QUEUE_SIZE;
        q->overrun++;
    }
    uint16_t len = s_inst.buf_lengths[buf_idx];
    if (len > BT401_DMA_BUF_SIZE) len = BT401_DMA_BUF_SIZE;
    memcpy(q->frames[q->head].data, s_inst.dma_buffer[buf_idx], len);
    q->frames[q->head].len = len;
    q->head = next;
    return true;
}

static bool bt401_queue_pop(bsp_bt401_frame_t* frame)
{
    bt401_queue_t* q = &s_inst.rx_queue;
    if (q->head == q->tail) return false;
    memcpy(frame, &q->frames[q->tail], sizeof(bsp_bt401_frame_t));
    q->tail = (q->tail + 1) % BT401_FRAME_QUEUE_SIZE;
    return true;
}

/*==============================================================================
 * ISR 回调
 *============================================================================*/

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef* huart, uint16_t Size)
{
    if (!g_bt401_isr || huart->Instance != USART3) return;
    if (HAL_UARTEx_GetRxEventType(huart) != HAL_UART_RXEVENT_IDLE) return;

    s_inst.buf_lengths[s_inst.current_buf] = Size;
    bt401_queue_push(s_inst.current_buf);
    s_inst.current_buf = (s_inst.current_buf + 1) % BT401_DMA_BUF_COUNT;
    HAL_UARTEx_ReceiveToIdle_DMA(huart, s_inst.dma_buffer[s_inst.current_buf], BT401_DMA_BUF_SIZE);
}

/*==============================================================================
 * 公共 API
 *============================================================================*/

bsp_status_t bsp_bt401_init(bsp_bt401_t** handle, const bsp_bt401_config_t* config)
{
    if (!handle || !config || !config->huart) return BSP_ERR_PARAM;
    if (*handle || s_inited) return BSP_ERR_BUSY;

    s_inst.huart      = config->huart;
    s_inst.rx_queue.head = s_inst.rx_queue.tail = 0;
    s_inst.rx_queue.overrun = 0;

    __HAL_DMA_DISABLE_IT(s_inst.huart->hdmarx, DMA_IT_HT);
    s_inst.current_buf = 0;
    g_bt401_isr = &s_inst;

    if (HAL_UARTEx_ReceiveToIdle_DMA(s_inst.huart, s_inst.dma_buffer[0], BT401_DMA_BUF_SIZE) != HAL_OK) {
        g_bt401_isr = NULL;
        return BSP_ERR_HW;
    }

    s_inst.initialized = true;
    s_inited = true;
    *handle = (bsp_bt401_t*)&s_inst;
    return BSP_OK;
}

void bsp_bt401_deinit(bsp_bt401_t** handle)
{
    if (!handle || !*handle || !s_inited) return;
    HAL_UART_DMAStop(s_inst.huart);
    g_bt401_isr = NULL;
    s_inst.initialized = false;
    s_inited = false;
    *handle = NULL;
}

bsp_status_t bsp_bt401_send(bsp_bt401_t* handle, const uint8_t* buf, uint16_t len)
{
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    if (!buf || !len) return BSP_ERR_PARAM;
    return (HAL_UART_Transmit(s_inst.huart, (uint8_t*)buf, len, HAL_MAX_DELAY) == HAL_OK)
               ? BSP_OK : BSP_ERR_HW;
}

int bsp_bt401_printf(bsp_bt401_t* handle, const char* format, ...)
{
    if (!handle || !s_inited) return -1;
    char buf[128];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    if (len < 0) len = 0;
    if ((size_t)len >= sizeof(buf)) len = sizeof(buf) - 1;
    return (HAL_UART_Transmit(s_inst.huart, (uint8_t*)buf, len, HAL_MAX_DELAY) == HAL_OK)
               ? len : 0;
}

bsp_status_t bsp_bt401_get_frame(bsp_bt401_t* handle, bsp_bt401_frame_t* frame)
{
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    if (!frame) return BSP_ERR_PARAM;
    __disable_irq();
    bool has = bt401_queue_pop(frame);
    __enable_irq();
    return has ? BSP_OK : BSP_ERROR;
}

uint32_t bsp_bt401_get_overrun(bsp_bt401_t* handle)
{
    return (handle && s_inited) ? s_inst.rx_queue.overrun : 0;
}
