/**
 * @file pid.c
 * @brief 工业级PID控制器实现（位置式算法，使用真实浮点温度）
 */

#include "pid.h"
#include <math.h>
#include <string.h>

#define DEFAULT_INTEGRAL_LIMIT 500.0f
#define DEFAULT_D_FILTER_COEF 0.3f
#define DEFAULT_WINDUP_THRESHOLD 5.0f
#define DEFAULT_INTEGRAL_DEADBAND 0.5f
#define MAX_TIME_DELTA_MS 2000   // 超时重置阈值

static float _pid_compute_derivative(pid_controller_t* pid, float error_c, float dt_s);
static float _pid_compute_derivative_on_measurement(pid_controller_t* pid, float current_c, float dt_s);
static void _pid_clamp_integral(pid_controller_t* pid);
static void _pid_update_integral(pid_controller_t* pid, float error_c, float dt_s);

void pid_init(pid_controller_t* pid, float kp, float ki, float kd, float deadband, uint16_t out_min, uint16_t out_max)
{
    if (!pid) return;
    memset(pid, 0, sizeof(pid_controller_t));

    pid->kp         = kp;
    pid->ki         = ki;
    pid->kd         = kd;
    pid->deadband   = deadband;
    pid->output_min = out_min;
    pid->output_max = out_max;

    pid->mode                      = PID_MODE_AUTO;
    pid->integral_limit            = DEFAULT_INTEGRAL_LIMIT;
    pid->d_filter_coef             = DEFAULT_D_FILTER_COEF;
    pid->windup_threshold          = DEFAULT_WINDUP_THRESHOLD;
    pid->integral_deadband         = DEFAULT_INTEGRAL_DEADBAND;
    pid->anti_windup               = true;
    pid->derivative_on_measurement = true;

    pid_reset(pid);
}

uint16_t pid_calc_with_time(pid_controller_t* pid, float measure, float setpoint, uint32_t current_time_ms)
{
    if (!pid || pid->mode != PID_MODE_AUTO)
    {
        return pid ? pid->output : 0;
    }

    pid->input    = measure;
    pid->setpoint = setpoint;
    float error_c = setpoint - measure;

    // 死区处理：对称死区（总宽 deadband）
    if (pid->deadband > 0.0f)
    {
        float half_deadband = pid->deadband * 0.5f;
        if (fabsf(error_c) < half_deadband)
        {
            error_c = 0.0f;
        }
    }

    // 首次样本初始化
    if (pid->_first_sample)
    {
        pid->last_input      = measure;
        pid->last_error      = error_c;
        pid->_last_update_ms = current_time_ms;
        pid->_first_sample   = false;

        // 首次输出只计算比例项
        float output_val = pid->kp * error_c;
        if (output_val > pid->output_max)
            output_val = pid->output_max;
        else if (output_val < pid->output_min)
            output_val = pid->output_min;

        pid->output = (uint16_t)output_val;
        return pid->output;
    }

    // 安全计算时间差
    uint32_t dt_ms = current_time_ms - pid->_last_update_ms;
    if (dt_ms == 0)
    {
        // 时间无变化，保持当前输出
        return pid->output;
    }

    if (dt_ms > MAX_TIME_DELTA_MS)
    {
        // 超时保护：重置控制器状态
        pid_reset(pid);
        pid->_last_update_ms = current_time_ms;
        pid->last_input      = measure;
        pid->last_error      = error_c;
        return pid->output;
    }

    float dt_s = dt_ms / 1000.0f;

    // ========== 位置式PID核心计算 ==========

    // 1. 微分项（需要使用上一次的测量值/误差）
    float d_term = 0.0f;
    if (pid->kd != 0.0f)
    {
        if (pid->derivative_on_measurement)
        {
            d_term = _pid_compute_derivative_on_measurement(pid, measure, dt_s);
        } else
        {
            d_term = _pid_compute_derivative(pid, error_c, dt_s);
        }
    }

    // 2. 比例项
    float p_term = pid->kp * error_c;

    // 3. 计算预输出（使用当前积分项）
    float output_val = p_term + pid->integral + d_term;

    // 4. 检查是否饱和（用于抗积分饱和）
    bool output_saturated    = false;
    bool saturation_positive = false;

    if (output_val > pid->output_max)
    {
        output_saturated    = true;
        saturation_positive = true;
    } else if (output_val < pid->output_min)
    {
        output_saturated    = true;
        saturation_positive = false;
    }

    // 5. 更新积分项（带抗饱和和死区处理）
    if (!output_saturated || (saturation_positive && error_c < 0) ||   // 正向饱和但误差为负，可以减小积分
        (!saturation_positive && error_c > 0))                         // 负向饱和但误差为正，可以增加积分
    {
        // 可以安全更新积分
        _pid_update_integral(pid, error_c, dt_s);
    }
    // 否则：输出饱和且误差方向与饱和方向一致，冻结积分（抗饱和）

    // 6. 重新计算总输出（积分可能已更新）
    output_val = p_term + pid->integral + d_term;

    // 7. 最终输出限幅
    if (output_val > pid->output_max)
        output_val = pid->output_max;
    else if (output_val < pid->output_min)
        output_val = pid->output_min;

    // 8. 更新状态
    pid->output          = (uint16_t)output_val;
    pid->_last_update_ms = current_time_ms;
    pid->last_input      = measure;
    pid->last_error      = error_c;

    return pid->output;
}

