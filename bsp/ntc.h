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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ntc_init(void);

// 浮点接口：温度 ℃（如 25.5f）
int ntc_read(float* temperature);

#ifdef __cplusplus
}
#endif

#endif /* __NTC_H */
