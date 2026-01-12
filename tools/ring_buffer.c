#include "ring_buffer.h"

/**
 * @brief 初始化环形缓冲区
 * @param rb 指向环形缓冲区结构体的指针
 */
void RingBuffer_Init(RingBuffer_TypeDef *rb)
{
    rb->head = 0;
    rb->tail = 0;
    rb->len = 0;
    memset((void *) rb->buf, 0, sizeof(rb->buf)); // 可选：清零缓冲区
}

/**
 * @brief 从中断服务程序（ISR）中向环形缓冲区写入一个字节
 * @param rb 指向环形缓冲区结构体的指针
 * @param data 要写入的字节
 * @return 0: 成功；1: 缓冲区已满，写入失败
 * @note 此函数是 ISR 安全的，内部使用临界区保护
 */
uint8_t RingBuffer_WriteByteFromISR(RingBuffer_TypeDef *rb, uint8_t data)
{
#if defined(USE_FREERTOS)
    UBaseType_t uxSavedInterruptStatus;
    uxSavedInterruptStatus = RINGBUF_ENTER_CRITICAL_FROM_ISR();
#else
    uint32_t dummy = RINGBUF_ENTER_CRITICAL_FROM_ISR();
    (void) dummy;
#endif

    uint8_t result;
    if (rb->len < RING_BUFFER_SIZE)
    {
        rb->buf[rb->tail] = data;
        rb->tail = (rb->tail + 1) % RING_BUFFER_SIZE;
        rb->len++;
        result = 0; // 成功
    }
    else
    {
        result = 1; // 缓冲区满，丢弃数据
    }

#if defined(USE_FREERTOS)
    RINGBUF_EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
#else
    RINGBUF_EXIT_CRITICAL_FROM_ISR(dummy);
#endif

    return result;
}

/**
 * @brief 从环形缓冲区读取一个字节（任务级调用）
 * @param rb 指向环形缓冲区结构体的指针
 * @param data 输出参数，用于存放读取到的字节
 * @return 0: 成功；1: 缓冲区为空，读取失败
 * @note 此函数是任务安全的，内部使用临界区防止与 ISR 冲突
 */
uint8_t RingBuffer_ReadByte(RingBuffer_TypeDef *rb, uint8_t *data)
{
    if (rb->len == 0)
    {
        return 0; // 缓冲区空
    }

    RINGBUF_ENTER_CRITICAL();

    *data = rb->buf[rb->head];
    rb->head = (rb->head + 1) % RING_BUFFER_SIZE;
    rb->len--;

    RINGBUF_EXIT_CRITICAL();

    return 1; // 成功
}

/**
 * @brief 从环形缓冲区读取多个字节（任务级调用）
 * @param rb 指向环形缓冲区结构体的指针
 * @param buffer 用户提供的接收缓冲区（非空）
 * @param len 要读取的字节数（必须 > 0）
 * @return 实际读取的字节数（0 ~ len）。若缓冲区数据不足，则只返回可用数据
 * @note
 *   - 该函数是原子操作（整个读取过程在临界区内完成）
 *   - 不会阻塞，立即返回可用数据
 *   - 若 buffer 为 NULL 或 len == 0，返回 0
 */
uint16_t RingBuffer_ReadBytes(RingBuffer_TypeDef *rb, uint8_t *buffer, uint16_t len)
{
    // 参数合法性检查
    if (buffer == NULL || len == 0) { return 0; }

    // 快速路径：缓冲区为空
    if (rb->len == 0) { return 0; }

    RINGBUF_ENTER_CRITICAL();

    // 实际可读取字节数（不超过请求长度和当前数据量）
    uint16_t to_read = (len < rb->len) ? len : rb->len;
    uint16_t read_count = 0;
    uint16_t current_head = rb->head;

    // 逐字节拷贝（也可优化为 memcpy，但需处理环形环绕）
    while (read_count < to_read)
    {
        buffer[read_count] = rb->buf[current_head];
        current_head = (current_head + 1) % RING_BUFFER_SIZE;
        read_count++;
    }

    // 更新 head 和 len
    rb->head = current_head;
    rb->len -= to_read;

    RINGBUF_EXIT_CRITICAL();

    return to_read;
}

/**
 * @brief 获取环形缓冲区中当前数据长度
 * @param rb 指向环形缓冲区结构体的指针
 * @return 当前缓冲区中的有效字节数
 * @note 此操作是原子的，可用于流控或状态查询
 */
uint16_t RingBuffer_GetLength(RingBuffer_TypeDef *rb)
{
#if defined(USE_FREERTOS)
    UBaseType_t uxSavedInterruptStatus;
    uxSavedInterruptStatus = RINGBUF_ENTER_CRITICAL_FROM_ISR();
#else
    uint32_t dummy = RINGBUF_ENTER_CRITICAL_FROM_ISR();
    (void) dummy;
#endif

    uint16_t len = rb->len;

#if defined(USE_FREERTOS)
    RINGBUF_EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
#else
    RINGBUF_EXIT_CRITICAL_FROM_ISR(dummy);
#endif

    return len;
}
