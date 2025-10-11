#include "rtc.h"
#include <stdbool.h>

// 备份寄存器相关定义
#define BKP_INIT_REG   RTC_BKP_DR1 // 使用备份寄存器1
#define BKP_INIT_MAGIC 0x1234      // 初始化标记

// RTC句柄定义
RTC_HandleTypeDef hrtc;

// 内部函数声明
static bool     is_leap_year(uint16_t year);
static uint8_t  get_days_in_month(uint8_t month, uint16_t year);
static uint32_t rtc_get_counter(void);
static void     rtc_set_counter(uint32_t counter);

// 每月天数表(非闰年)
static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

// 初始化RTC(使用HAL库的备份寄存器函数)
HAL_StatusTypeDef RTC_Init(void)
{
    // 初始化RTC句柄
    hrtc.Instance = RTC;

    // 使能PWR和BKP时钟(使用HAL库宏)
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_BKP_CLK_ENABLE();

    // 允许访问备份域(使用HAL库函数)
    HAL_PWR_EnableBkUpAccess();

    // 检查RTC是否已经初始化(使用HAL的备份寄存器函数)
    if (HAL_RTCEx_BKUPRead(&hrtc, BKP_INIT_REG) != BKP_INIT_MAGIC)
    {
        // 配置LSE作为RTC时钟源
        __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);

        // 等待LSE稳定
        uint32_t timeout = 0xFFFF;
        while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == RESET && timeout-- > 0)
            ;

        if (timeout == 0)
        {
            // LSE启动失败，尝试使用LSI
            __HAL_RCC_LSI_ENABLE();
            timeout = 0xFFFF;
            while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET && timeout-- > 0)
                ;

            if (timeout == 0)
            {
                // LSI也启动失败，返回错误
                return HAL_ERROR;
            }

            // 选择RTC时钟源为LSI
            MODIFY_REG(RCC->BDCR, RCC_BDCR_RTCSEL, RCC_BDCR_RTCSEL_LSI);
        }
        else
        {
            // 选择RTC时钟源为LSE
            MODIFY_REG(RCC->BDCR, RCC_BDCR_RTCSEL, RCC_BDCR_RTCSEL_LSE);
        }

        // 使能RTC时钟
        SET_BIT(RCC->BDCR, RCC_BDCR_RTCEN);

        // 等待RTC寄存器同步
        CLEAR_BIT(RTC->CRL, RTC_CRL_RSF);
        while ((RTC->CRL & RTC_CRL_RSF) == 0)
            ;

        // 进入配置模式
        SET_BIT(RTC->CRL, RTC_CRL_CNF);

        // 设置预分频器
        if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY))
        {
            // LSE为32768Hz，设置预分频得到1Hz
            RTC->PRLH = 0x0000;
            RTC->PRLL = 0x7FFF; // 32767
        }
        else
        {
            // LSI约为40000Hz，设置预分频得到1Hz
            RTC->PRLH = 0x0000;
            RTC->PRLL = 0x9C3F; // 39999
        }

        // 退出配置模式
        CLEAR_BIT(RTC->CRL, RTC_CRL_CNF);

        // 等待操作完成
        while ((RTC->CRL & RTC_CRL_RTOFF) == 0)
            ;

        // 标记RTC已初始化(使用HAL的备份寄存器函数)
        HAL_RTCEx_BKUPWrite(&hrtc, BKP_INIT_REG, BKP_INIT_MAGIC);
    }
    else
    {
        // RTC已初始化，等待寄存器同步
        CLEAR_BIT(RTC->CRL, RTC_CRL_RSF);
        while ((RTC->CRL & RTC_CRL_RSF) == 0)
            ;
    }

    return HAL_OK;
}

// 设置RTC日期时间(完全自定义实现)
HAL_StatusTypeDef RTC_SetDateTime(RTC_DateTimeTypeDef *datetime)
{
    if (datetime == NULL) return HAL_ERROR;

    // 检查日期时间有效性
    if (datetime->year > 99 || datetime->month < 1 || datetime->month > 12 || datetime->day < 1 ||
        datetime->day > get_days_in_month(datetime->month, 2000 + datetime->year) || datetime->hour > 23 ||
        datetime->minute > 59 || datetime->second > 59)
    {
        return HAL_ERROR;
    }

    // 转换为UTC时间戳并设置
    uint32_t utc = RTC_DateTimeToUTC(datetime);
    return RTC_SetUTC(utc);
}

