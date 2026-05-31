/**
 * @file ntc.h
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief NTC温度采样头文件（查表法 + 插值，仅支持float接口）
 * @version 1.2
 * @date 2026-01-11
 *
 * @copyright Copyright (c) 2026
 */

#ifndef __NTC_H
#define __NTC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ntc_init(void);
int ntc_read(float* temperature);// 浮点接口：温度 ℃（如 25.5f）
int ntc_get_adc_value(uint32_t* adc_value);// 获取ADC采样值
int ntc_get_raw_samples(uint32_t* buffer, uint8_t size);// 获取原始采样值
void ntc_set_temperature_offset(float offset);// 设置温度补偿偏移量
void ntc_reset_filter(void);// 重置滤波器状态
bool ntc_is_fault(void);// 查询NTC故障状态
void ntc_clear_fault(void);// 清除NTC故障状态

#ifdef __cplusplus
}
#endif

#endif /* __NTC_H */
