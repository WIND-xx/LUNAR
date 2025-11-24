#ifndef HEAT_TASK_H
#define HEAT_TASK_H

#include <stdint.h>

// 加热状态枚举
typedef enum
{
    HEAT_STOP = 0,
    HEAT_RUNNING = 1
} HeatStatus;
typedef enum
{
    HEAT_LEVEL_1,
    HEAT_LEVEL_2,
    HEAT_LEVEL_3
} HeatLevel;

// 外部声明加热控制结构体
typedef struct
{
    HeatStatus status; // 加热状态（0-停止，1-运行）
    HeatLevel  level;
    float      target_temperature; // 目标温度
    uint16_t   set_time;           // 设置的定时时间（分钟）
    uint32_t   remain_sec;         // 剩余时间（秒）
} Heat_t;

// 初始化加热任务及定时相关资源
void heat_task_init(void);

// 设置加热定时（外部调用，如Modbus任务）
void heat_set_timer(uint16_t minute);

// 启动/停止加热（外部调用）
void heat_set_status(HeatStatus status);
void heat_status_switch(void);

// 设置加热档位（外部调用）
void heat_set_level(HeatLevel level);
void heat_level_up(void);
void heat_level_down(void);

#endif // HEAT_TASK_H
