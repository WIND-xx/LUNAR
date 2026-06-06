/**
 * @file    bsp_rtc.c
 * @brief   RTC 实时时钟驱动实现（静态分配）
 * @version 2.1
 */

#include "bsp_rtc.h"

#define UNIX_EPOCH_OFFSET 946684800U
#define BKP_INIT_REG RTC_BKP_DR1
#define BKP_INIT_MAGIC 0x1234

struct bsp_rtc_s {
    RTC_HandleTypeDef* hrtc;
    bool initialized;
};

static struct bsp_rtc_s s_inst;
static bool s_inited = false;

static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static bool rtc_is_leap(uint16_t y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static uint8_t rtc_days_in_mon(uint8_t m, uint16_t y) {
    if (m < 1 || m > 12) return 0;
    uint8_t d = days_in_month[m - 1];
    if (m == 2 && rtc_is_leap(y)) d++;
    return d;
}

static uint32_t rtc_read_cnt(void) {
    return (RTC->CNTH << 16) | RTC->CNTL;
}
static void rtc_write_cnt(uint32_t c) {
    RTC->CNTH = (c >> 16) & 0xFFFF;
    RTC->CNTL = c & 0xFFFF;
}

bsp_status_t bsp_rtc_init(bsp_rtc_t** handle, const bsp_rtc_config_t* config) {
    if (!handle || !config || !config->hrtc) return BSP_ERR_PARAM;
    if (*handle || s_inited) return BSP_ERR_BUSY;

    s_inst.hrtc = config->hrtc;
    s_inst.hrtc->Instance = RTC;

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();

    if (HAL_RTCEx_BKUPRead(s_inst.hrtc, BKP_INIT_REG) != BKP_INIT_MAGIC) {
        __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);
        uint32_t timeout = 0xFFFF;
        while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == RESET && timeout-- > 0);

        if (timeout == 0) {
            __HAL_RCC_LSI_ENABLE();
            timeout = 0xFFFF;
            while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET && timeout-- > 0);
            if (timeout == 0) return BSP_ERR_HW;
            MODIFY_REG(RCC->BDCR, RCC_BDCR_RTCSEL, RCC_BDCR_RTCSEL_LSI);
        } else {
            MODIFY_REG(RCC->BDCR, RCC_BDCR_RTCSEL, RCC_BDCR_RTCSEL_LSE);
        }

        SET_BIT(RCC->BDCR, RCC_BDCR_RTCEN);
        CLEAR_BIT(RTC->CRL, RTC_CRL_RSF);
        while (!(RTC->CRL & RTC_CRL_RSF));
        SET_BIT(RTC->CRL, RTC_CRL_CNF);
        RTC->PRLH = 0;
        RTC->PRLL = __HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) ? 0x7FFF : 0x9C3F;
        CLEAR_BIT(RTC->CRL, RTC_CRL_CNF);
        while (!(RTC->CRL & RTC_CRL_RTOFF));
        HAL_RTCEx_BKUPWrite(s_inst.hrtc, BKP_INIT_REG, BKP_INIT_MAGIC);
    } else {
        CLEAR_BIT(RTC->CRL, RTC_CRL_RSF);
        while (!(RTC->CRL & RTC_CRL_RSF));
    }

    s_inst.initialized = true;
    s_inited = true;
    *handle = (bsp_rtc_t*)&s_inst;
    return BSP_OK;
}

void bsp_rtc_deinit(bsp_rtc_t** handle) {
    if (!handle || !*handle || !s_inited) return;
    s_inst.initialized = false;
    s_inited = false;
    *handle = NULL;
}

bsp_status_t bsp_rtc_set_datetime(bsp_rtc_t* handle, const bsp_rtc_datetime_t* dt) {
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    if (!dt) return BSP_ERR_PARAM;
    if (dt->year > 99 || dt->month < 1 || dt->month > 12 || dt->day < 1 ||
        dt->day > rtc_days_in_mon(dt->month, 2000 + dt->year) || dt->hour > 23 || dt->minute > 59 || dt->second > 59)
        return BSP_ERR_PARAM;
    return bsp_rtc_set_utc(handle, bsp_rtc_datetime_to_utc(dt));
}

bsp_status_t bsp_rtc_get_datetime(bsp_rtc_t* handle, bsp_rtc_datetime_t* dt) {
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    if (!dt) return BSP_ERR_PARAM;
    bsp_rtc_utc_to_datetime(bsp_rtc_get_utc(handle), dt);
    return BSP_OK;
}

uint32_t bsp_rtc_get_utc(bsp_rtc_t* handle) {
    if (!handle || !s_inited) return 0;
    uint32_t c1, c2;
    do {
        c1 = rtc_read_cnt();
        c2 = rtc_read_cnt();
    } while (c1 != c2);
    return c1 + UNIX_EPOCH_OFFSET;
}

bsp_status_t bsp_rtc_set_utc(bsp_rtc_t* handle, uint32_t utc) {
    if (!handle || !s_inited) return BSP_ERR_NOTINIT;
    uint32_t c = utc - UNIX_EPOCH_OFFSET;
    SET_BIT(RTC->CRL, RTC_CRL_CNF);
    while (!(RTC->CRL & RTC_CRL_CNF));
    rtc_write_cnt(c);
    CLEAR_BIT(RTC->CRL, RTC_CRL_CNF);
    while (!(RTC->CRL & RTC_CRL_RTOFF));
    return BSP_OK;
}

void bsp_rtc_utc_to_datetime(uint32_t utc, bsp_rtc_datetime_t* dt) {
    if (!dt) return;
    uint32_t sec = utc - UNIX_EPOCH_OFFSET;
    dt->second = sec % 60;
    sec /= 60;
    dt->minute = sec % 60;
    sec /= 60;
    dt->hour = sec % 24;
    uint32_t total_days = sec / 24, days = total_days;
    for (dt->year = 0;; dt->year++) {
        uint32_t diy = rtc_is_leap(2000 + dt->year) ? 366 : 365;
        if (days < diy) break;
        days -= diy;
    }
    for (dt->month = 1;; dt->month++) {
        uint8_t dim = rtc_days_in_mon(dt->month, 2000 + dt->year);
        if (days < dim) break;
        days -= dim;
    }
    dt->day = days + 1;
    dt->weekday = (total_days + 6) % 7;
}

uint32_t bsp_rtc_datetime_to_utc(const bsp_rtc_datetime_t* dt) {
    if (!dt) return 0;
    uint32_t sec = 0;
    for (uint8_t y = 0; y < dt->year; y++) sec += rtc_is_leap(2000 + y) ? 366 : 365;
    for (uint8_t m = 1; m < dt->month; m++) sec += rtc_days_in_mon(m, 2000 + dt->year);
    sec += dt->day - 1;
    return sec * 86400 + dt->hour * 3600 + dt->minute * 60 + dt->second + UNIX_EPOCH_OFFSET;
}
