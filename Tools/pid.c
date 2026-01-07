
/**
 * @file pid.c
 * @brief PID控制器的实现（面向对象风格）
 */
#include "pid.h"
#include <math.h>

// 限幅辅助宏
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))

void pid_init(pid_p self, float kp, float ki, float kd,
              float max_integral, float min_output, float max_output)
{
    self->kp = kp;
    self->ki = ki;
    self->kd = kd;
    self->max_integral = max_integral;
    self->min_output = min_output;
    self->max_output = max_output;
    pid_reset(self); // 复用reset函数来初始化状态
}

void pid_reset(pid_p self)
{
    self->integral = 0.0f;
    self->prev_input = 0.0f;
    self->first_run = true;
}

uint16_t pid_compute(pid_p self, float input, float setpoint, uint16_t dt_ms)
{
    const float error = setpoint - input;
    const float dt_sec = dt_ms / 1000.0f;

    // 比例项
    const float p_out = self->kp * error;

    // 积分项（带条件抗积分饱和）
    if (fabsf(error) < 10.0f) {
        self->integral += self->ki * error * dt_sec;
        // 限制积分值
        self->integral = CLAMP(self->integral, -self->max_integral, self->max_integral);
    } else {
        // 可选：当误差过大时禁用积分以防止积分饱和
        self->integral = 0.0f;
    }

    // 微分项（对测量值微分，负反馈）
    float d_out = 0.0f;
    if (!self->first_run) {
        const float input_derivative = (input - self->prev_input) / dt_sec;
        d_out = -self->kd * input_derivative;
    } else {
        self->first_run = false;
    }
    self->prev_input = input;

    // 总输出
    float output = p_out + self->integral + d_out;
    output = CLAMP(output, self->min_output, self->max_output);

    return (uint16_t)(output + 0.5f); // 四舍五入到最近的整数
}
