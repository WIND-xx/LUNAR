#ifndef BUFFER_PROCESS_H
#define BUFFER_PROCESS_H

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "stm32f1xx_hal.h"
#include "task.h"

// -------------------------- 配置参数 --------------------------
#define AT_FRAME_MAX_LEN     64 // AT帧最大长度（含\r\n）
#define MODBUS_FRAME_MAX_LEN 64 // Modbus帧最大长度（含CRC）
#define QUEUE_AT_LEN         5  // AT队列缓存数
#define QUEUE_MODBUS_LEN     5  // Modbus队列缓存数
#define TASK_BUFFER_PRIO     5  // 任务优先级
// -------------------------- 全局变量/队列声明 --------------------------

// -------------------------- 函数声明 --------------------------

void   buffer_process_init(void);
size_t get_at_frame(uint8_t *buf, size_t buflen);
size_t get_data_frame(uint8_t *buf, size_t buflen);

#endif