// 获取当前日期时间(完全自定义实现)
HAL_StatusTypeDef RTC_GetDateTime(RTC_DateTimeTypeDef *datetime)
{
    if (datetime == NULL) return HAL_ERROR;

    // 获取当前UTC时间戳并转换为日期时间
    uint32_t utc = RTC_GetUTC();
    RTC_UTCToDateTime(utc, datetime);

    return HAL_OK;
}

// 获取当前UTC时间戳
uint32_t RTC_GetUTC(void)
{
    uint32_t counter1, counter2;

    // 读取计数器值，确保读取稳定
    do
    {
        counter1 = rtc_get_counter();
        counter2 = rtc_get_counter();
    } while (counter1 != counter2);

    return counter1 + 946684800; // 加上2000年到1970年的秒数差
}

// 设置UTC时间戳
HAL_StatusTypeDef RTC_SetUTC(uint32_t utc)
{
    // 转换为2000年为基准的计数器值
    uint32_t counter = utc - 946684800; // 946684800是2000-01-01 00:00:00的UTC时间戳

    // 进入配置模式
    SET_BIT(RTC->CRL, RTC_CRL_CNF);

    // 等待配置模式进入
    while ((RTC->CRL & RTC_CRL_CNF) == 0)
        ;

    // 设置计数器值
    rtc_set_counter(counter);

    // 退出配置模式
    CLEAR_BIT(RTC->CRL, RTC_CRL_CNF);

    // 等待操作完成
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0)
        ;

    return HAL_OK;
}

// 从UTC时间戳转换为日期时间(完全自定义算法)
void RTC_UTCToDateTime(uint32_t utc, RTC_DateTimeTypeDef *datetime)
{
    if (datetime == NULL) return;

    // 转换为2000年为基准的秒数
    uint32_t seconds = utc - 946684800; // 946684800是2000-01-01 00:00:00的UTC时间戳

    datetime->second = seconds % 60;
    seconds /= 60;

    datetime->minute = seconds % 60;
    seconds /= 60;

    datetime->hour = seconds % 24;
    uint32_t days = seconds / 24;

    // 计算年份(2000年开始)
    datetime->year = 0;
    while (1)
    {
        uint16_t year = 2000 + datetime->year;
        uint32_t days_in_year = is_leap_year(year) ? 366 : 365;

        if (days < days_in_year) break;

        days -= days_in_year;
        datetime->year++;
    }

    // 计算月份和日期
    datetime->month = 1;
    while (1)
    {
        uint16_t year = 2000 + datetime->year;
        uint8_t  dim = get_days_in_month(datetime->month, year);

        if (days < dim) break;

        days -= dim;
        datetime->month++;
    }

    datetime->day = days + 1; // 天数从1开始

    // 计算星期几 (2000-01-01是星期六)
    datetime->weekday = (days + 6) % 7; // 0=星期日, 6=星期六
}

// 从日期时间转换为UTC时间戳(完全自定义算法)
uint32_t RTC_DateTimeToUTC(RTC_DateTimeTypeDef *datetime)
{
    if (datetime == NULL) return 0;

    uint32_t seconds = 0;

    // 计算从2000年到指定年份的总天数
    for (uint8_t y = 0; y < datetime->year; y++)
    {
        seconds += is_leap_year(2000 + y) ? 366 : 365;
    }

    // 计算从年初到指定月份的总天数
    for (uint8_t m = 1; m < datetime->month; m++)
    {
        seconds += get_days_in_month(m, 2000 + datetime->year);
    }

    // 加上日期、小时、分钟和秒
    seconds += (datetime->day - 1);
    seconds = seconds * 86400 + datetime->hour * 3600 + datetime->minute * 60 + datetime->second;

    // 加上2000年到1970年的秒数差
    return seconds + 946684800;
}

// 内部函数：判断是否为闰年
static bool is_leap_year(uint16_t year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// 内部函数：获取月份的天数
static uint8_t get_days_in_month(uint8_t month, uint16_t year)
{
    if (month < 1 || month > 12) return 0;

    uint8_t days = days_in_month[month - 1];

    // 处理闰年的2月
    if (month == 2 && is_leap_year(year)) days++;

    return days;
}

// 内部函数：读取RTC计数器值
static uint32_t rtc_get_counter(void)
{
    return (RTC->CNTH << 16) | RTC->CNTL;
}

// 内部函数：设置RTC计数器值
static void rtc_set_counter(uint32_t counter)
{
    RTC->CNTH = (counter >> 16) & 0xFFFF;
    RTC->CNTL = counter & 0xFFFF;
}
