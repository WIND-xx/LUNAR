/**
 * @file ntc.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief NTC温度采样（仅浮点接口）- 修正版
 * @version 1.4
 * @date 2026-01-11
 */

#include "ntc.h"
#include "adc.h"
#include "cmsis_os2.h"
#include "main.h"
#include "stm32f1xx_hal_adc.h"
#include <math.h>
#include <stdint.h>

// 配置
#define NTC_NUM 5
#define MOVING_AVG_LEN 10
#define LOW_PASS_ALPHA 0.3f   // 浮点低通系数
#define ADC_MAX_VALUE 4095
#define TEMP_MIN_VALID (-20.0f)   // 应用层有效范围
#define TEMP_MAX_VALID (100.0f)

#ifndef NTC_USE_DMA
#    define NTC_USE_DMA 0
#endif

// 查表数据（-40°C ~ +120°C）
static const uint16_t NTC_adc_table[161] = {
    3996, 3988, 3981, 3972, 3964, 3955, 3945, 3935, 3924, 3912, 3900, 3887, 3874, 3860, 3845, 3830, 3813, 3796,
    3778, 3760, 3740, 3720, 3698, 3676, 3653, 3629, 3604, 3578, 3551, 3524, 3495, 3465, 3435, 3403, 3371, 3337,
    3303, 3267, 3231, 3194, 3156, 3118, 3078, 3038, 2997, 2955, 2913, 2870, 2826, 2782, 2738, 2693, 2648, 2602,
    2556, 2510, 2464, 2417, 2371, 2324, 2278, 2231, 2185, 2139, 2093, 2048, 2002, 1957, 1913, 1868, 1825, 1781,
    1739, 1697, 1655, 1614, 1574, 1534, 1495, 1456, 1419, 1382, 1346, 1310, 1275, 1241, 1208, 1175, 1143, 1112,
    1081, 1052, 1023, 994,  967,  940,  914,  888,  863,  839,  815,  792,  770,  748,  727,  707,  687,  668,
    649,  631,  613,  596,  579,  563,  547,  532,  517,  502,  488,  475,  462,  449,  436,  424,  413,  401,
    390,  380,  369,  359,  350,  340,  331,  322,  314,  305,  297,  289,  282,  274,  267,  260,  253,  247,
    240,  234,  228,  222,  217,  211,  206,  201,  196,  191,  186,  181,  177,  173,  168,  164,  160};

static const int16_t NTC_temperature_table[161] = {
    -40, -39, -38, -37, -36, -35, -34, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18,
    -17, -16, -15, -14, -13, -12, -11, -10, -9,  -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,  0,   1,   2,   3,   4,   5,
    6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,
    29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,
    52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,
    75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,
    98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120};

#define NTC_TABLE_SIZE (sizeof(NTC_adc_table) / sizeof(NTC_adc_table[0]))

// 全局状态
static uint32_t adc_raw[NTC_NUM];
static uint32_t adc_moving_buf[MOVING_AVG_LEN] = {0};
static uint8_t moving_idx                      = 0;
static float last_filtered_temp                = 25.0f;
static uint8_t is_first                        = 1;

// 冒泡排序（小数组，可接受）
static void bubble_sort(uint32_t* arr, uint8_t n)
{
    for (uint8_t i = 0; i < n - 1; i++)
    {
        for (uint8_t j = 0; j < n - i - 1; j++)
        {
            if (arr[j] > arr[j + 1])
            {
                uint32_t t = arr[j];
                arr[j]     = arr[j + 1];
                arr[j + 1] = t;
            }
        }
    }
}

static uint32_t median_filter(uint32_t* buf, uint8_t n)
{
    uint32_t tmp[NTC_NUM];
    for (uint8_t i = 0; i < n; i++) tmp[i] = buf[i];
    bubble_sort(tmp, n);
    return (n % 2 == 0) ? (tmp[n / 2 - 1] + tmp[n / 2]) / 2 : tmp[n / 2];
}

static uint32_t moving_average(uint32_t val)
{
    adc_moving_buf[moving_idx++] = val;
    if (moving_idx >= MOVING_AVG_LEN) moving_idx = 0;
    uint32_t sum = 0;
    for (uint8_t i = 0; i < MOVING_AVG_LEN; i++) sum += adc_moving_buf[i];
    return sum / MOVING_AVG_LEN;
}


