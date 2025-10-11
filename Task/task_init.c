#include "task_init.h"
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "key_task.h"
void task_init(void)
{
    // 创建按键扫描任务
    xTaskCreate(key_scan, "KeyScan", 128 * 2, NULL, 2, NULL);

    // 启动调度器
    vTaskStartScheduler();
}
