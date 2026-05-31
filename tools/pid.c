/**
 * @file pid.c
 * @brief 工业级PID控制器（位置式 + 抗饱和 + 微分先行 + 死区）
 * @version 1.1
 */

#include "pid.h"
#include <math.h>
#include <string.h>

#define PID_DEFAULT_INTEGRAL_LIMIT   500.0f
#define PID_DEFAULT_D_FILTER         0.3f
#define PID_DEFAULT_WINDUP_THRESHOLD 5.0f
#define PID_DEFAULT_DEADBAND_HALF    0.5f
#define PID_MAX_TIME_DELTA_MS        2000

static float pid_compute_derivative(pid_controller_t *pid, float error, float dt);
static float pid_compute_derivative_on_meas(pid_controller_t *pid, float measured, float dt);
static void  pid_clamp_integral(pid_controller_t *pid);

void pid_init(pid_controller_t *pid, float kp, float ki, float kd,
              float deadband, uint16_t out_min, uint16_t out_max)
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
    pid->integral_limit            = PID_DEFAULT_INTEGRAL_LIMIT;
    pid->d_filter_coef             = PID_DEFAULT_D_FILTER;
    pid->windup_threshold          = PID_DEFAULT_WINDUP_THRESHOLD;
    pid->integral_deadband         = PID_DEFAULT_DEADBAND_HALF;
    pid->anti_windup               = true;
    pid->derivative_on_measurement = true;

    pid_reset(pid);
}

uint16_t pid_calc_with_time(pid_controller_t *pid, float measure, float setpoint,
                            uint32_t current_time_ms)
{
    if (!pid || pid->mode != PID_MODE_AUTO) {
        return pid ? pid->output : 0;
    }

    pid->input    = measure;
    pid->setpoint = setpoint;
    float error   = setpoint - measure;

    /* 死区处理 */
    if (pid->deadband > 0.0f) {
        float half = pid->deadband * 0.5f;
        if (fabsf(error) < half) error = 0.0f;
    }

    /* 首次采样：仅比例项 */
    if (pid->_first_sample) {
        pid->last_input      = measure;
        pid->last_error      = error;
        pid->_last_update_ms = current_time_ms;
        pid->_first_sample   = false;

        float out = pid->kp * error;
        if (out > pid->output_max) out = pid->output_max;
        if (out < pid->output_min) out = pid->output_min;
        pid->output = (uint16_t)out;
        return pid->output;
    }

    /* 计算时间差 */
    uint32_t dt_ms = current_time_ms - pid->_last_update_ms;
    if (dt_ms == 0) return pid->output;

    if (dt_ms > PID_MAX_TIME_DELTA_MS) {
        pid_reset(pid);
        pid->_last_update_ms = current_time_ms;
        pid->last_input      = measure;
        pid->last_error      = error;
        return pid->output;
    }

    float dt = dt_ms / 1000.0f;

    /* 1. 微分项 */
    float d_term = 0.0f;
    if (pid->kd != 0.0f) {
        d_term = pid->derivative_on_measurement
                     ? pid_compute_derivative_on_meas(pid, measure, dt)
                     : pid_compute_derivative(pid, error, dt);
    }

    /* 2. 比例项 + 预输出 */
    float p_term   = pid->kp * error;
    float pre_out  = p_term + pid->integral + d_term;

    /* 3. 抗积分饱和判断 */
    bool saturated = (pre_out > pid->output_max || pre_out < pid->output_min);
    bool sat_pos   = (pre_out > pid->output_max);

    if (!saturated || (sat_pos && error < 0) || (!sat_pos && error > 0)) {
        if (fabsf(error) > pid->integral_deadband) {
            pid->integral += pid->ki * error * dt;
            pid_clamp_integral(pid);
        }
    }

    /* 4. 最终输出 */
    float output = p_term + pid->integral + d_term;
    if (output > pid->output_max) output = pid->output_max;
    if (output < pid->output_min) output = pid->output_min;

    /* 5. 保存状态 */
    pid->output          = (uint16_t)output;
    pid->_last_update_ms = current_time_ms;
    pid->last_input      = measure;
    pid->last_error      = error;

    return pid->output;
}

static float pid_compute_derivative(pid_controller_t *pid, float error, float dt)
{
    float rate = (error - pid->last_error) / dt;
    pid->derivative = pid->d_filter_coef * pid->derivative
                    + (1.0f - pid->d_filter_coef) * rate;
    return pid->kd * pid->derivative;
}

static float pid_compute_derivative_on_meas(pid_controller_t *pid, float measured, float dt)
{
    float rate = (measured - pid->last_input) / dt;
    pid->derivative = pid->d_filter_coef * pid->derivative
                    + (1.0f - pid->d_filter_coef) * (-rate);
    return pid->kd * pid->derivative;
}

static void pid_clamp_integral(pid_controller_t *pid)
{
    if (pid->integral > pid->integral_limit)
        pid->integral = pid->integral_limit;
    else if (pid->integral < -pid->integral_limit)
        pid->integral = -pid->integral_limit;
}

void pid_reset(pid_controller_t *pid)
{
    if (!pid) return;
    pid->integral        = 0.0f;
    pid->derivative      = 0.0f;
    pid->last_input      = 0.0f;
    pid->last_error      = 0.0f;
    pid->_first_sample   = true;
    pid->_last_update_ms = 0;
    pid->output          = 0;
}

void pid_set_mode(pid_controller_t *pid, pid_mode_t mode)
{
    if (pid) pid->mode = mode;
}

void pid_set_tunings(pid_controller_t *pid, float kp, float ki, float kd)
{
    if (!pid) return;
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void pid_set_output_limits(pid_controller_t *pid, uint16_t min, uint16_t max)
{
    if (!pid) return;
    pid->output_min = min;
    pid->output_max = max;
    if (pid->output < min) pid->output = min;
    if (pid->output > max) pid->output = max;
    pid_clamp_integral(pid);
}

void pid_set_integral_params(pid_controller_t *pid, float deadband, float limit)
{
    if (!pid) return;
    pid->integral_deadband = deadband;
    pid->integral_limit    = limit;
    pid_clamp_integral(pid);
}

void pid_set_derivative_filter(pid_controller_t *pid, float coef)
{
    if (pid) pid->d_filter_coef = coef;
}
