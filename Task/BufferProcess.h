#ifndef BUFFER_PROCESS_H
#define BUFFER_PROCESS_H

#include "FreeRTOS.h"
#include "queue.h"
#include "stm32f1xx_hal.h"
#include "task.h"

// -------------------------- 配置参数（需根据实际场景修改） --------------------------
#define AT_FRAME_MAX_LEN     100 // AT指令返回帧最大长度（如+NAME:BT05\r\n）
#define MODBUS_FRAME_MAX_LEN 64  // Modbus帧最大长度（含CRC）
#define TASK_BUFFER_PRIO     3   // 缓冲处理任务优先级（高于AT/Modbus任务）
#define QUEUE_AT_LEN         5   // AT队列长度（最多缓存5个AT帧）
#define QUEUE_MODBUS_LEN     5   // Modbus队列长度（最多缓存5个Modbus帧）

// -------------------------- 全局变量/队列声明 --------------------------

extern QueueHandle_t xQueue_AT;     // AT帧处理队列
extern QueueHandle_t xQueue_Modbus; // Modbus帧处理队列

// -------------------------- 函数声明 --------------------------
// 缓冲处理任务（入口函数）
void vBufferProcessTask(void *pvParameters);

#endif
