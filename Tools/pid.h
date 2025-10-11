// pid.h
#ifndef __PID_H
#define __PID_H

#include "main.h"

// PID控制器结构体
typedef struct
{
    float   Kp;           // 比例增益
    float   Ki;           // 积分增益
    float   Kd;           // 微分增益
    float   integral;     // 积分累积值
    float   prev_error;   // 前一次误差
    float   max_integral; // 积分限幅值
    float   min_output;   // 最小输出值
    float   max_output;   // 最大输出值
    float   prev_input;   // 前一次输入值（用于微分项）
    uint8_t first_run;    // 首次运行标志
} PID_Controller;

void     PID_Init(PID_Controller *pid, float Kp, float Ki, float Kd, float max_integral, float min_output,
                  float max_output);
void     PID_Reset(PID_Controller *pid);
uint16_t PID(PID_Controller *pid, float input_temp, float target_temp, uint16_t dt_ms);

#endif
