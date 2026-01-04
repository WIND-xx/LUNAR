/**
 * @file ntc.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief NTC温度采样优化（解决跳变问题）
 * @version 0.2
 * @date 2025-09-19
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "ntc.h"
#include "adc.h"
#include "cmsis_os2.h"
#include "main.h"   // 需包含HAL_Delay所需的头文件
#include "math.h"
#include "stm32f1xx_hal_adc.h"


// 优化：增加采样样本数，提升滤波效果（5→10）
#define NTC_NUM 10
// 滑动平均缓存长度（越多越平滑，响应越慢，建议8~16）
#define MOVING_AVG_LEN 8
// 一阶低通滤波系数（0~1，越接近1越平滑，0.2~0.5为宜）
#define LOW_PASS_ALPHA 0.3f

uint32_t adc_raw[NTC_NUM];   // adc原始数据
// 滑动平均缓存（存储最近N次滤波后的ADC值）
static uint32_t adc_moving_buf[MOVING_AVG_LEN] = {0};
// 滑动平均缓存索引
static uint8_t moving_idx = 0;
// 上一次的温度值（用于低通滤波）
static float last_temp = 0.0f;
// 标记是否首次采样
static uint8_t is_first_sample = 1;

// 如果定义为1，使用DMA方式采集ADC；定义为0时使用阻塞轮询（非DMA）采集
#ifndef NTC_USE_DMA
#    define NTC_USE_DMA 0
#endif

// NTC参数配置
#define NTC_RESISTANCE 10000      // NTC标称电阻值(Ω)
#define NTC_BETA 3950             // B常数
#define SERIES_RESISTANCE 10000   // 串联电阻值(Ω)
#define REFERENCE_VOLTAGE 3300    // 参考电压(mV)
#define ADC_MAX_VALUE 4095        // ADC最大值(12位)
// 优化：采样间隔（ms），确保采样电路稳定
#define NTC_SAMPLE_INTERVAL 5

void NTC_Init(void)
{
#if NTC_USE_DMA
    HAL_ADC_Start_DMA(&hadc1, adc_raw, NTC_NUM);
#else
    // 非DMA模式：初始化滑动平均缓存（首次填充默认值）
    for (uint8_t i = 0; i < MOVING_AVG_LEN; i++)
    {
        adc_moving_buf[i] = ADC_MAX_VALUE / 2;
    }
#endif
    // 初始化上一次温度值（默认25℃）
    last_temp = 25.0f;
}

// 冒泡排序函数，用于中值滤波
static void bubble_sort(uint32_t* array, uint8_t size)
{
    for (uint8_t i = 0; i < size - 1; i++)
    {
        for (uint8_t j = 0; j < size - i - 1; j++)
        {
            if (array[j] > array[j + 1])
            {
                uint32_t temp = array[j];
                array[j]      = array[j + 1];
                array[j + 1]  = temp;
            }
        }
    }
}

// 中值滤波函数
static uint32_t median_filter(uint32_t* data_buffer, uint8_t size)
{
    uint32_t sorted_data[NTC_NUM];

    // 复制数据到临时数组
    for (uint8_t i = 0; i < size; i++)
    {
        sorted_data[i] = data_buffer[i];
    }

    // 排序
    bubble_sort(sorted_data, size);

    // 返回中值（优化：奇数个样本取中间，偶数个取中间两个的平均）
    if (size % 2 == 0)
    {
        return (sorted_data[size / 2 - 1] + sorted_data[size / 2]) / 2;
    } else
    {
        return sorted_data[size / 2];
    }
}

// 滑动平均滤波（基于中值滤波后的ADC值）
static uint32_t moving_average_filter(uint32_t filtered_adc)
{
    // 将新值存入缓存
    adc_moving_buf[moving_idx++] = filtered_adc;
    if (moving_idx >= MOVING_AVG_LEN)
    {
        moving_idx = 0;
    }

    // 计算平均值
    uint32_t sum = 0;
    for (uint8_t i = 0; i < MOVING_AVG_LEN; i++)
    {
        sum += adc_moving_buf[i];
    }
    return sum / MOVING_AVG_LEN;
}

// 根据ADC值计算NTC电阻值
static float calculate_ntc_resistance(uint32_t adc_value)
{
    // ADC值为0或接近ADC最大值都会导致不合理的计算，进行边界保护
    if (adc_value == 0 || adc_value >= ADC_MAX_VALUE)
    {
        return -1.0f;   // 表示无效电阻值
    }

    // 计算NTC电阻值
    float v_ratio = (float)adc_value / (float)ADC_MAX_VALUE;
    return (float)SERIES_RESISTANCE * (v_ratio / (1.0f - v_ratio));
}

// 根据NTC电阻值计算温度(℃)
static float calculate_temperature(float resistance)
{
    static const float T0 = 298.15f;   // 25℃对应的开尔文温度
    static const float R0 = NTC_RESISTANCE;
    static const float B  = NTC_BETA;

    float ln_ratio    = logf(resistance / R0);
    float temp_kelvin = 1.0f / (1.0f / T0 + ln_ratio / B);

    // 转换为摄氏度（保留1位小数，减少跳变）
    float temp_c = temp_kelvin - 273.15f;
    return (float)((int)(temp_c * 10)) / 10.0f;
}

// 一阶低通滤波（平滑温度值）
static float low_pass_filter(float current_temp)
{
    if (is_first_sample)
    {
        is_first_sample = 0;
        last_temp       = current_temp;
        return current_temp;
    }

    // 一阶低通公式：输出 = 系数*当前值 + (1-系数)*上一次值
    float filtered_temp = LOW_PASS_ALPHA * current_temp + (1 - LOW_PASS_ALPHA) * last_temp;
    last_temp           = filtered_temp;
    // 保留1位小数，进一步减少跳变
    return (float)((int)(filtered_temp * 10)) / 10.0f;
}

// 读取NTC温度（优化版）
int NTC_Read(float* temperature)
{
    int ret               = 0;
    uint32_t filtered_adc = 0;
    uint32_t final_adc    = 0;

#if NTC_USE_DMA
    // DMA模式：停止DMA以读取稳定数据
    HAL_ADC_Stop_DMA(&hadc1);
    // 第一步：中值滤波去毛刺
    filtered_adc = median_filter(adc_raw, NTC_NUM);
    // 第二步：滑动平均滤波平滑
    final_adc = moving_average_filter(filtered_adc);
    // 重新启动DMA
    HAL_ADC_Start_DMA(&hadc1, adc_raw, NTC_NUM);
#else
    // 非DMA模式：多次采样+延长间隔
    for (uint8_t i = 0; i < NTC_NUM; i++)
    {
        // 触发单次转换并等待完成
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
        {
            adc_raw[i] = HAL_ADC_GetValue(&hadc1);
        } else
        {
            adc_raw[i] = 0;
            ret        = -1;
        }
        HAL_ADC_Stop(&hadc1);

        // 优化：改用HAL_Delay，确保采样间隔稳定（5ms）
        osDelay(NTC_SAMPLE_INTERVAL);
    }
    // 第一步：中值滤波去毛刺
    filtered_adc = median_filter(adc_raw, NTC_NUM);
    // 第二步：滑动平均滤波平滑
    final_adc = moving_average_filter(filtered_adc);
#endif

    // 计算电阻和温度
    float ntc_resistance = calculate_ntc_resistance(final_adc);
    if (ntc_resistance < 0.0f)
    {
        *temperature = 0.0f;
        return -1;
    }

    float raw_temp = calculate_temperature(ntc_resistance);
    // 第三步：一阶低通滤波，最终平滑
    *temperature = low_pass_filter(raw_temp);

    // 温度范围校验（-20~100℃）
    if (*temperature < -20.0f || *temperature > 100.0f)
    {
        ret = -1;
    }

    return ret;
}
