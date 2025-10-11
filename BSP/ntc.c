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

// NTC参数配置
#define NTC_RESISTANCE    10000 // NTC标称电阻值(Ω)
#define NTC_BETA          3950  // B常数
#define SERIES_RESISTANCE 10000 // 串联电阻值(Ω)
#define REFERENCE_VOLTAGE 3300  // 参考电压(mV)
#define ADC_MAX_VALUE     4095  // ADC最大值(12位)

void NTC_Init(void)
{
    HAL_ADC_Start_DMA(&hadc1, adc_raw, NTC_NUM);
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
    if (adc_value >= ADC_MAX_VALUE)
    {
        return 0.0; // 防止除零错误
    }

    // 计算NTC电阻值
    // R = R_series * (ADC_max / ADC_value - 1)
    return (float) SERIES_RESISTANCE * ((float) ADC_MAX_VALUE / (float) adc_value - 1.0f);
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

    HAL_ADC_Stop_DMA(&hadc1); // 停止DMA
    // 中值滤波
    uint32_t filtered_adc = median_filter(adc_raw, NTC_NUM);
    // 计算NTC电阻值
    float ntc_resistance = calculate_ntc_resistance(filtered_adc);
    // 计算温度
    *temperature = calculate_temperature(ntc_resistance);

    // 重新启动DMA
    HAL_ADC_Start_DMA(&hadc1, adc_raw, NTC_NUM);
    if (*temperature < -20 || *temperature > 100) { ret = -1; }
    return ret;
}
