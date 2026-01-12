/**
 * @file pid.c
 * @brief 工业级PID控制器实现（使用真实浮点温度）
 */

#include "pid.h"
#include <math.h>
#include <string.h>

#define DEFAULT_INTEGRAL_LIMIT 500.0f
#define DEFAULT_D_FILTER_COEF 0.3f
#define DEFAULT_WINDUP_THRESHOLD 5.0f
#define DEFAULT_INTEGRAL_DEADBAND 0.5f

static float _pid_compute(pid_controller_t* pid, float error_c, float dt_s);
static void _pid_update_integral(pid_controller_t* pid, float error_c, float dt_s);
static float _pid_compute_derivative(pid_controller_t* pid, float error_c, float dt_s);
static float _pid_compute_derivative_on_measurement(pid_controller_t* pid, float current_c, float dt_s);
static void _pid_clamp_integral(pid_controller_t* pid);

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

    if (pid->_first_sample)
    {
        pid->last_input      = measure;
        pid->last_error      = error_c;
        pid->_last_update_ms = current_time_ms;
        pid->_first_sample   = false;
        return pid->output;
    }

    uint32_t dt_ms = current_time_ms - pid->_last_update_ms;
    float dt_s     = (dt_ms == 0 || dt_ms > 5000) ? 0.1f : (float)dt_ms / 1000.0f;

    float output_change = _pid_compute(pid, error_c, dt_s);

    int32_t new_output = (int32_t)pid->output + (int32_t)output_change;
    if (new_output > (int32_t)pid->output_max)
        new_output = pid->output_max;
    else if (new_output < (int32_t)pid->output_min)
        new_output = pid->output_min;
    pid->output = (uint16_t)new_output;

    pid->_last_update_ms = current_time_ms;
    pid->last_input      = measure;

    return pid->output;
}

static float _pid_compute(pid_controller_t* pid, float error_c, float dt_s)
{
    if (dt_s <= 0.0f) return 0.0f;

    float p_term = pid->kp * error_c;

    _pid_update_integral(pid, error_c, dt_s);
    float i_term = pid->integral;

    float d_term = 0.0f;
    if (pid->kd != 0.0f)
    {
        if (pid->derivative_on_measurement)
        {
            d_term = _pid_compute_derivative_on_measurement(pid, pid->input, dt_s);
        } else
        {
            d_term = _pid_compute_derivative(pid, error_c, dt_s);
        }
    }

    if (pid->anti_windup && fabsf(error_c) > pid->windup_threshold)
    {
        pid->integral *= 0.98f;
    }

    return p_term + i_term + d_term;
}

static void _pid_update_integral(pid_controller_t* pid, float error_c, float dt_s)
{
    if (fabsf(error_c) <= pid->integral_deadband)
    {
        pid->integral += error_c * pid->ki * dt_s;
        _pid_clamp_integral(pid);
    } else
    {
        pid->integral *= 0.95f;
    }
}

static float _pid_compute_derivative(pid_controller_t* pid, float error_c, float dt_s)
{
    float error_rate = (error_c - pid->last_error) / dt_s;
    pid->last_error  = error_c;
    pid->derivative  = pid->d_filter_coef * pid->derivative + (1.0f - pid->d_filter_coef) * error_rate;
    return pid->kd * pid->derivative;
}

static float _pid_compute_derivative_on_measurement(pid_controller_t* pid, float current_c, float dt_s)
{
    float input_rate = (current_c - pid->last_input) / dt_s;
    pid->derivative  = pid->d_filter_coef * pid->derivative + (1.0f - pid->d_filter_coef) * (-input_rate);
    return pid->kd * pid->derivative;
}

static void _pid_clamp_integral(pid_controller_t* pid)
{
    if (pid->integral > pid->integral_limit)
    {
        pid->integral = pid->integral_limit;
    } else if (pid->integral < -pid->integral_limit)
    {
        pid->integral = -pid->integral_limit;
    }
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
    if (mode == PID_MODE_STANDBY)
    {
        pid->output = 0;
    } else if (mode == PID_MODE_AUTO && pid->mode != PID_MODE_AUTO)
    {
        pid_reset(pid);
    }
    pid->mode = mode;
}

void pid_set_tunings(pid_controller_t* pid, float kp, float ki, float kd)
{
    if (!pid) return;
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}

void pid_set_output_limits(pid_controller_t* pid, uint16_t min, uint16_t max)
{
    if (!pid || min >= max) return;
    pid->output_min = min;
    pid->output_max = max;
    if (pid->output < min) pid->output = min;
    if (pid->output > max) pid->output = max;
}

void pid_set_integral_params(pid_controller_t* pid, float deadband, float limit)
{
    if (!pid) return;
    pid->integral_deadband = deadband;
    pid->integral_limit    = limit;
    _pid_clamp_integral(pid);
}

void pid_set_derivative_filter(pid_controller_t* pid, float coef)
{
    if (!pid || coef < 0.0f || coef > 1.0f) return;
    pid->d_filter_coef = coef;
}
