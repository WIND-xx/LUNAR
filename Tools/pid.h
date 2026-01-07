
/**
 * @file pid.h
 * @brief PID控制器模块（C语言面向对象风格）
 */
#ifndef __PID_H
#define __PID_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief PID控制器实例结构体
 */
typedef struct {
    float kp;             ///< 比例增益
    float ki;             ///< 积分增益
    float kd;             ///< 微分增益
    float integral;       ///< 累积的积分项
    float prev_input;     ///< 上一次的过程变量（用于测量微分）
    float max_integral;   ///< 积分项的抗饱和限制
    float min_output;     ///< 最小允许输出值
    float max_output;     ///< 最大允许输出值
    bool  first_run;      ///< 首次调用时跳过微分的标志
} pid_t;

/**
 * @brief PID控制器句柄类型
 */
typedef pid_t* pid_p;

/**
 * @brief 初始化PID控制器实例
 *
 * @param[in] self        PID实例指针
 * @param[in] kp          比例增益
 * @param[in] ki          积分增益
 * @param[in] kd          微分增益
 * @param[in] max_integral 积分项的最大绝对值（抗饱和）
 * @param[in] min_output  最小输出值（例如：0.0f）
 * @param[in] max_output  最大输出值（例如：100.0f）
 */
void pid_init(pid_p self, float kp, float ki, float kd,
              float max_integral, float min_output, float max_output);

/**
 * @brief 重置PID控制器的内部状态（清除积分等）
 *
 * @param[in] self PID实例指针
 */
void pid_reset(pid_p self);

/**
 * @brief 计算PID输出
 *
 * 使用"测量微分"方式以避免微分冲击。
 * 当误差超过±10.0时禁用积分，以防止大瞬态期间的饱和。
 *
 * @param[in] self        PID实例指针
 * @param[in] input       当前过程变量（例如：温度）
 * @param[in] setpoint    目标值
 * @param[in] dt_ms       自上次调用以来的时间间隔，单位为毫秒
 * @return                限幅后的输出，uint16_t类型（例如：占空比0~100或0~1000）
 */
uint16_t pid_compute(pid_p self, float input, float setpoint, uint16_t dt_ms);

#ifdef __cplusplus
}
#endif

#endif // __PID_H
