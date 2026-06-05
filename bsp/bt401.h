#ifndef BT401_H
#define BT401_H

#include "FreeRTOS.h"  // 用于TickType_t类型
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// DMA缓冲区大小（需与源文件保持一致）
#define BT401_DMA_BUFFER_SIZE 128

// DMA缓冲区数量（可调整以增加接收并发帧数）
#ifndef BT401_DMA_BUFFER_COUNT
#define BT401_DMA_BUFFER_COUNT 4
#endif

// 帧结构：存储一帧完整数据（供外部解析使用）
#pragma pack(1)
typedef struct {
    uint8_t data[BT401_DMA_BUFFER_SIZE];  // 帧数据缓冲区
    uint16_t len;                         // 实际帧长度（<= BT401_DMA_BUFFER_SIZE）
} frame_t;
#pragma pack()

void bt401_init(void);
uint8_t bt401_sendbytes(uint8_t* buf, uint16_t len);
int bt401_printf(const char* format, ...);
uint8_t bt401_get_frame(frame_t* frame, TickType_t timeout);
uint32_t bt401_get_overrun_count(void);  // DMA溢出计数

#endif  // BT401_H
