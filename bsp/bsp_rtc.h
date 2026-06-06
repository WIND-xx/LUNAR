/**
 * @file    bsp_rtc.h
 * @brief   RTC 实时时钟驱动（无 OS 依赖）
 * @version 2.0
 * @date    2026-06-06
 */

#ifndef BSP_RTC_H
#define BSP_RTC_H

#include "bsp_common.h"
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 类型定义
 *============================================================================*/

/** 不透明 RTC 句柄 */
BSP_HANDLE_DECLARE(rtc);

/** 日期时间结构体 */
typedef struct {
    uint8_t year;    /**< 年（偏移 2000，如 26 = 2026） */
    uint8_t month;   /**< 月 [1, 12] */
    uint8_t day;     /**< 日 [1, 31] */
    uint8_t hour;    /**< 时 [0, 23] */
    uint8_t minute;  /**< 分 [0, 59] */
    uint8_t second;  /**< 秒 [0, 59] */
    uint8_t weekday; /**< 星期 [0=周日, 6=周六] */
} bsp_rtc_datetime_t;

/** RTC 配置 */
typedef struct {
    RTC_HandleTypeDef* hrtc; /**< HAL RTC 句柄（CubeMX 生成） */
} bsp_rtc_config_t;

/*==============================================================================
 * API 函数
 *============================================================================*/

/**
 * @brief 初始化 RTC
 * @param[out] handle  返回的 RTC 句柄
 * @param[in]  config  配置（含 HAL 句柄）
 * @return BSP_OK 成功
 */
bsp_status_t bsp_rtc_init(bsp_rtc_t** handle, const bsp_rtc_config_t* config);

/**
 * @brief 反初始化 RTC
 */
void bsp_rtc_deinit(bsp_rtc_t** handle);

/**
 * @brief 设置日期时间
 */
bsp_status_t bsp_rtc_set_datetime(bsp_rtc_t* handle, const bsp_rtc_datetime_t* dt);

/**
 * @brief 获取日期时间
 */
bsp_status_t bsp_rtc_get_datetime(bsp_rtc_t* handle, bsp_rtc_datetime_t* dt);

/**
 * @brief 获取 UTC 时间戳（自 1970-01-01 秒数）
 */
uint32_t bsp_rtc_get_utc(bsp_rtc_t* handle);

/**
 * @brief 设置 UTC 时间戳
 */
bsp_status_t bsp_rtc_set_utc(bsp_rtc_t* handle, uint32_t utc);

/**
 * @brief UTC 转日期时间
 */
void bsp_rtc_utc_to_datetime(uint32_t utc, bsp_rtc_datetime_t* dt);

/**
 * @brief 日期时间转 UTC
 */
uint32_t bsp_rtc_datetime_to_utc(const bsp_rtc_datetime_t* dt);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H */
