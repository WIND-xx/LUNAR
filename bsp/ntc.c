/**
 * @file ntc.c
 * @brief NTC温度采样（优化版）
 * @version 2.0
 * @date 2026-01-14
 */

#include "ntc.h"
#include "adc.h"
#include "cmsis_os2.h"
#include "main.h"
#include "stm32f1xx_hal_adc.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

// ========== 配置区域 ==========
// 根据应用需求调整以下参数
#define NTC_NUM 5                // 中值滤波样本数，建议奇数
#define MOVING_AVG_LEN 8         // 滑动平均长度（2的幂次）
#define LOW_PASS_ALPHA 0.3f      // 浮点低通系数
#define ADC_MAX_VALUE 4095       // 12位ADC最大值
#define TEMP_MIN_VALID (-20.0f)  // 应用层有效范围
#define TEMP_MAX_VALID 100.0f
#define ADC_SAMPLE_DELAY_MS 1  // 非DMA模式采样间隔

// DMA模式开关
#ifndef NTC_USE_DMA
#define NTC_USE_DMA 1  // 默认启用DMA
#endif

// 传感器故障检测阈值
#ifndef NTC_STUCK_THRESHOLD
#define NTC_STUCK_THRESHOLD 10
#endif

// DMA双缓冲区开关
#define USE_DOUBLE_BUFFER 1     // 启用双缓冲区
#define ADC_DMA_BUFFER_SIZE 16  // DMA缓冲区大小

// 快速查找表开关
#define USE_FAST_LOOKUP 1  // 启用快速查表

// ========== 查表数据 ==========
// 优化：使用const和static确保数据放在Flash
static const uint16_t NTC_adc_table[] = {
    3996, 3988, 3981, 3972, 3964, 3955, 3945, 3935, 3924, 3912, 3900, 3887, 3874, 3860, 3845, 3830, 3813, 3796,
    3778, 3760, 3740, 3720, 3698, 3676, 3653, 3629, 3604, 3578, 3551, 3524, 3495, 3465, 3435, 3403, 3371, 3337,
    3303, 3267, 3231, 3194, 3156, 3118, 3078, 3038, 2997, 2955, 2913, 2870, 2826, 2782, 2738, 2693, 2648, 2602,
    2556, 2510, 2464, 2417, 2371, 2324, 2278, 2231, 2185, 2139, 2093, 2048, 2002, 1957, 1913, 1868, 1825, 1781,
    1739, 1697, 1655, 1614, 1574, 1534, 1495, 1456, 1419, 1382, 1346, 1310, 1275, 1241, 1208, 1175, 1143, 1112,
    1081, 1052, 1023, 994,  967,  940,  914,  888,  863,  839,  815,  792,  770,  748,  727,  707,  687,  668,
    649,  631,  613,  596,  579,  563,  547,  532,  517,  502,  488,  475,  462,  449,  436,  424,  413,  401,
    390,  380,  369,  359,  350,  340,  331,  322,  314,  305,  297,  289,  282,  274,  267,  260,  253,  247,
    240,  234,  228,  222,  217,  211,  206,  201,  196,  191,  186,  181,  177,  173,  168,  164,  160};

static const int8_t NTC_temperature_table[] = {
    -40, -39, -38, -37, -36, -35, -34, -33, -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18,
    -17, -16, -15, -14, -13, -12, -11, -10, -9,  -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1,  0,   1,   2,   3,   4,   5,
    6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,
    29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,
    52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,
    75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,
    98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120};

#define NTC_TABLE_SIZE (sizeof(NTC_adc_table) / sizeof(NTC_adc_table[0]))

// ========== 全局状态 ==========
static uint32_t adc_raw[NTC_NUM];                // 原始采样数据
static uint32_t adc_moving_buf[MOVING_AVG_LEN];  // 滑动平均缓冲区
static uint8_t moving_idx = 0;                   // 滑动平均索引
static float last_filtered_temp = 25.0f;         // 上次滤波温度
static uint8_t is_first = 1;                     // 首次运行标志
static float g_temp_offset = 0.0f;               // 温度补偿偏移量

// 传感器故障检测
static uint32_t ntc_last_raw_adc = 0;  // 上一次ADC原始值
static uint8_t ntc_stuck_counter = 0;  // 卡死计数器
static bool ntc_fault_active = false;  // 故障标志

