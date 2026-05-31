/**
 * @file frame_process.c
 * @brief BLE数据帧分发任务（Modbus / AT 命令分流）
 * @version 1.1
 */

#include "frame_process.h"
#include "FreeRTOS.h"
#include "at_ctrl.h"
#include "at_process.h"
#include "bt401.h"
#include "crc16.h"
#include "protocol.h"
#include "task.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static void ble_data_process_task(void* pvParameters)
{
    (void)pvParameters;
    frame_t frame = {0};
    bt401_init();
    bt_start();

    for (;;) {
        if (bt401_get_frame(&frame, portMAX_DELAY)) {
            // Modbus帧：地址0x01 + 功能码03/06/10
            if (frame.len >= 5 && frame.data[0] == 0x01
                && (frame.data[1] == 0x03 || frame.data[1] == 0x06 || frame.data[1] == 0x10)) {
                protocol_handle_request(frame.data, frame.len);
            }
            // AT帧：以 \r\n 结尾
            else if (frame.len >= 2
                     && frame.data[frame.len - 2] == '\r'
                     && frame.data[frame.len - 1] == '\n') {
                decode_at_command(frame.data, frame.len);
            }

            frame.len = 0; // 重置帧长度即可，下次 memcpy 会覆盖旧数据
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

void ble_data_process_task_init(void)
{
    xTaskCreate(ble_data_process_task, "BufferProcessTask", 512, NULL, 5, NULL);
}
