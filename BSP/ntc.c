/**
 * @file ntc.c
 * @author ChenGaoxin (3180200199@qq.com)
 * @brief
 * @version 0.1
 * @date 2025-09-19
 *
 * @copyright Copyright (c) 2025
 *
 */
#include "ntc.h"
#include "adc.h"
#include "math.h"
#include "stm32f1xx_hal_adc.h"

#define NTC_NUM 5

uint32_t adc_raw[NTC_NUM]; // adc原始数据

// 如果定义为1，使用DMA方式采集ADC；定义为0时使用阻塞轮询（非DMA）采集
#ifndef NTC_USE_DMA
#define NTC_USE_DMA 1
#endif

// NTC参数配置
#define NTC_RESISTANCE    10000 // NTC标称电阻值(Ω)
#define NTC_BETA          3950  // B常数
#define SERIES_RESISTANCE 10000 // 串联电阻值(Ω)
#define REFERENCE_VOLTAGE 3300  // 参考电压(mV)
#define ADC_MAX_VALUE     4095  // ADC最大值(12位)

void NTC_Init(void)
{
#if NTC_USE_DMA
    HAL_ADC_Start_DMA(&hadc1, adc_raw, NTC_NUM);
#else
    // 非DMA模式：确保ADC已初始化，提前采样一次或不做任何事
    // 用户可在 NTC_Read 中触发单次转换
#endif
}

// 冒泡排序函数，用于中值滤波
static void bubble_sort(uint32_t *array, uint8_t size)
{
    for (uint8_t i = 0; i < size - 1; i++)
    {
        for (uint8_t j = 0; j < size - i - 1; j++)
        {
            if (array[j] > array[j + 1])
            {
                uint32_t temp = array[j];
                array[j] = array[j + 1];
                array[j + 1] = temp;
            }
        }
    }
}

// 中值滤波函数
static uint32_t median_filter(uint32_t *data_buffer, uint8_t size)
{
    uint32_t sorted_data[NTC_NUM];

    // 复制数据到临时数组
    for (uint8_t i = 0; i < size; i++)
    {
        sorted_data[i] = data_buffer[i];
    }

    // 排序
    bubble_sort(sorted_data, size);

    // 返回中值
    return sorted_data[size / 2];
}

// 根据ADC值计算NTC电阻值
static float calculate_ntc_resistance(uint32_t adc_value)
{
    // ADC值为0或接近ADC最大值都会导致不合理的计算，进行边界保护
    if (adc_value == 0 || adc_value >= ADC_MAX_VALUE)
    {
        return -1.0f; // 表示无效电阻值
    }

    // 计算NTC电阻值
    // ADC采样到的电压 = Vref * (ADC_value / ADC_MAX_VALUE)
    // Vntc = Vref * (adc_value / ADC_MAX_VALUE)
    // Rntc = Rseries * (Vntc / (Vref - Vntc))
    float v_ratio = (float) adc_value / (float) ADC_MAX_VALUE;
    return (float) SERIES_RESISTANCE * (v_ratio / (1.0f - v_ratio));
}

// 根据NTC电阻值计算温度(℃)
static float calculate_temperature(float resistance)
{
    // 使用Steinhart-Hart方程计算温度
    // 1/T = 1/T0 + 1/B * ln(R/R0)
    // T0 = 25℃ = 298.15K

    static const float T0 = 298.15f; // 25℃对应的开尔文温度
    static const float R0 = NTC_RESISTANCE;
    static const float B = NTC_BETA;

    float ln_ratio = logf(resistance / R0);
    float temp_kelvin = 1.0f / (1.0f / T0 + ln_ratio / B);

    // 转换为摄氏度
    return temp_kelvin - 273.15f;
}

// 读取NTC温度
int NTC_Read(float *temperature)
{
    int ret = 0;

#if NTC_USE_DMA
    // DMA模式：停止DMA以读取稳定数据（短时间内）
    HAL_ADC_Stop_DMA(&hadc1);
    // 中值滤波
    uint32_t filtered_adc = median_filter(adc_raw, NTC_NUM);
    // 重新启动DMA
    HAL_ADC_Start_DMA(&hadc1, adc_raw, NTC_NUM);
#else
    // 非DMA模式：进行 NTC_NUM 次单次ADC转换并存入 adc_raw，然后中值滤波
    for (uint8_t i = 0; i < NTC_NUM; i++)
    {
        // 触发单次转换并等待完成
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) { adc_raw[i] = HAL_ADC_GetValue(&hadc1); }
        else
        {
            adc_raw[i] = 0; // 超时或错误，记录0以供后续检测
        }
        HAL_ADC_Stop(&hadc1);
        // 短延时，给采样电路恢复时间（可调整或由上层提供）
        for (volatile int d = 0; d < 1000; d++)
        {
            __NOP();
        }
    }
    uint32_t filtered_adc = median_filter(adc_raw, NTC_NUM);
#endif

    float ntc_resistance = calculate_ntc_resistance(filtered_adc);
    if (ntc_resistance < 0.0f)
    {
        // 无效读数
        *temperature = 0.0f;
        return -1;
    }

    *temperature = calculate_temperature(ntc_resistance);
    if (*temperature < -20.0f || *temperature > 100.0f) { ret = -1; }
    return ret;
}