static void _pid_update_integral(pid_controller_t* pid, float error_c, float dt_s)
{
    // 积分死区处理：死区内不更新积分
    if (fabsf(error_c) > pid->integral_deadband)
    {
        pid->integral += pid->ki * error_c * dt_s;
        _pid_clamp_integral(pid);
    }
    // 死区内保持积分不变
}

static float _pid_compute_derivative(pid_controller_t* pid, float error_c, float dt_s)
{
    // 使用误差的微分
    float error_rate = (error_c - pid->last_error) / dt_s;
    // 一阶低通滤波
    pid->derivative = pid->d_filter_coef * pid->derivative + (1.0f - pid->d_filter_coef) * error_rate;
    return pid->kd * pid->derivative;
}

static float _pid_compute_derivative_on_measurement(pid_controller_t* pid, float current_c, float dt_s)
{
    // 使用测量值的微分（微分先行）
    float input_rate = (current_c - pid->last_input) / dt_s;
    // 一阶低通滤波，负号是因为测量值增加意味着误差减小
    pid->derivative = pid->d_filter_coef * pid->derivative + (1.0f - pid->d_filter_coef) * (-input_rate);
    return pid->kd * pid->derivative;
}

static void _pid_clamp_integral(pid_controller_t* pid)
{
    if (pid->integral > pid->integral_limit)
        pid->integral = pid->integral_limit;
    else if (pid->integral < -pid->integral_limit)
        pid->integral = -pid->integral_limit;
}

void pid_reset(pid_controller_t* pid)
{
    if (!pid) return;
    pid->integral        = 0.0f;
    pid->derivative      = 0.0f;
    pid->last_input      = 0.0f;
    pid->last_error      = 0.0f;
    pid->output          = 0;
    pid->_first_sample   = true;
    pid->_last_update_ms = 0;
}

void pid_set_mode(pid_controller_t* pid, pid_mode_t mode)
{
    if (!pid) return;

    pid_mode_t old_mode = pid->mode;
    pid->mode           = mode;

    if (mode == PID_MODE_STANDBY)
    {
        pid->output = 0;
    } else if (mode == PID_MODE_AUTO && old_mode != PID_MODE_AUTO)
    {
        // 从非自动模式切换到自动模式时重置
        pid_reset(pid);
    }
}

void pid_set_tunings(pid_controller_t* pid, float kp, float ki, float kd)
{
    if (!pid) return;

    // 保存参数
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void pid_set_output_limits(pid_controller_t* pid, uint16_t min, uint16_t max)
{
    if (!pid || min >= max) return;

    pid->output_min = min;
    pid->output_max = max;

    // 限幅当前输出
    if (pid->output < min) pid->output = min;
    if (pid->output > max) pid->output = max;

    // 重新限幅积分项
    _pid_clamp_integral(pid);
}

void pid_set_integral_params(pid_controller_t* pid, float deadband, float limit)
{
    if (!pid) return;

    pid->integral_deadband = deadband;
    pid->integral_limit    = limit;

    // 确保当前积分值在新限制内
    _pid_clamp_integral(pid);
}

void pid_set_derivative_filter(pid_controller_t* pid, float coef)
{
    if (!pid || coef < 0.0f || coef > 1.0f) return;
    pid->d_filter_coef = coef;
}