// 简化的插值函数版本（使用二分查找优化）
static void interpolate_temperature_simple(uint32_t adc, float* p_temp)
{
    // 边界检查
    if (adc >= NTC_adc_table[0])
    {
        *p_temp = -40.0f;   // 最低温度
        return;
    }
    if (adc <= NTC_adc_table[NTC_TABLE_SIZE - 1])
    {
        *p_temp = 120.0f;   // 最高温度
        return;
    }

    // 使用二分查找定位ADC值所在的区间
    uint8_t left  = 0;
    uint8_t right = NTC_TABLE_SIZE - 1;

    while (right - left > 1)
    {
        uint8_t mid = left + (right - left) / 2;

        if (NTC_adc_table[mid] <= adc)
        {
            right = mid;
        } else
        {
            left = mid;
        }
    }

    // 此时left和right是相邻的索引，且adc在它们之间
    // NTC_adc_table[right] <= adc <= NTC_adc_table[left]
    if (NTC_adc_table[left] >= adc && NTC_adc_table[right] <= adc)
    {
        // 计算比例因子
        float ratio = (float)(NTC_adc_table[left] - adc) / (float)(NTC_adc_table[left] - NTC_adc_table[right]);

        // 计算插值温度
        *p_temp = (float)NTC_temperature_table[left] +
                  ratio * (float)(NTC_temperature_table[right] - NTC_temperature_table[left]);
    }
}

static int ntc_sample_and_filter(uint32_t* p_adc_out)
{
    uint32_t final_adc = 0;

#if NTC_USE_DMA
    // DMA模式下，暂停DMA读取数据
    osKernelLock();
    HAL_ADC_Stop_DMA(&hadc1);
    uint32_t med = median_filter(adc_raw, NTC_NUM);
    final_adc    = moving_average(med);
    HAL_ADC_Start_DMA(&hadc1, adc_raw, NTC_NUM);
    osKernelUnlock();
#else
    // 非DMA模式，轮询采样
    for (uint8_t i = 0; i < NTC_NUM; i++)
    {
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
        {
            adc_raw[i] = HAL_ADC_GetValue(&hadc1);
        } else
        {
            adc_raw[i] = ADC_MAX_VALUE / 2;   // 采样失败时使用中间值
        }
        HAL_ADC_Stop(&hadc1);
        osDelay(5);   // 短暂延时，让ADC稳定
    }
    uint32_t med = median_filter(adc_raw, NTC_NUM);
    final_adc    = moving_average(med);
#endif

    *p_adc_out = final_adc;
    return 0;
}

// === Public API ===

void ntc_init(void)
{
#if NTC_USE_DMA
    HAL_ADC_Start_DMA(&hadc1, adc_raw, NTC_NUM);
#else
    // 初始化滑动平均缓冲区为中间值
    for (uint8_t i = 0; i < MOVING_AVG_LEN; i++)
    {
        adc_moving_buf[i] = ADC_MAX_VALUE / 2;
    }
#endif
    last_filtered_temp = 25.0f;
    is_first           = 1;
    moving_idx         = 0;
}

int ntc_read(float* temperature)
{
    if (!temperature) return -1;

    uint32_t adc_val;
    if (ntc_sample_and_filter(&adc_val) != 0)
    {
        *temperature = 25.0f;   // 默认温度
        return -1;
    }

    float raw_temp = 25.0f;
    interpolate_temperature_simple(adc_val, &raw_temp);

    // 一阶低通滤波
    float filtered_temp;
    if (is_first)
    {
        is_first           = 0;
        last_filtered_temp = raw_temp;
        filtered_temp      = raw_temp;
    } else
    {
        filtered_temp      = LOW_PASS_ALPHA * raw_temp + (1.0f - LOW_PASS_ALPHA) * last_filtered_temp;
        last_filtered_temp = filtered_temp;
    }

    *temperature = filtered_temp;

    // 有效范围检查
    if (filtered_temp <= TEMP_MIN_VALID || filtered_temp >= TEMP_MAX_VALID)
    {
        return -1;   // 温度超出应用有效范围
    }

    return 0;
}

// 直接获取ADC值（用于调试）
int ntc_get_adc_value(uint32_t* adc_value)
{
    if (!adc_value) return -1;
    return ntc_sample_and_filter(adc_value);
}
