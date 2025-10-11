#include "heat_task.h"
#include "heat.h"
#include "ntc.h"
#include "pid.h"

#include "FreeRTOS.h"
#include "task.h"

float target_temperature = 50.0f; // 目标温度，单位摄氏度
void  heat_control_task(void *arg)
{
    (void) arg;
    static TickType_t xLastWakeTime = 0;
    xLastWakeTime = xTaskGetTickCount();
    PID_Controller heater_pid;
    PID_Init(&heater_pid, 10.0f, 0.1f, 4.5f, 50.0f, 0.0f, 100.0f);
    for (;;)
    {
        float current_temp;
        int   ret;
        ret = NTC_Read(&current_temp);

        if (ret != 0)
        {
            PID_Reset(&heater_pid);
            continue;
        }
        float pid_output = PID(&heater_pid, current_temp, target_temperature, 100);
        if (pid_output > 0) { heat_on(pid_output); }
        else { heat_off(); }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
    }
}
