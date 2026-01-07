#ifndef HEAT_TASK_H
#define HEAT_TASK_H

#include <stdbool.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"
#include "timers.h"
#include "pid.h"

// Forward declaration
typedef struct HeatController HeatController;

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

// 加热控制状态结构体
typedef struct
{
    HeatStatus status; // 加热状态（0-停止，1-运行）
    HeatLevel  level;
    float      target_temperature; // 目标温度
    uint16_t   set_time;           // 设置的定时时间（分钟）
    uint32_t   remain_sec;         // 剩余时间（秒）
    bool       is_timing;          // 是否处于定时状态
} Heat_t;

// 配置结构体
typedef struct
{
    uint8_t  queueSize;              // 控制消息队列深度
    uint16_t taskStackSize;          // FreeRTOS任务栈大小
    uint8_t  taskPriority;           // 任务调度优先级
    uint16_t mutexTimeoutMs;         // 互斥锁获取超时
    uint16_t maxTimerMinutes;        // 最大加热时长
    uint8_t  ntcMaxFailCount;        // 温度读取失败阈值
    uint16_t controlPeriodNormalMs;  // 正常控制周期
    uint16_t controlPeriodHighMs;    // 高温控制周期
    uint16_t controlPeriodIdleMs;    // 空闲轮询周期
    float    highTempThreshold;      // 高温阈值
} HeatConfig;

// 硬件抽象接口
typedef struct
{
    void (*heatOn)(uint16_t duty);          // 启动加热
    void (*heatOff)(void);                  // 停止加热
    void (*ledOn)(void);                    // 打开LED
    void (*ledOff)(void);                   // 关闭LED
    void (*ledTimeDisplay)(uint32_t seconds); // 显示剩余时间
    void (*buzzerBeep)(uint16_t ms);        // 蜂鸣器提示
    int  (*readTemperature)(float* temp);   // 读取温度
} HeatHwInterface;

// 操作接口（函数指针表）
typedef struct
{
    void   (*init)(HeatController* self);                        // 初始化
    bool   (*setStatus)(HeatController* self, HeatStatus status); // 设置状态
    bool   (*setLevel)(HeatController* self, HeatLevel level);    // 设置档位
    bool   (*setTimer)(HeatController* self, uint16_t minute);    // 设置定时
    void   (*statusSwitch)(HeatController* self);                 // 切换状态
    void   (*levelUp)(HeatController* self);                      // 提升档位
    void   (*levelDown)(HeatController* self);                    // 降低档位
    Heat_t (*getState)(HeatController* self);                     // 获取状态
} HeatOperations;

// 主控制器结构体
struct HeatController
{
    Heat_t            state;          // 当前加热状态
    HeatHwInterface*  hwInterface;    // 硬件抽象接口
    pid_t             pidController;  // PID控制器
    SemaphoreHandle_t mutex;          // 状态保护互斥锁
    QueueHandle_t     ctrlQueue;      // 消息队列
    TimerHandle_t     heatingTimer;   // 加热定时器
    TimerHandle_t     remainTimer;    // 倒计时定时器
    HeatConfig        config;         // 配置参数
    HeatOperations    ops;            // 操作函数表
    uint8_t           ntcFailCount;   // NTC失败计数
};

// ===========================================
// 工厂函数和硬件接口
// ===========================================

// 创建控制器实例
HeatController* HeatController_Create(const HeatConfig* config, HeatHwInterface* hwInterface);

// 获取默认配置
HeatConfig HeatController_GetDefaultConfig(void);

// 创建默认硬件接口
HeatHwInterface* HeatController_CreateDefaultHwInterface(void);

// ===========================================
// 向后兼容的全局API（内部使用默认控制器实例）
// ===========================================

// 初始化加热任务及定时相关资源
void heat_task_init(void);

// 设置加热定时（外部调用，如Modbus任务）
bool heat_set_timer(uint16_t minute);

// 启动/停止加热（外部调用）
bool heat_set_status(HeatStatus status);
void heat_status_switch(void);

// 设置加热档位（外部调用）
bool heat_set_level(HeatLevel level);
void heat_level_up(void);
void heat_level_down(void);

// 获取全局加热状态（向后兼容）
extern Heat_t heat;

#endif // HEAT_TASK_H
