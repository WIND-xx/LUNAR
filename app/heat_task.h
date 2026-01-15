#ifndef HEAT_TASK_H
#define HEAT_TASK_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 加热任务配置宏
 */
#define HEAT_TASK_NAME               "heat_task"      // 任务名
#define HEAT_TASK_STACK_SIZE         512              // 任务栈大小
#define HEAT_TASK_PRIORITY           3                // 任务优先级
#define HEAT_CTRL_QUEUE_LEN          5                // 控制队列长度
#define HEAT_MUTEX_LOCK_TIMEOUT_MS   50               // 互斥锁超时时间(ms)
#define HEAT_CONTROL_PERIOD_MS       500              // 加热控制周期(ms)

#define HEAT_MAX_TIMER_MINUTE        720              // 最大定时时间(分钟)
#define HEAT_NTC_FAIL_THRESHOLD      3                // NTC读取失败阈值
#define HEAT_BUZZER_BEEP_COUNT       5                // 档位到极值时蜂鸣次数

/**
 * @brief 加热状态枚举
 */
typedef enum
{
    HEAT_STATUS_STOP    = 0,    ///< 加热停止
    HEAT_STATUS_RUNNING = 1     ///< 加热运行
} HeatStatus;

/**
 * @brief 加热档位枚举
 */
typedef enum
{
    HEAT_LEVEL_1 = 0,           ///< 加热档位1
    HEAT_LEVEL_2,               ///< 加热档位2
    HEAT_LEVEL_3,               ///< 加热档位3
    HEAT_LEVEL_MAX = HEAT_LEVEL_3 ///< 最大档位（用于边界校验）
} HeatLevel;

/**
 * @brief 档位-目标温度映射表（集中配置，方便修改）
 */
typedef struct
{
    HeatLevel level;            ///< 加热档位
    float target_temp;          ///< 对应目标温度(℃)
} HeatLevelTempMap;

/**
 * @brief 初始化加热任务及相关资源
 * @retval true - 初始化成功, false - 初始化失败
 */
bool heat_task_init(void);

/**
 * @brief 反初始化加热任务（释放资源）
 */
void heat_task_deinit(void);

/**
 * @brief 设置加热运行状态
 * @param status 目标状态（HEAT_STATUS_STOP/HEAT_STATUS_RUNNING）
 * @retval true - 设置成功, false - 参数无效/队列发送失败
 */
bool heat_status_set(HeatStatus status);

/**
 * @brief 切换加热运行状态（运行↔停止）
 */
void heat_status_switch(void);

/**
 * @brief 设置加热档位
 * @param level 目标档位（HEAT_LEVEL_1~HEAT_LEVEL_3）
 * @retval true - 设置成功, false - 参数无效/队列发送失败
 */
bool heat_level_set(HeatLevel level);

/**
 * @brief 加热档位加1（到最大值后不再增加）
 */
void heat_level_up(void);

/**
 * @brief 加热档位减1（到最小值后不再减少）
 */
void heat_level_down(void);

/**
 * @brief 设置加热定时时间
 * @param minute 定时时间(分钟)，0表示取消定时，最大HEAT_MAX_TIMER_MINUTE
 * @retval true - 设置成功, false - 参数无效/队列发送失败
 */
bool heat_timer_set(uint16_t minute);

/**
 * @brief 获取当前加热状态
 * @retval 当前加热状态（HEAT_STATUS_STOP/HEAT_STATUS_RUNNING）
 */
HeatStatus heat_status_get(void);

/**
 * @brief 获取当前加热档位
 * @retval 当前加热档位（HEAT_LEVEL_1~HEAT_LEVEL_3）
 */
HeatLevel heat_level_get(void);

/**
 * @brief 获取剩余定时时间
 * @retval 剩余时间(分钟)，0表示无定时
 */
uint16_t heat_remain_time_get(void);

#endif   // HEAT_TASK_H
