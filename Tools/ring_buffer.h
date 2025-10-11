#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <string.h>

#define USE_FREERTOS 1
// ============================================================================
// 用户配置说明：
//   - 若使用 RTOS（如 FreeRTOS），请在包含本头文件前定义：#define USE_FREERTOS
//   - 可通过 #define RING_BUFFER_SIZE N 自定义缓冲区大小（默认 256）
// ============================================================================

#ifndef RING_BUFFER_SIZE
#define RING_BUFFER_SIZE 256 // 默认缓冲区大小，单位：字节
#endif

// ============================================================================
// 临界区保护机制抽象（根据是否使用 RTOS 自动切换）
// ============================================================================
#if defined(USE_FREERTOS)
// 使用 FreeRTOS 时，包含其内核头文件
#include "FreeRTOS.h"
#include "task.h"

// RTOS 任务级临界区：禁止任务切换（但允许中断）
#define RINGBUF_ENTER_CRITICAL()          taskENTER_CRITICAL()
#define RINGBUF_EXIT_CRITICAL()           taskEXIT_CRITICAL()

// RTOS 中断级临界区：禁止中断（或提升中断屏蔽级别）
#define RINGBUF_ENTER_CRITICAL_FROM_ISR() taskENTER_CRITICAL_FROM_ISR()
#define RINGBUF_EXIT_CRITICAL_FROM_ISR(x) taskEXIT_CRITICAL_FROM_ISR(x)

#else
// 裸机环境：使用 Cortex-M 的全局中断开关（适用于 STM32、NXP 等）
// 注意：需确保包含 CMSIS 核心头文件（如 core_cm4.h）
#include "cmsis_compiler.h" // 提供 __disable_irq / __enable_irq

// 裸机任务级临界区：直接关闭全局中断
#define RINGBUF_ENTER_CRITICAL()                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        __disable_irq();                                                                                               \
    } while (0)
#define RINGBUF_EXIT_CRITICAL()                                                                                        \
    do                                                                                                                 \
    {                                                                                                                  \
        __enable_irq();                                                                                                \
    } while (0)

#endif

// ============================================================================
// 环形缓冲区结构体定义
// - buf: 数据存储区
// - head: 读指针（由任务/主循环读取时递增）
// - tail: 写指针（由中断写入时递增）
// - len: 当前有效数据长度（避免 head == tail 时无法区分空/满）
// ============================================================================
typedef struct
{
    volatile uint8_t  buf[RING_BUFFER_SIZE]; // 环形缓冲区数组
    volatile uint16_t head;                  // 读取位置（出队）
    volatile uint16_t tail;                  // 写入位置（入队）
    volatile uint16_t len;                   // 当前数据长度
} RingBuffer_TypeDef;

// ============================================================================
// 公共 API 声明
// ============================================================================
void     RingBuffer_Init(RingBuffer_TypeDef *rb);
uint8_t  RingBuffer_WriteByteFromISR(RingBuffer_TypeDef *rb, uint8_t data);
uint8_t  RingBuffer_ReadByte(RingBuffer_TypeDef *rb, uint8_t *data);
uint16_t RingBuffer_ReadBytes(RingBuffer_TypeDef *rb, uint8_t *buffer, uint16_t len);
uint16_t RingBuffer_GetLength(RingBuffer_TypeDef *rb);

#endif // RING_BUFFER_H
