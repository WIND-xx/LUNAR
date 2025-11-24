#ifndef BUFFERPROCESS_H
#define BUFFERPROCESS_H

#include <stddef.h>
#include <stdint.h>

// 帧最大长度定义
#define AT_FRAME_MAX_LEN     128
#define MODBUS_FRAME_MAX_LEN 64

// 初始化函数
void buffer_process_init(void);

#endif // BUFFERPROCESS_H
