#ifndef __RTC_H
#define __RTC_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

extern RTC_HandleTypeDef hrtc;

typedef struct {
    uint8_t year, month, day, hour, minute, second, weekday;
} rtc_datetime_t;

HAL_StatusTypeDef rtc_init(void);
HAL_StatusTypeDef rtc_set_datetime(rtc_datetime_t* dt);
HAL_StatusTypeDef rtc_get_datetime(rtc_datetime_t* dt);
uint32_t rtc_get_utc(void);
HAL_StatusTypeDef rtc_set_utc(uint32_t utc);
void rtc_utc_to_datetime(uint32_t utc, rtc_datetime_t* dt);
uint32_t rtc_datetime_to_utc(rtc_datetime_t* dt);

#endif
