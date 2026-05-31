#ifndef __BSP_RTC_H
#define __BSP_RTC_H

#include "rtc.h"
#include "stm32f1xx_hal.h"

// 自定义日期时间结构体(不使用HAL的RTC_DateTypeDef和RTC_TimeTypeDef)
typedef struct
{
    uint8_t year;    // 0-99 (表示2000-2099年)
    uint8_t month;   // 1-12
    uint8_t day;     // 1-31
    uint8_t hour;    // 0-23
    uint8_t minute;  // 0-59
    uint8_t second;  // 0-59
    uint8_t weekday; // 0-6 (0=星期日, 6=星期六)
} RTC_DateTimeTypeDef;

// 外部函数声明
HAL_StatusTypeDef RTC_Init(void);
HAL_StatusTypeDef RTC_SetDateTime(RTC_DateTimeTypeDef *datetime);
HAL_StatusTypeDef RTC_GetDateTime(RTC_DateTimeTypeDef *datetime);
uint32_t          RTC_GetUTC(void);
HAL_StatusTypeDef RTC_SetUTC(uint32_t utc);
void              RTC_UTCToDateTime(uint32_t utc, RTC_DateTimeTypeDef *datetime);
uint32_t          RTC_DateTimeToUTC(RTC_DateTimeTypeDef *datetime);

// 外部RTC句柄
extern RTC_HandleTypeDef hrtc;

#endif /* __BSP_RTC_H */