#if NTC_USE_DMA
// DMA相关变量
#if USE_DOUBLE_BUFFER
static uint32_t adc_dma_buf[2][ADC_DMA_BUFFER_SIZE];  // 双缓冲区
static volatile uint8_t active_buf = 0;               // 当前活动缓冲区
static volatile uint8_t data_ready = 0;               // 数据就绪标志
#else
static uint32_t adc_dma_buf[ADC_DMA_BUFFER_SIZE];  // 单缓冲区
#endif
#endif

// ========== 函数声明 ==========
// 快速中值滤波（使用选择算法）
static uint32_t quick_select(uint32_t arr[], uint8_t n);
static void swap(uint32_t* a, uint32_t* b);

// 快速插值函数
static float interpolate_temperature_fast(uint32_t adc);
static float low_pass_filter(float new_val, float last_val);

// ========== 优化算法实现 ==========

// 交换函数
static inline void swap(uint32_t* a, uint32_t* b)
{
    uint32_t t = *a;
    *a = *b;
    *b = t;
}

// 快速选择算法（找第k小元素）
static uint32_t quick_select(uint32_t arr[], uint8_t n)
{
    uint32_t tmp[NTC_NUM];

    // 复制数组避免修改原数据
    for (uint8_t i = 0; i < n; i++) {
        tmp[i] = arr[i];
    }

    uint8_t low = 0, high = n - 1;
    uint8_t median = n / 2;

    while (1) {
        if (high <= low) {
            return tmp[median];
        }

        uint8_t middle = low + (high - low) / 2;
        uint32_t pivot = tmp[middle];

        // 将pivot移到末尾
        swap(&tmp[middle], &tmp[high]);

        uint8_t i = low;
        for (uint8_t j = low; j < high; j++) {
            if (tmp[j] < pivot) {
                swap(&tmp[i], &tmp[j]);
                i++;
            }
        }

        swap(&tmp[i], &tmp[high]);

        if (i == median) {
            return tmp[i];
        } else if (i < median) {
            low = i + 1;
        } else {
            high = i - 1;
        }
    }
}

// 优化的中值滤波（针对小数组）
static uint32_t median_filter_opt(uint32_t buf[], uint8_t n)
{
    // 对于小数组（n<=5），使用优化的冒泡排序
    if (n <= 5) {
        // 优化版冒泡排序（提前终止）
        for (uint8_t i = 0; i < n - 1; i++) {
            uint8_t swapped = 0;
            for (uint8_t j = 0; j < n - i - 1; j++) {
                if (buf[j] > buf[j + 1]) {
                    uint32_t t = buf[j];
                    buf[j] = buf[j + 1];
                    buf[j + 1] = t;
                    swapped = 1;
                }
            }
            if (!swapped)
                break;
        }
        return buf[n / 2];
    } else {
        // 大数组使用快速选择
        return quick_select(buf, n);
    }
}

// 优化的滑动平均（使用移位代替除法）
static uint32_t moving_average_opt(uint32_t new_val)
{
    static uint32_t sum = 0;

    // 减去最旧的值，加上最新的值
    sum = sum - adc_moving_buf[moving_idx] + new_val;
    adc_moving_buf[moving_idx] = new_val;
    moving_idx = (moving_idx + 1) & (MOVING_AVG_LEN - 1);  // 要求MOVING_AVG_LEN为2的幂

    // 使用移位进行除法（假设MOVING_AVG_LEN是2的幂）
#if (MOVING_AVG_LEN == 4)
    return sum >> 2;  // 除以4
#elif (MOVING_AVG_LEN == 8)
    return sum >> 3;  // 除以8
#elif (MOVING_AVG_LEN == 16)
    return sum >> 4;  // 除以16
#else
    return sum / MOVING_AVG_LEN;  // 通用除法
#endif
}

