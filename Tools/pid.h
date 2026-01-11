/**
 * @file pid.h
 * @brief 工业级PID控制器头文件（使用真实浮点温度）
 *
 * @note 所有温度值均为真实浮点数（单位：°C），如 25.5f 表示 25.5°C
 */

#ifndef PID_H
#define PID_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    PID_MODE_MANUAL = 0,
    PID_MODE_AUTO,
    PID_MODE_STANDBY
} pid_mode_t;

typedef struct pid_controller_s
{
    // 公共参数
    pid_mode_t mode;
    float setpoint;    // 设定值 (°C)
    float input;       // 输入值 (°C)
    uint16_t output;   // 输出值 (PWM 0-1000)

    // PID参数（基于°C）
    float kp;
    float ki;   // 单位：1/s
    float kd;

    // 积分项
    float integral;
    float integral_limit;
    float integral_deadband;   // (°C)

    // 微分项
    float last_input;
    float last_error;
    float derivative;
    float d_filter_coef;

    // 输出限制
    uint16_t output_min;
    uint16_t output_max;

    // 抗积分饱和
    bool anti_windup;
    float windup_threshold;   // (°C)

    // 死区控制（总宽度，单位：°C）
    float deadband;   // 如 2.0f 表示 ±1.0°C（总宽 2°C）

    // 微分先行
    bool derivative_on_measurement;

    // 内部状态
    bool _first_sample;
    uint32_t _last_update_ms;

} pid_controller_t;

// 初始化
void pid_init(pid_controller_t* pid, float kp, float ki, float kd, float deadband, uint16_t out_min, uint16_t out_max);

// 计算输出（必须传时间戳）
uint16_t pid_calc_with_time(pid_controller_t* pid, float measure, float setpoint, uint32_t current_time_ms);

// 其他辅助函数
void pid_reset(pid_controller_t* pid);
void pid_set_mode(pid_controller_t* pid, pid_mode_t mode);
void pid_set_tunings(pid_controller_t* pid, float kp, float ki, float kd);
void pid_set_output_limits(pid_controller_t* pid, uint16_t min, uint16_t max);
void pid_set_integral_params(pid_controller_t* pid, float deadband, float limit);
void pid_set_derivative_filter(pid_controller_t* pid, float coef);

#endif   // PID_H
