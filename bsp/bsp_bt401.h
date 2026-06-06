/**
 * @file    bsp_bt401.h
 * @brief   BT401 蓝牙模块驱动（UART DMA + IDLE 中断，无 OS 依赖）
 * @version 2.0
 * @date    2026-06-06
 *
 * @note    BSP 层提供原始收发能力，多缓冲 + 环形帧队列。
 *          TX 非线程安全，上层需自行序列化。
 *          RX 帧队列在 ISR 中填充，get_frame 非阻塞。
 */

#ifndef BSP_BT401_H
#define BSP_BT401_H

#include "bsp_common.h"
#include "stm32f1xx_hal.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 编译期配置
 *============================================================================*/

#define BT401_DMA_BUF_SIZE 128   /**< DMA 缓冲区大小 */
#define BT401_DMA_BUF_COUNT 3    /**< DMA 缓冲池数量（3 平衡 RAM 和吞吐） */
#define BT401_FRAME_QUEUE_SIZE 4 /**< 帧队列容量 */

/*==============================================================================
 * 类型定义
 *============================================================================*/

/** 不透明 BT401 句柄 */
BSP_HANDLE_DECLARE(bt401);

/** 一帧数据 */
typedef struct {
    uint8_t data[BT401_DMA_BUF_SIZE]; /**< 帧数据 */
    uint16_t len;                     /**< 实际长度 */
} bsp_bt401_frame_t;

/** BT401 配置 */
typedef struct {
    UART_HandleTypeDef* huart; /**< HAL UART 句柄（USART3） */
} bsp_bt401_config_t;

/*==============================================================================
 * API 函数
 *============================================================================*/

/**
 * @brief 初始化 BT401 模块
 * @param[out] handle  返回的句柄
 * @param[in]  config  UART 配置
 * @return BSP_OK 成功
 */
bsp_status_t bsp_bt401_init(bsp_bt401_t** handle, const bsp_bt401_config_t* config);

/**
 * @brief 反初始化（停止 DMA + 释放资源）
 * @param handle 句柄指针的指针
 */
void bsp_bt401_deinit(bsp_bt401_t** handle);

/**
 * @brief 发送原始字节（阻塞）
 * @param handle  BT401 句柄
 * @param buf     数据缓冲区
 * @param len     数据长度
 * @return BSP_OK 成功
 *
 * @note 非线程安全。上层需自行序列化发送。
 */
bsp_status_t bsp_bt401_send(bsp_bt401_t* handle, const uint8_t* buf, uint16_t len);

/**
 * @brief 格式化发送（printf 风格，阻塞）
 * @param handle  BT401 句柄
 * @param format  格式化字符串
 * @param ...     参数
 * @return 实际发送字节数，<0 表示失败
 *
 * @note 非线程安全。
 */
int bsp_bt401_printf(bsp_bt401_t* handle, const char* format, ...);

/**
 * @brief 获取一帧接收数据（非阻塞）
 * @param handle  BT401 句柄
 * @param[out] frame  帧数据
 * @return BSP_OK=有新帧, BSP_ERROR=无新帧
 */
bsp_status_t bsp_bt401_get_frame(bsp_bt401_t* handle, bsp_bt401_frame_t* frame);

/**
 * @brief 获取 DMA 溢出计数（诊断用）
 * @return 溢出次数
 */
uint32_t bsp_bt401_get_overrun(bsp_bt401_t* handle);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BT401_H */