#if USE_FAST_LOOKUP
// 使用预计算的插值查找表（更快）
static float interpolate_temperature_fast(uint32_t adc)
{
    static uint8_t last_index = 0;

    // 边界检查
    if (adc >= NTC_adc_table[0]) {
        return -40.0f;
    }
    if (adc <= NTC_adc_table[NTC_TABLE_SIZE - 1]) {
        return 120.0f;
    }

    // 从上一次的位置开始查找（大多数情况下变化不大）
    uint8_t i = last_index;

    // 向前或向后查找
    if (adc < NTC_adc_table[i]) {
        // 向后查找
        while (i < NTC_TABLE_SIZE - 1 && adc < NTC_adc_table[i + 1]) {
            i++;
        }
    } else if (adc > NTC_adc_table[i]) {
        // 向前查找
        while (i > 0 && adc > NTC_adc_table[i - 1]) {
            i--;
        }
        if (i > 0)
            i--;
    }

    last_index = i;

    // 线性插值
    float ratio = (float)(NTC_adc_table[i] - adc) / (float)(NTC_adc_table[i] - NTC_adc_table[i + 1]);

    return (float)NTC_temperature_table[i] + ratio * (float)(NTC_temperature_table[i + 1] - NTC_temperature_table[i]);
}
#else
// 优化的二分查找插值
static float interpolate_temperature_fast(uint32_t adc)
{
    // 边界检查
    if (adc >= NTC_adc_table[0]) {
        return -40.0f;
    }
    if (adc <= NTC_adc_table[NTC_TABLE_SIZE - 1]) {
        return 120.0f;
    }

    // 二分查找
    uint8_t left = 0;
    uint8_t right = NTC_TABLE_SIZE - 1;

    while (right - left > 1) {
        uint8_t mid = left + (right - left) / 2;

        if (NTC_adc_table[mid] > adc) {
            left = mid;
        } else {
            right = mid;
        }
    }

    // 线性插值
    float ratio = (float)(NTC_adc_table[left] - adc) / (float)(NTC_adc_table[left] - NTC_adc_table[right]);

    return (float)NTC_temperature_table[left] +
           ratio * (float)(NTC_temperature_table[right] - NTC_temperature_table[left]);
}
#endif

// 优化的低通滤波器（使用整数运算减少浮点计算）
static float low_pass_filter(float new_val, float last_val)
{
    // 使用预计算的系数
    static const float alpha = LOW_PASS_ALPHA;
    static const float beta = 1.0f - LOW_PASS_ALPHA;

    return alpha * new_val + beta * last_val;
}

// ========== DMA模式相关函数 ==========
#if NTC_USE_DMA

#if USE_DOUBLE_BUFFER
// DMA转换完成回调函数
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
    if (hadc->Instance == ADC1) {
        // 切换缓冲区并设置数据就绪标志
        active_buf ^= 1;  // 切换0/1
        data_ready = 1;

        // 重新配置DMA到新缓冲区
        HAL_ADC_Stop_DMA(hadc);
        HAL_ADC_Start_DMA(hadc, (uint32_t*)adc_dma_buf[active_buf], ADC_DMA_BUFFER_SIZE);
    }
}

// 从DMA缓冲区提取数据
static int extract_dma_data(uint32_t* dest, uint8_t n)
{
    if (!data_ready) {
        return -1;  // 数据未就绪
    }

    uint8_t ready_buf = active_buf ^ 1;  // 获取就绪的缓冲区

    // 提取最新的n个数据
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (ADC_DMA_BUFFER_SIZE - n + i) & (ADC_DMA_BUFFER_SIZE - 1);
        dest[i] = adc_dma_buf[ready_buf][idx];
    }

    data_ready = 0;  // 清除标志
    return 0;
}
#else
// 单缓冲区模式
static int extract_dma_data(uint32_t* dest, uint8_t n)
{
    // 直接从DMA缓冲区读取最新数据
    for (uint8_t i = 0; i < n; i++) {
        uint8_t idx = (ADC_DMA_BUFFER_SIZE - n + i) % ADC_DMA_BUFFER_SIZE;
        dest[i] = adc_dma_buf[idx];
    }
    return 0;
}
#endif

#endif  // NTC_USE_DMA

