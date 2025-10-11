#include "pid.h"
#include <math.h>
#include <stdint.h>
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
// 初始化PID控制器
void PID_Init(PID_Controller *pid, float Kp, float Ki, float Kd, float max_integral, float min_output, float max_output)
{
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev_input = 0.0f;
    pid->max_integral = max_integral;
    pid->min_output = min_output;
    pid->max_output = max_output;
    pid->first_run = 1;
}

// 重置PID控制器
void PID_Reset(PID_Controller *pid)
{
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->prev_input = 0.0f;
    pid->first_run = 1;
}

// PID计算函数
uint16_t PID(PID_Controller *pid, float input, float setpoint, uint16_t dt_ms)
{
    float error = setpoint - input;
    float dt_sec = dt_ms / 1000.0f;

    // 比例项
    float P = pid->Kp * error;

    // 积分项
    if (fabsf(error) < 10.0f) // 只在误差较小时启用积分
    {
        pid->integral += pid->Ki * error * dt_sec;
    }
    else
    {
        pid->integral = 0.0f; // 误差过大时清空积分
    }
    // 积分限幅
    if (pid->integral > pid->max_integral)
        pid->integral = pid->max_integral;
    else if (pid->integral < -pid->max_integral)
        pid->integral = -pid->max_integral;

    // 微分项
    float D = 0.0f;
    if (!pid->first_run)
    {
        float input_derivative = (input - pid->prev_input) / dt_sec;
        D = -pid->Kd * input_derivative;
    }
    else
    {
        pid->first_run = 0;
    }
    pid->prev_input = input;

    // 计算输出
    float output = P + pid->integral + D;
    output = CLAMP(output, pid->min_output, pid->max_output);

    return (uint16_t) (output + 0.5f);
}