// ========== 核心采样函数 ==========
static int ntc_sample_and_filter(uint32_t* p_adc_out)
{
    uint32_t final_adc = 0;

#if NTC_USE_DMA
    // DMA模式 - 优化版本
#if USE_DOUBLE_BUFFER
    // 双缓冲区模式，无需暂停DMA
    if (extract_dma_data(adc_raw, NTC_NUM) != 0) {
        // 数据未就绪，返回上次值
        static uint32_t last_good_value = ADC_MAX_VALUE / 2;
        *p_adc_out = last_good_value;
        return -1;
    }
#else
    // 单缓冲区模式，需要短暂暂停DMA
    osKernelLock();
    HAL_ADC_Stop_DMA(&hadc1);

    // 复制数据到本地缓冲区
    for (uint8_t i = 0; i < NTC_NUM; i++) {
        uint8_t idx = (ADC_DMA_BUFFER_SIZE - NTC_NUM + i) % ADC_DMA_BUFFER_SIZE;
        adc_raw[i] = adc_dma_buf[idx];
    }

    HAL_ADC_Start_DMA(&hadc1, adc_dma_buf, ADC_DMA_BUFFER_SIZE);
    osKernelUnlock();
#endif

    // 应用滤波
    uint32_t med = median_filter_opt(adc_raw, NTC_NUM);
    final_adc = moving_average_opt(med);

#else
    // 非DMA模式 - 优化版本
    uint32_t tmp_buffer[NTC_NUM];

    // 启动ADC连续转换（如果支持）
    HAL_ADC_Start(&hadc1);

    for (uint8_t i = 0; i < NTC_NUM; i++) {
        if (HAL_ADC_PollForConversion(&hadc1, 2) == HAL_OK) {  // 减少超时时间
            tmp_buffer[i] = HAL_ADC_GetValue(&hadc1);
        } else {
            tmp_buffer[i] = ADC_MAX_VALUE / 2;
        }
    }

    HAL_ADC_Stop(&hadc1);

    // 应用滤波
    uint32_t med = median_filter_opt(tmp_buffer, NTC_NUM);
    final_adc = moving_average_opt(med);

#endif  // NTC_USE_DMA

    *p_adc_out = final_adc;
    return 0;
}

// ========== 公共API ==========

void ntc_init(void)
{
    // 初始化滑动平均缓冲区
    for (uint8_t i = 0; i < MOVING_AVG_LEN; i++) {
        adc_moving_buf[i] = ADC_MAX_VALUE / 2;
    }

#if NTC_USE_DMA
    // DMA模式初始化
#if USE_DOUBLE_BUFFER
    // 初始化双缓冲区
    for (uint8_t i = 0; i < 2; i++) {
        for (uint8_t j = 0; j < ADC_DMA_BUFFER_SIZE; j++) {
            adc_dma_buf[i][j] = ADC_MAX_VALUE / 2;
        }
    }
    active_buf = 0;
    data_ready = 0;

    // 启动DMA到第一个缓冲区
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_dma_buf[0], ADC_DMA_BUFFER_SIZE);
#else
    // 单缓冲区初始化
    for (uint8_t i = 0; i < ADC_DMA_BUFFER_SIZE; i++) {
        adc_dma_buf[i] = ADC_MAX_VALUE / 2;
    }
    HAL_ADC_Start_DMA(&hadc1, adc_dma_buf, ADC_DMA_BUFFER_SIZE);
#endif
#endif  // NTC_USE_DMA

    // 初始化状态变量
    last_filtered_temp = 25.0f;
    is_first = 1;
    moving_idx = 0;
    ntc_stuck_counter = 0;
    ntc_fault_active = false;
}

/**
 * @brief 检测NTC传感器故障（卡死/开路/短路）
 * @param adc_raw_val 当前ADC原始中值
 * @retval true: 传感器故障, false: 正常
 */
static bool ntc_detect_fault(uint32_t adc_raw_val)
{
    /* 开路检测：ADC接近最大值 */
    if (adc_raw_val >= ADC_MAX_VALUE - 10) {
        ntc_fault_active = true;
        return true;
    }

    /* 短路检测：ADC接近0 */
    if (adc_raw_val <= 10) {
        ntc_fault_active = true;
        return true;
    }

    /* 卡死检测：连续多次读数不变 */
    if (adc_raw_val == ntc_last_raw_adc) {
        if (++ntc_stuck_counter >= NTC_STUCK_THRESHOLD) {
            ntc_fault_active = true;
            return true;
        }
    } else {
        ntc_stuck_counter = 0;
        ntc_last_raw_adc = adc_raw_val;
    }

    ntc_fault_active = false;
    return false;
}

/**
 * @brief 获取NTC故障状态
 */
bool ntc_is_fault(void)
{
    return ntc_fault_active;
}

/**
 * @brief 清除NTC故障状态
 */
void ntc_clear_fault(void)
{
    ntc_fault_active = false;
    ntc_stuck_counter = 0;
}

int ntc_read(float* temperature)
{
    if (!temperature) {
        return -1;
    }

    uint32_t adc_val = 0;
    int ret = ntc_sample_and_filter(&adc_val);

    // 采样失败处理
    if (ret != 0) {
        if (is_first) {
            *temperature = 25.0f;
            return -1;
        } else {
            // 返回上次的有效温度
            *temperature = last_filtered_temp;
            return -2;  // 采样失败但返回缓存的温度
        }
    }

    // ADC值合理性检查
    if (adc_val > ADC_MAX_VALUE) {
        adc_val = ADC_MAX_VALUE;
    }

    // 传感器故障检测
    if (ntc_detect_fault(adc_val)) {
        *temperature = last_filtered_temp;
        return -4;  // 传感器故障
    }

    // 查表计算温度
    float raw_temp = interpolate_temperature_fast(adc_val);
    raw_temp += g_temp_offset;  // 应用温度补偿偏移量

    // 低通滤波
    float filtered_temp;
    if (is_first) {
        is_first = 0;
        last_filtered_temp = raw_temp;
        filtered_temp = raw_temp;
    } else {
        filtered_temp = low_pass_filter(raw_temp, last_filtered_temp);
        last_filtered_temp = filtered_temp;
    }

    // 温度范围检查
    if (filtered_temp <= TEMP_MIN_VALID || filtered_temp >= TEMP_MAX_VALID) {
        // 超出范围，返回上次有效值
        static float last_valid_temp = 25.0f;
        if (filtered_temp > TEMP_MIN_VALID && filtered_temp < TEMP_MAX_VALID) {
            last_valid_temp = filtered_temp;
        }
        *temperature = last_valid_temp;
        return -3;  // 温度超出范围
    }

    *temperature = filtered_temp;
    return 0;
}

// 直接获取ADC值（用于调试）
int ntc_get_adc_value(uint32_t* adc_value)
{
    if (!adc_value) {
        return -1;
    }
    return ntc_sample_and_filter(adc_value);
}

// 获取原始采样数据（用于调试）
int ntc_get_raw_samples(uint32_t* buffer, uint8_t size)
{
    if (!buffer || size < NTC_NUM) {
        return -1;
    }

#if NTC_USE_DMA
    // DMA模式下，需要暂停DMA获取数据
    osKernelLock();
    HAL_ADC_Stop_DMA(&hadc1);

    // 复制最新数据
#if USE_DOUBLE_BUFFER
    uint8_t ready_buf = active_buf ^ 1;
    for (uint8_t i = 0; i < NTC_NUM; i++) {
        uint8_t idx = (ADC_DMA_BUFFER_SIZE - NTC_NUM + i) & (ADC_DMA_BUFFER_SIZE - 1);
        buffer[i] = adc_dma_buf[ready_buf][idx];
    }
#else
    for (uint8_t i = 0; i < NTC_NUM; i++) {
        uint8_t idx = (ADC_DMA_BUFFER_SIZE - NTC_NUM + i) % ADC_DMA_BUFFER_SIZE;
        buffer[i] = adc_dma_buf[idx];
    }
#endif

#if USE_DOUBLE_BUFFER
    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc_dma_buf[active_buf], ADC_DMA_BUFFER_SIZE);
#else
    HAL_ADC_Start_DMA(&hadc1, adc_dma_buf, ADC_DMA_BUFFER_SIZE);
#endif
    osKernelUnlock();
#else
    // 非DMA模式，直接采样
    for (uint8_t i = 0; i < NTC_NUM; i++) {
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
            buffer[i] = HAL_ADC_GetValue(&hadc1);
        } else {
            buffer[i] = ADC_MAX_VALUE / 2;
        }
        HAL_ADC_Stop(&hadc1);

        if (i < NTC_NUM - 1) {
            HAL_Delay(ADC_SAMPLE_DELAY_MS);
        }
    }
#endif

    return 0;
}

// 温度补偿函数
void ntc_set_temperature_offset(float offset)
{
    // 可以通过偏移量校准温度
    g_temp_offset = offset;
}

// 重置滤波器状态
void ntc_reset_filter(void)
{
    last_filtered_temp = 25.0f;
    is_first = 1;
    moving_idx = 0;

    // 重置滑动平均缓冲区
    for (uint8_t i = 0; i < MOVING_AVG_LEN; i++) {
        adc_moving_buf[i] = ADC_MAX_VALUE / 2;
    }
}
